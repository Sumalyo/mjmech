// Copyright 2019 Josh Pieper, jjp@pobox.com.  All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mech/quadruped_debug.h"

#include <boost/asio/streambuf.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

#include "mjlib/base/tokenizer.h"

namespace pl = std::placeholders;

namespace mjmech {
namespace mech {

using QM = QuadrupedCommand::Mode;

class QuadrupedDebug::Impl {
 public:
  Impl(QuadrupedControl* control,
       mjlib::io::SharedStream stream)
      : control_(control),
        stream_(stream) {
    StartRead();
  }

  void StartRead() {
    boost::asio::async_read_until(
        *stream_,
        streambuf_,
        '\n',
        std::bind(&Impl::HandleRead, this, pl::_1));
  }

  void HandleRead(const mjlib::base::error_code& ec) {
    mjlib::base::FailIf(ec);

    std::string line;
    std::istream istr(&streambuf_);
    std::getline(istr, line);

    HandleLine(line);
    StartRead();
  }

  void HandleLine(const std::string& line) {
    mjlib::base::Tokenizer tokenizer(line, " ");

    QuadrupedCommand qcommand;

    const auto cmd = tokenizer.next();
    if (cmd == "stop") {
      qcommand.mode = QM::kStopped;
    } else if (cmd == "j") {
      if (!ParseJoint(&qcommand, tokenizer.remaining())) {
        return;
      }
    } else if (cmd == "l") {
      if (!ParseLeg(&qcommand, tokenizer.remaining())) {
        return;
      }
    } else if (cmd == "zero") {
      qcommand.mode = QM::kZeroVelocity;
    } else if (cmd == "stand") {
      if (!ParseStand(&qcommand, tokenizer.remaining())) {
        return;
      }
    } else if (cmd == "rest") {
      if (!ParseRest(&qcommand, tokenizer.remaining())) {
        return;
      }
    } else if (cmd == "jump") {
      if (!ParseJump(&qcommand, tokenizer.remaining())) {
        return;
      }
    } else {
      Write("unknown command");
      return;
    }

    Write("OK");
    control_->Command(qcommand);
  }

  bool ParseJump(QuadrupedCommand* qcommand, std::string_view remaining) {
    qcommand->mode = QM::kJump;
    qcommand->jump = QuadrupedCommand::Jump();

    mjlib::base::Tokenizer tokenizer(remaining, " ");

    while (true) {
      const auto token = std::string(tokenizer.next());
      if (token.size() < 1) { return true; }

      if (ParseRBCommand(qcommand, token)) {
      } else if (token[0] == 'a') {
        qcommand->jump->acceleration_mm_s2 = std::stod(token.substr(1));
      } else if (token[0] == '+') {
        qcommand->jump->repeat = true;
      } else {
        Write("jump parse error: " + token);
        return false;
      }
    }

    return true;
  }

  bool ParseRest(QuadrupedCommand* qcommand, std::string_view remaining) {
    qcommand->mode = QM::kRest;

    mjlib::base::Tokenizer tokenizer(remaining, " ");

    while (true) {
      const auto token = std::string(tokenizer.next());
      if (token.size() < 1) { return true; }

      if (ParseRBCommand(qcommand, token)) {
      } else {
        Write("rest parse error: " + token);
        return false;
      }
    }

    return true;
  }

  bool ParseRBCommand(QuadrupedCommand* qcommand, const std::string& token) {
    if (token[0] == 'r') {
      qcommand->pose_mm_RB.translation() = ParseVector(token.substr(1));
      return true;
    }
    if (token[0] == 'R') {
      auto rpy = ParseVector(token.substr(1));
      qcommand->pose_mm_RB.so3() = Sophus::SO3d(
          Eigen::AngleAxisd(base::Radians(rpy[0]), Eigen::Vector3d::UnitX()) *
          Eigen::AngleAxisd(base::Radians(rpy[1]), Eigen::Vector3d::UnitY()) *
          Eigen::AngleAxisd(base::Radians(rpy[2]), Eigen::Vector3d::UnitZ()));
      return true;
    }

    return false;
  }

  bool ParseStand(QuadrupedCommand* qcommand, std::string_view remaining) {
    qcommand->mode = QM::kStandUp;

    return true;
  }

