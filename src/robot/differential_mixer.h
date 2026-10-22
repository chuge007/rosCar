#pragma once

#include <optional>

namespace crawling {

struct WheelTargets {
  double leftMps = 0.0;
  double rightMps = 0.0;
  double linearMps = 0.0;
  double angularRadps = 0.0;
};

class DifferentialMixer final {
 public:
  static WheelTargets mix(double linearMps, double angularRadps,
                          double trackWidthM, double minimumInnerRatio);
  static WheelTargets limitUniformly(WheelTargets targets, double maximumWheelSpeedMps);
  // Restore the requested pair mean after response synchronization, then
  // reduce only differential motion to respect the physical wheel limits.
  // An unattainable mean or invalid pair is an error, not a lower speed.
  static std::optional<WheelTargets> preserveLinearSpeed(
      WheelTargets targets, double linearMps, double trackWidthM,
      double minimumInnerRatio, double maximumWheelSpeedMps,
      double maximumAngularRadps);
};

}  // namespace crawling
