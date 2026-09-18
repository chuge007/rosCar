#pragma once

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
};

}  // namespace crawling