  bool ParseLeg(QuadrupedCommand* qcommand, std::string_view remaining) {
    qcommand->mode = QM::kLeg;

    mjlib::base::Tokenizer tokenizer(remaining, " ");

    QuadrupedCommand::Leg* current_leg = nullptr;

    while (true) {
      const auto token = std::string(tokenizer.next());
      if (token.size() < 1) { return true; }

      if (token[0] == 'l') {
        // A new leg.
        qcommand->legs_B.push_back({});
        current_leg = &qcommand->legs_B.back();
        current_leg->power = true;
        current_leg->leg_id = std::stoi(token.substr(1));
      } else if (token[0] == 'o' && current_leg) {
        current_leg->power = false;
      } else if (token[0] == 'z' && current_leg) {
        current_leg->zero_velocity = true;
      } else if (token[0] == 'p' && current_leg) {
        current_leg->position_mm = ParseVector(token.substr(1));
      } else if (token[0] == 'v' && current_leg) {
        current_leg->velocity_mm_s = ParseVector(token.substr(1));
      } else if (token[0] == 'f' && current_leg) {
        current_leg->force_N = ParseVector(token.substr(1));
      } else if (token[0] == 'k' && current_leg) {
        current_leg->kp_scale = ParseVector(token.substr(1));
      } else if (token[0] == 'd' && current_leg) {
        current_leg->kd_scale = ParseVector(token.substr(1));
      } else {
        Write("leg parse error: " + token);
        return false;
      }
    }
    return true;
  }

  base::Point3D ParseVector(std::string_view str) {
    mjlib::base::Tokenizer tokenizer(str, ",");

    const auto x = std::stod(std::string(tokenizer.next()));
    const auto y = std::stod(std::string(tokenizer.next()));
    const auto z = std::stod(std::string(tokenizer.next()));
    return base::Point3D{x, y, z};
  }

  bool ParseJoint(QuadrupedCommand* qcommand, std::string_view remaining) {
    qcommand->mode = QM::kJoint;

    mjlib::base::Tokenizer tokenizer(remaining, " ");

    QuadrupedCommand::Joint* current_joint = nullptr;

    while (true) {
      const auto token = std::string(tokenizer.next());
      if (token.size() < 1) { return true; }

      if (token[0] == 'j') {
        // A new joint.
        qcommand->joints.push_back({});
        current_joint = &qcommand->joints.back();
        current_joint->power = true;
        current_joint->id = std::stoi(token.substr(1));
      } else if (token[0] == 'o' && current_joint) {
        current_joint->power = false;
      } else if (token[0] == 'z' && current_joint) {
        current_joint->zero_velocity = true;
      } else if (token[0] == 'a' && current_joint) {
        current_joint->angle_deg = std::stod(token.substr(1));
      } else if (token[0] == 'v' && current_joint) {
        current_joint->velocity_dps = std::stod(token.substr(1));
      } else if (token[0] == 't' && current_joint) {
        current_joint->torque_Nm = std::stod(token.substr(1));
      } else if (token[0] == 'p' && current_joint) {
        current_joint->kp_scale = std::stod(token.substr(1));
      } else if (token[0] == 'd' && current_joint) {
        current_joint->kd_scale = std::stod(token.substr(1));
      } else if (token[0] == 'm' && current_joint) {
        current_joint->max_torque_Nm = std::stod(token.substr(1));
      } else if (token[0] == 's' && current_joint) {
        current_joint->stop_angle_deg = std::stod(token.substr(1));
      } else {
        Write("joint parse error: " + token);
        return false;
      }
    }

    return true;
  }

  void Write(const std::string& line) {
    std::ostream ostr(&write_streambuf_);
    ostr.write(line.data(), line.size());
    ostr.write("\r\n", 2);

    if (!write_outstanding_) {
      StartWrite();
    }
  }

  void StartWrite() {
    write_outstanding_ = true;

    boost::asio::async_write(
        *stream_,
        write_streambuf_,
        std::bind(&Impl::HandleWrite, this, pl::_1));
  }

  void HandleWrite(const mjlib::base::error_code& ec) {
    mjlib::base::FailIf(ec);

    write_outstanding_ = false;

    // TODO: Verify that I don't need to consume from the streambuf.

    if (streambuf_.size() != 0) {
      StartWrite();
    }
  }

  QuadrupedControl* const control_;
  mjlib::io::SharedStream stream_;
  boost::asio::streambuf streambuf_;

  boost::asio::streambuf write_streambuf_;
  bool write_outstanding_ = false;
};

QuadrupedDebug::QuadrupedDebug(QuadrupedControl* control,
                               mjlib::io::SharedStream stream)
    : impl_(std::make_unique<Impl>(control, stream)) {}

QuadrupedDebug::~QuadrupedDebug() {}

}
}
