// Copyright 2014-2015 Josh Pieper, jjp@pobox.com.  All rights reserved.
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

#pragma once

#include <cmath>

#include <boost/assert.hpp>
#include <boost/format.hpp>

#include <Eigen/Core>

#include "point3d.h"

namespace legtool {
class Quaternion;
inline Quaternion operator*(const Quaternion& lhs,
                            const Quaternion& rhs);

class Quaternion {
 public:
  Quaternion(double w, double x, double y, double z)
      : w_(w), x_(x), y_(y), z_(z) {}

  Quaternion()
      : w_(1.0), x_(0.), y_(0.), z_(0.) {}

  std::string str() const {
    return (boost::format("%f %f %f %f") % w_ % x_ % y_ % z_).str();
  }

  Point3D Rotate(const Point3D& vector3d) const {
    Quaternion p(0.0,
                 vector3d[0],
                 vector3d[1],
                 vector3d[2]);
    Quaternion q = *this * p * conjugated();
    return Point3D(q.x(), q.y(), q.z());
  }

  Quaternion conjugated() const {
    return Quaternion(w_, -x_, -y_, -z_);
  }

  Quaternion normalized() const {
    double norm = std::sqrt(w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_);
    return Quaternion(w_ / norm,
                      x_ / norm,
                      y_ / norm,
                      z_ / norm);
  }

  Eigen::Matrix<double, 3, 3> matrix() const {
    Eigen::Matrix<double, 3, 3> r;

    r(0, 0) = w_ * w_ + x_ * x_ - y_ * y_ - z_ * z_;
    r(0, 1) = 2 * (x_ * y_ - w_ * z_);
    r(0, 2) = 2 * (w_ * y_ + x_ * z_);
    r(1, 0) = 2 * (x_ * y_ + w_ * z_);
    r(1, 1) = w_ * w_ - x_ * x_ + y_ * y_ - z_ * z_;
    r(1, 2) = 2 * (y_ * z_ - w_ * x_);
    r(2, 0) = 2 * (x_ * z_ - w_ * y_);
    r(2, 1) = 2 * (w_ * x_ + y_ * z_);
    r(2, 2) = w_ * w_ - x_ * x_ - y_ * y_ + z_ * z_;

    return r;
  }

  /// Euler angles are in roll, pitch, then yaw.
  ///  +roll -> right side down
  ///  +pitch -> forward edge up
  ///  +yaw -> clockwise looking down
  struct Euler {
    double roll_rad;
    double pitch_rad;
    double yaw_rad;

    Euler() : roll_rad(0.), pitch_rad(0.), yaw_rad(0.) {}
  };

  Euler euler() const {
    double sp = 2 * (w_ * x_ + y_ * z_);
    Euler result;
    if (std::abs(sp - 1.0) < 1e-8) { // north pole
      result.pitch_rad = M_PI_2;
      result.roll_rad = 0;
      result.yaw_rad = -std::atan2((w_ * y_ + x_ * z_),
                                   -(y_ * z_ - w_ * x_));
    } else if (std::abs(sp + 1.0) < 1e-8) { // south pole
      result.pitch_rad = -M_PI_2;
      result.roll_rad = 0;
      result.yaw_rad = std::atan2((w_ * y_ + x_ * z_),
                                  (y_ * z_ - w_ * x_));
    } else {
      result.pitch_rad = std::asin(sp);
      result.roll_rad = -std::atan2(2 * (x_ * z_ - w_ * y_),
                                    1.0 - 2 * x_ * x_ - 2 * y_ * y_);
      result.yaw_rad = std::atan2(2 * (x_ * y_ - w_ * z_),
                                  1 - 2 * x_ * x_ - 2 * z_ * z_);
    }

    return result;
  }

  static Quaternion FromEuler(
      double roll_rad, double pitch_rad, double yaw_rad) {
    // Quaternions multiply in opposite order, and we want to get into
    // roll, pitch, then yaw as standard.
    return (Quaternion::FromAxisAngle(yaw_rad, 0, 0, -1) *
            Quaternion::FromAxisAngle(pitch_rad, 1, 0, 0) *
            Quaternion::FromAxisAngle(roll_rad, 0, 1, 0));
  }

  static Quaternion FromAxisAngle(
      double angle_rad, double x, double y, double z) {
    double c = std::cos(angle_rad / 2.0);
    double s = std::sin(angle_rad / 2.0);

    return Quaternion(c, x * s, y * s, z * s);
  }

  static Quaternion IntegrateRotationRate(
      double roll_rate_rps, double pitch_rate_rps, double yaw_rate_rps,
      double dt_s) {
    // This simple technique will yield terrible results if the total
    // delta is too large.
    const double kMaxIntegrationAngle = 0.5;

    BOOST_ASSERT(roll_rate_rps * dt_s < kMaxIntegrationAngle);
    BOOST_ASSERT(pitch_rate_rps * dt_s < kMaxIntegrationAngle);
    BOOST_ASSERT(yaw_rate_rps * dt_s < kMaxIntegrationAngle);

    return Quaternion(1.0,
                      0.5 * pitch_rate_rps * dt_s,
                      0.5 * roll_rate_rps * dt_s,
                      -0.5 * yaw_rate_rps * dt_s).normalized();
  }

  double w() const { return w_; }
  double x() const { return x_; }
  double y() const { return y_; }
  double z() const { return z_; }

 private:
  double w_;
  double x_;
  double y_;
  double z_;
};

inline Quaternion operator*(const Quaternion& lhs,
                            const Quaternion& rhs) {
  double a = lhs.w();
  double b = lhs.x();
  double c = lhs.y();
  double d = lhs.z();

  double e = rhs.w();
  double f = rhs.x();
  double g = rhs.y();
  double h = rhs.z();

  return Quaternion(a * e - b * f - c * g - d * h,
                    b * e + a * f + c * h - d * g,
                    a * g - b * h + c * e + d * f,
                    a * h + b * g - c * f + d * e);
}
}