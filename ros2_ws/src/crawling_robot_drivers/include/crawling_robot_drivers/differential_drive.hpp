#pragma once

#include <algorithm>
#include <cmath>

namespace crawling_robot_drivers {

struct DifferentialWheelSpeeds {
  double left_m_s = 0.0;
  double right_m_s = 0.0;
  double applied_angular_rad_s = 0.0;
};

// Convert a body twist to physical wheel speeds. During a translating turn,
// keep the inner/outer wheel ratio above the configured value so both physical
// wheels keep the same direction. With zero linear speed, a pivot turn remains
// available and the wheels may rotate in opposite directions.
inline DifferentialWheelSpeeds mixDifferentialWheelSpeeds(
    double linear_m_s, double angular_rad_s, double wheel_separation_m,
    double minimum_inner_wheel_ratio = 0.5) {
  if (!(wheel_separation_m > 0.0) || !std::isfinite(wheel_separation_m)) {
    return {};
  }
  if (!std::isfinite(linear_m_s) || !std::isfinite(angular_rad_s)) {
    return {};
  }

  minimum_inner_wheel_ratio =
      std::clamp(minimum_inner_wheel_ratio, 0.0, 1.0);
  if (std::abs(linear_m_s) > 1e-9) {
    const double max_same_direction_angular =
        (2.0 * std::abs(linear_m_s) / wheel_separation_m) *
        ((1.0 - minimum_inner_wheel_ratio) /
         (1.0 + minimum_inner_wheel_ratio));
    if (std::abs(angular_rad_s) > max_same_direction_angular) {
      angular_rad_s = std::copysign(max_same_direction_angular, angular_rad_s);
    }
  }

  const double half_separation = wheel_separation_m * 0.5;
  // The drive motors are mounted on opposite axle faces.  On this chassis the
  // physical left/right wheel labels are therefore reversed from the common
  // mathematical convention used by the motor axes.  Preserve the operator's
  // turn direction: positive angular command makes the physical left wheel
  // the outer (faster) wheel.
  return {linear_m_s + angular_rad_s * half_separation,
          linear_m_s - angular_rad_s * half_separation, angular_rad_s};
}

// Apply the wheel-speed safety cap uniformly. Independent per-wheel clipping
// changes the requested differential ratio and makes one wheel appear much
// slower than the other.
inline DifferentialWheelSpeeds limitDifferentialWheelSpeeds(
    DifferentialWheelSpeeds speeds, double max_wheel_speed_m_s) {
  if (!(max_wheel_speed_m_s > 0.0) || !std::isfinite(max_wheel_speed_m_s)) {
    return {};
  }
  const double peak = std::max(std::abs(speeds.left_m_s),
                               std::abs(speeds.right_m_s));
  if (peak > max_wheel_speed_m_s) {
    const double scale = max_wheel_speed_m_s / peak;
    speeds.left_m_s *= scale;
    speeds.right_m_s *= scale;
    speeds.applied_angular_rad_s *= scale;
  }
  return speeds;
}

}  // namespace crawling_robot_drivers
