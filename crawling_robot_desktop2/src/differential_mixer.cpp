#include "differential_mixer.h"

#include <algorithm>
#include <cmath>

namespace crawling {

WheelTargets DifferentialMixer::mix(double linearMps, double angularRadps,
                                    double trackWidthM, double minimumInnerRatio) {
  if (!(trackWidthM > 0.0) || !std::isfinite(trackWidthM) ||
      !std::isfinite(linearMps) || !std::isfinite(angularRadps)) {
    return {};
  }

  minimumInnerRatio = std::clamp(minimumInnerRatio, 0.0, 1.0);
  if (std::abs(linearMps) > 1e-9) {
    const double maximumSameDirectionAngular =
        (2.0 * std::abs(linearMps) / trackWidthM) *
        ((1.0 - minimumInnerRatio) / (1.0 + minimumInnerRatio));
    angularRadps = std::clamp(angularRadps, -maximumSameDirectionAngular,
                              maximumSameDirectionAngular);
  }

  const double halfTrack = trackWidthM * 0.5;
  // Positive angular input denotes a right turn: the physical left wheel is
  // the outer wheel and therefore runs faster. Keep the turn component
  // antisymmetric so a pivot drives the wheels in opposite directions while
  // a translating turn keeps the commanded centre velocity unchanged.
  const double turnComponent = angularRadps * halfTrack;
  const double leftMps = linearMps + turnComponent;
  const double rightMps = linearMps - turnComponent;
  return {leftMps, rightMps, (leftMps + rightMps) * 0.5,
          (leftMps - rightMps) / trackWidthM};
}

WheelTargets DifferentialMixer::limitUniformly(WheelTargets targets,
                                                double maximumWheelSpeedMps) {
  if (!(maximumWheelSpeedMps > 0.0) || !std::isfinite(maximumWheelSpeedMps)) {
    return {};
  }
  const double peak = std::max(std::abs(targets.leftMps), std::abs(targets.rightMps));
  if (peak > maximumWheelSpeedMps) {
    const double scale = maximumWheelSpeedMps / peak;
    targets.leftMps *= scale;
    targets.rightMps *= scale;
    targets.linearMps *= scale;
    targets.angularRadps *= scale;
  }
  return targets;
}

}  // namespace crawling
