#include "laser_seam_trajectory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

constexpr qint64 kMaximumPredictionAgeMs = 2500;
constexpr qint64 kMaximumHistoryAgeMs = 60000;
constexpr double kMaximumHistoryDistanceM = 0.30;
constexpr double kMinimumObservationSpacingM = 0.0005;
constexpr double kMaximumRmsM = 0.006;
constexpr double kMinimumSpanM = 0.006;
constexpr int kMinimumObservations = 5;

bool finitePose(const LaserSeamPose& pose) {
  return std::isfinite(pose.longitudinalM) &&
         std::isfinite(pose.lateralM) && std::isfinite(pose.yawRad);
}

double poseDistance(const LaserSeamPose& a, const LaserSeamPose& b) {
  return std::hypot(a.longitudinalM - b.longitudinalM,
                    a.lateralM - b.lateralM);
}

}  // namespace

void LaserSeamTrajectory::clear() { observations_.clear(); }

void LaserSeamTrajectory::observe(const LaserSeamPose& pose, qint64 receivedMs,
                                 double leftLateralM, double rightLateralM,
                                 double lookaheadM, double confidence) {
  if (!finitePose(pose) || receivedMs < 0 ||
      !std::isfinite(leftLateralM) || !std::isfinite(rightLateralM) ||
      !(rightLateralM > leftLateralM) || !std::isfinite(lookaheadM) ||
      lookaheadM < 0.0 || !std::isfinite(confidence) || confidence < 0.08) {
    return;
  }
  Observation observation;
  observation.pose = pose;
  observation.receivedMs = receivedMs;
  observation.confidence = std::clamp(confidence, 0.0, 1.0);
  const double cosine = std::cos(pose.yawRad);
  const double sine = std::sin(pose.yawRad);
  auto& world = observation.worldEdges;
  world.leftLongitudinalM = pose.longitudinalM + cosine * lookaheadM -
                            sine * leftLateralM;
  world.rightLongitudinalM = pose.longitudinalM + cosine * lookaheadM -
                             sine * rightLateralM;
  world.longitudinalM =
      (world.leftLongitudinalM + world.rightLongitudinalM) * 0.5;
  world.leftLateralM = pose.lateralM + sine * lookaheadM +
                      cosine * leftLateralM;
  world.rightLateralM = pose.lateralM + sine * lookaheadM +
                       cosine * rightLateralM;
  // Stationary high-rate frames improve the newest observation, but cannot
  // manufacture longitudinal support for a line fit.
  if (!observations_.isEmpty() &&
      poseDistance(observations_.last().pose, pose) <
          kMinimumObservationSpacingM) {
    observations_.last() = observation;
  } else {
    observations_.append(observation);
  }
  while (observations_.size() > 1 &&
         (observations_.size() > 600 ||
          receivedMs - observations_.first().receivedMs > kMaximumHistoryAgeMs ||
          poseDistance(observations_.first().pose, pose) >
              kMaximumHistoryDistanceM)) {
    observations_.remove(0);
  }
}

