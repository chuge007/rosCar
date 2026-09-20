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

std::optional<WheelTargets> DifferentialMixer::preserveLinearSpeed(
    WheelTargets targets, double linearMps, double trackWidthM,
    double minimumInnerRatio, double maximumWheelSpeedMps,
    double maximumAngularRadps) {
  if (!std::isfinite(targets.leftMps) || !std::isfinite(targets.rightMps) ||
      !std::isfinite(linearMps) || !std::isfinite(trackWidthM) ||
      !std::isfinite(minimumInnerRatio) ||
      !std::isfinite(maximumWheelSpeedMps) ||
      !std::isfinite(maximumAngularRadps) || trackWidthM <= 0.0 ||
      maximumWheelSpeedMps <= 0.0 || maximumAngularRadps < 0.0 ||
      minimumInnerRatio < 0.0 || minimumInnerRatio > 1.0 ||
      std::abs(linearMps) > maximumWheelSpeedMps) {
    return std::nullopt;
  }

  double turnComponent = targets.leftMps * 0.5 - targets.rightMps * 0.5;
  double turnLimit = std::min(maximumWheelSpeedMps - std::abs(linearMps),
                              maximumAngularRadps * trackWidthM * 0.5);
  if (std::abs(linearMps) > 1e-9) {
    const double pairMean = targets.leftMps * 0.5 + targets.rightMps * 0.5;
    if (pairMean * linearMps <= 0.0 ||
        targets.leftMps * linearMps < 0.0 ||
        targets.rightMps * linearMps < 0.0 || std::abs(pairMean) <= 1e-12) {
      return std::nullopt;
    }
    // Common scaling keeps the synchronizer's response-compensated ratio.
    // It does not imply the measured vehicle speed equals its command.
    turnComponent = linearMps * (turnComponent / pairMean);
    turnLimit = std::min(turnLimit, std::abs(linearMps) *
        (1.0 - minimumInnerRatio) / (1.0 + minimumInnerRatio));
  }
  turnComponent = std::clamp(turnComponent, -turnLimit, turnLimit);
  return WheelTargets{linearMps + turnComponent, linearMps - turnComponent,
                      linearMps, 2.0 * turnComponent / trackWidthM};
}

}  // namespace crawling
