#include "wheel_synchronizer.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

double keepDirection(double value, double reference) {
  if (reference > 0.0) {
    return std::max(0.0, value);
  }
  if (reference < 0.0) {
    return std::min(0.0, value);
  }
  return 0.0;
}

}  // namespace

SynchronizerResult WheelSynchronizer::update(
    double leftTargetMps, double rightTargetMps, double leftActualMps,
    double rightActualMps, bool feedbackFresh, double deltaSeconds,
    const SynchronizerConfig& configuration) {
  SynchronizerResult result;
  result.leftMps = leftTargetMps;
  result.rightMps = rightTargetMps;

  const bool targetsShareDirection = leftTargetMps * rightTargetMps > 0.0;
  const double slowerTarget = std::min(std::abs(leftTargetMps), std::abs(rightTargetMps));
  if (!feedbackFresh || !targetsShareDirection ||
      slowerTarget < configuration.minimumControlledSpeedMps ||
      !(deltaSeconds > 0.0) || !std::isfinite(deltaSeconds)) {
    reset();
    return result;
  }

  const double leftRatio = leftActualMps / leftTargetMps;
  const double rightRatio = rightActualMps / rightTargetMps;
  if (!std::isfinite(leftRatio) || !std::isfinite(rightRatio)) {
    reset();
    return result;
  }

  result.normalizedError = rightRatio - leftRatio;
  const double nextIntegral = std::clamp(integral_ + result.normalizedError * deltaSeconds,
                                         -2.0, 2.0);
  const double targetPeak = std::max(std::abs(leftTargetMps), std::abs(rightTargetMps));
  const double unclampedCorrection =
      (configuration.proportionalGain * result.normalizedError +
       configuration.integralGain * nextIntegral) * targetPeak;
  result.correctionMps = std::clamp(unclampedCorrection,
                                    -configuration.maximumCorrectionMps,
                                    configuration.maximumCorrectionMps);

  // A positive error means the right side is making more progress than the
  // left relative to its own target. Shift the pair toward the left wheel.
  const double direction = leftTargetMps > 0.0 ? 1.0 : -1.0;
  result.leftMps = keepDirection(leftTargetMps + direction * result.correctionMps,
                                 leftTargetMps);
  result.rightMps = keepDirection(rightTargetMps - direction * result.correctionMps,
                                  rightTargetMps);
  integral_ = nextIntegral;
  result.active = true;
  return result;
}

void WheelSynchronizer::reset() {
  integral_ = 0.0;
}

}  // namespace crawling

