#include "wheel_synchronizer.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kMinimumResponseFactor = 0.02;
constexpr double kMaximumResponseFactor = 2.0;

int directionOf(double value) {
  return value > 0.0 ? 1 : (value < 0.0 ? -1 : 0);
}

void updateResponseFactor(double appliedCommandMps, double actualMps,
                          double deltaSeconds, double* responseFactor,
                          bool* haveResponse,
                          const SynchronizerConfig& configuration) {
  if (responseFactor == nullptr || haveResponse == nullptr ||
      std::abs(appliedCommandMps) <= 1e-9 ||
      !std::isfinite(appliedCommandMps) || !std::isfinite(actualMps)) {
    return;
  }
  // A sign mismatch indicates stale feedback or a direction configuration
  // problem. It must not be turned into a speed compensation factor.
  if (std::abs(actualMps) > 1e-6 &&
      directionOf(actualMps) != directionOf(appliedCommandMps)) {
    return;
  }

  const double measured = std::clamp(
      std::abs(actualMps / appliedCommandMps), kMinimumResponseFactor,
      kMaximumResponseFactor);
  if (!*haveResponse) {
    *responseFactor = measured;
    *haveResponse = true;
    return;
  }

  const double filterWeight = std::clamp(
      configuration.proportionalGain +
          configuration.integralGain * deltaSeconds,
      0.10, 0.75);
  *responseFactor += filterWeight * (measured - *responseFactor);
}

}  // namespace

SynchronizerResult WheelSynchronizer::update(
    double leftTargetMps, double rightTargetMps, double leftActualMps,
    double rightActualMps, double leftAppliedCommandMps,
    double rightAppliedCommandMps, bool feedbackUpdated, double deltaSeconds,
    const SynchronizerConfig& configuration) {
  SynchronizerResult result;
  result.leftMps = leftTargetMps;
  result.rightMps = rightTargetMps;

  const bool targetsShareDirection = leftTargetMps * rightTargetMps > 0.0;
  const double slowerTarget = std::min(std::abs(leftTargetMps), std::abs(rightTargetMps));
  if (!targetsShareDirection ||
      slowerTarget < configuration.minimumControlledSpeedMps) {
    reset();
    return result;
  }

  const int targetDirection = directionOf(leftTargetMps);
  if (targetDirection_ != 0 && targetDirection_ != targetDirection) {
    reset();
  }
  targetDirection_ = targetDirection;

  if (feedbackUpdated && deltaSeconds > 0.0 && std::isfinite(deltaSeconds) &&
      leftAppliedCommandMps * rightAppliedCommandMps > 0.0 &&
      directionOf(leftAppliedCommandMps) == targetDirection &&
      directionOf(rightAppliedCommandMps) == targetDirection) {
    updateResponseFactor(leftAppliedCommandMps, leftActualMps, deltaSeconds,
                         &leftResponseFactor_, &haveLeftResponse_,
                         configuration);
    updateResponseFactor(rightAppliedCommandMps, rightActualMps, deltaSeconds,
                         &rightResponseFactor_, &haveRightResponse_,
                         configuration);
  }

  if (!haveLeftResponse_ || !haveRightResponse_) {
    return result;
  }

  result.leftResponseFactor = leftResponseFactor_;
  result.rightResponseFactor = rightResponseFactor_;
  result.normalizedError = rightResponseFactor_ - leftResponseFactor_;

  // Match predicted wheel progress by preserving the less responsive side's
  // command and throttling only the more responsive side. Multiplying each
  // requested curve target by these factors preserves the requested physical
  // left/right speed ratio without asking the limited motor for more speed.
  const double commonResponse = std::min(leftResponseFactor_, rightResponseFactor_);
  const auto limitedCommandScale = [&configuration](double targetMps,
                                                     double desiredScale) {
    const double targetMagnitude = std::abs(targetMps);
    const double maximumReduction = std::min(
        targetMagnitude, configuration.maximumCorrectionMps);
    const double minimumScale = targetMagnitude > 0.0
        ? 1.0 - maximumReduction / targetMagnitude
        : 1.0;
    return std::clamp(desiredScale, minimumScale, 1.0);
  };
  result.leftCommandScale = limitedCommandScale(
      leftTargetMps, commonResponse / leftResponseFactor_);
  result.rightCommandScale = limitedCommandScale(
      rightTargetMps, commonResponse / rightResponseFactor_);
  result.leftMps = leftTargetMps * result.leftCommandScale;
  result.rightMps = rightTargetMps * result.rightCommandScale;

  const double leftReduction = std::abs(leftTargetMps) - std::abs(result.leftMps);
  const double rightReduction = std::abs(rightTargetMps) - std::abs(result.rightMps);
  result.correctionMps = rightReduction >= leftReduction
      ? rightReduction
      : -leftReduction;
  result.active = leftReduction > 1e-9 || rightReduction > 1e-9;
  return result;
}

void WheelSynchronizer::reset() {
  leftResponseFactor_ = 1.0;
  rightResponseFactor_ = 1.0;
  haveLeftResponse_ = false;
  haveRightResponse_ = false;
  targetDirection_ = 0;
}

}  // namespace crawling