LaserSeamPrediction LaserSeamTrajectory::predict(
    const LaserSeamPose& pose, qint64 receivedMs, double lookaheadM,
    double imageSpanM, double cameraLateralSign) const {
  LaserSeamPrediction result;
  if (observations_.size() < kMinimumObservations || !finitePose(pose) ||
      !std::isfinite(lookaheadM) || !std::isfinite(imageSpanM) ||
      imageSpanM <= 0.0 || !std::isfinite(cameraLateralSign) ||
      std::abs(cameraLateralSign) < 0.5) {
    return result;
  }
  result.observationAgeMs = receivedMs - observations_.last().receivedMs;
  if (result.observationAgeMs < 0 ||
      result.observationAgeMs > kMaximumPredictionAgeMs) return result;

  QVector<LaserEdgeSample> localSamples;
  localSamples.reserve(observations_.size());
  const double cosine = std::cos(pose.yawRad);
  const double sine = std::sin(pose.yawRad);
  double minimumX = std::numeric_limits<double>::infinity();
  double maximumX = -std::numeric_limits<double>::infinity();
  double confidenceSum = 0.0;
  for (const auto& observation : observations_) {
    if (receivedMs - observation.receivedMs > kMaximumHistoryAgeMs ||
        poseDistance(observation.pose, pose) > kMaximumHistoryDistanceM) {
      continue;
    }
    const auto& world = observation.worldEdges;
    const double leftDx = world.leftLongitudinalM - pose.longitudinalM;
    const double rightDx = world.rightLongitudinalM - pose.longitudinalM;
    const double leftDy = world.leftLateralM - pose.lateralM;
    const double rightDy = world.rightLateralM - pose.lateralM;
    LaserEdgeSample local;
    local.leftLongitudinalM = cosine * leftDx + sine * leftDy;
    local.rightLongitudinalM = cosine * rightDx + sine * rightDy;
    local.longitudinalM =
        (local.leftLongitudinalM + local.rightLongitudinalM) * 0.5;
    local.leftLateralM = -sine * leftDx + cosine * leftDy;
    local.rightLateralM = -sine * rightDx + cosine * rightDy;
    localSamples.append(local);
    minimumX = std::min(minimumX, local.longitudinalM);
    maximumX = std::max(maximumX, local.longitudinalM);
    confidenceSum += observation.confidence;
  }
  const LaserPathFit fit = LaserPathEstimator::fit(
      localSamples, kMinimumObservations, kMinimumSpanM, kMaximumRmsM);
  result.sampleCount = fit.sampleCount;
  result.observedSpanM = fit.observedSpanM;
  if (!fit.valid ||
      std::abs(std::atan(fit.leftSlope) - std::atan(fit.rightSlope)) > 0.14) {
    return result;
  }
  result.extrapolationM = std::max(
      {0.0, minimumX - lookaheadM, lookaheadM - maximumX});
  // A short scan cannot justify extrapolating far ahead. A mature rolling
  // cloud may bridge a little more occlusion, but never indefinite driving.
  const double maximumExtrapolationM =
      std::clamp(fit.spanM * 0.60, 0.012, 0.060);
  if (result.extrapolationM > maximumExtrapolationM) return result;
  const double leftM = fit.leftInterceptM + fit.leftSlope * lookaheadM;
  const double rightM = fit.rightInterceptM + fit.rightSlope * lookaheadM;
  const double widthM = rightM - leftM;
  const double centerM = (leftM + rightM) * 0.5;
  if (!std::isfinite(centerM) || !std::isfinite(widthM) ||
      widthM <= imageSpanM * 0.003 || widthM >= imageSpanM * 0.90) {
    return result;
  }
  result.absoluteCenterRatio = 0.5 + centerM /
                                      (cameraLateralSign * imageSpanM);
  result.absoluteWidthRatio = widthM / imageSpanM;
  // Do not clamp an off-image prediction to an image edge: that would turn
  // an out-of-range weld into a plausible in-range measurement.
  if (result.absoluteCenterRatio < 0.0 ||
      result.absoluteCenterRatio > 1.0) return result;
  result.uncertaintyRatio = std::clamp(
      (0.003 + fit.rmsErrorM * 2.5 + result.extrapolationM * 0.15 +
       result.observationAgeMs * 0.000001) / imageSpanM,
      0.015, 0.10);
  const double freshness = 1.0 -
      static_cast<double>(result.observationAgeMs) / kMaximumPredictionAgeMs;
  const double extensionQuality =
      1.0 - result.extrapolationM / maximumExtrapolationM;
  const double geometryQuality = 1.0 - fit.rmsErrorM / kMaximumRmsM;
  result.confidence = std::clamp(
      confidenceSum / std::max(1, localSamples.size()) * freshness *
          extensionQuality * geometryQuality,
      0.0, 1.0);
  result.valid = result.confidence >= 0.03;
  return result;
}

}  // namespace crawling
