#include "laser_correction_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double clamp(double value, double low, double high) {
  return std::max(low, std::min(value, high));
}

double rate(double current, double target, double maximumStep) {
  return current + clamp(target - current, -maximumStep, maximumStep);
}

double wrapAngle(double angle) {
  while (angle > kPi) angle -= kTwoPi;
  while (angle < -kPi) angle += kTwoPi;
  return angle;
}

double filterAlpha(double cutoffHz, double dt, double fallbackAlpha) {
  if (!std::isfinite(cutoffHz) || cutoffHz <= 0.0) {
    return clamp(fallbackAlpha, 0.02, 1.0);
  }
  return clamp(1.0 - std::exp(-kTwoPi * cutoffHz * clamp(dt, 0.001, 0.2)),
               0.02, 1.0);
}

double applyDeadband(double value, double width) {
  const double deadband = std::max(0.0, width);
  if (std::abs(value) <= deadband) return 0.0;
  return std::copysign(std::abs(value) - deadband, value);
}

}  // namespace

LaserCorrectionController::LaserCorrectionController(QObject* parent) : QObject(parent) {
  clock_.start();
  watchdog_.setInterval(20);
  connect(&watchdog_, &QTimer::timeout, this, &LaserCorrectionController::watchdogTick);
  watchdog_.start();
  status_.reason = QStringLiteral("自动纠偏未启动");
}

void LaserCorrectionController::setSettings(const crawling::LaserCorrectionSettings& value) {
  settings_ = value;
  settings_.steeringSign = value.steeringSign < 0 ? -1 : 1;
  settings_.headingSign = value.headingSign < 0 ? -1 : 1;
  settings_.gyroSign = value.gyroSign < 0 ? -1 : 1;
}

void LaserCorrectionController::setEnabled(bool enabled) {
  status_.active = enabled;
  filteredError_ = 0.0;
  lateralIntegralMs_ = 0.0;
  lateralFilterInitialized_ = false;
  currentLinear_ = 0.0;
  currentAngular_ = 0.0;
  previousControlMs_ = -1;

  const qint64 now = clock_.elapsed();
  const bool imuFresh =
      lastImuMs_ >= 0 && now - lastImuMs_ <= settings_.imuTimeoutMs;
  if (imuFresh) {
    filteredHeadingError_ = settings_.headingSign *
                            wrapAngle(currentYawRad_ - settings_.headingReferenceRad);
    filteredGyroRadps_ = currentGyroRadps_;
    headingFilterInitialized_ = true;
    gyroFilterInitialized_ = true;
    previousImuMs_ = lastImuMs_;
  } else {
    filteredHeadingError_ = 0.0;
    filteredGyroRadps_ = 0.0;
    headingFilterInitialized_ = false;
    gyroFilterInitialized_ = false;
    previousImuMs_ = -1;
  }

  status_.lateralIntegralMs = 0.0;

  if (!enabled) {
    stop(QStringLiteral("自动纠偏已停止"));
    return;
  }

  status_.contourValid = false;
  status_.reason = QStringLiteral("自动纠偏已启动，等待有效轮廓和 IMU");
  emit logMessage(status_.reason);
  publishStatus();
}

void LaserCorrectionController::setImuSample(const crawling::ImuSample& sample) {
  if (!std::isfinite(sample.yawRad) || !std::isfinite(sample.gyroZRadps)) {
    return;
  }

  const qint64 now = clock_.elapsed();
  currentYawRad_ = sample.yawRad;
  currentGyroRadps_ = sample.gyroZRadps;
  lastImuMs_ = now;

  const double dt = previousImuMs_ < 0
                        ? 0.01
                        : clamp((now - previousImuMs_) / 1000.0, 0.001, 0.2);
  previousImuMs_ = now;

  const double rawHeadingError = settings_.headingSign *
                                 wrapAngle(currentYawRad_ - settings_.headingReferenceRad);
  if (!headingFilterInitialized_) {
    filteredHeadingError_ = rawHeadingError;
    headingFilterInitialized_ = true;
  } else {
    const double alpha = filterAlpha(settings_.headingFilterCutoffHz, dt,
                                     settings_.filterAlpha);
    filteredHeadingError_ = wrapAngle(
        filteredHeadingError_ + alpha * wrapAngle(rawHeadingError - filteredHeadingError_));
  }

  if (!gyroFilterInitialized_) {
    filteredGyroRadps_ = currentGyroRadps_;
    gyroFilterInitialized_ = true;
  } else {
    const double gyroAlpha = filterAlpha(settings_.gyroFilterCutoffHz, dt,
                                         settings_.derivativeAlpha);
    filteredGyroRadps_ += gyroAlpha * (currentGyroRadps_ - filteredGyroRadps_);
  }
}

void LaserCorrectionController::resetReferenceToCurrentContour() {
  const qint64 now = clock_.elapsed();
  const bool contourChanged = status_.contourValid;
  const bool headingChanged =
      lastImuMs_ >= 0 && now - lastImuMs_ <= settings_.imuTimeoutMs;
  if (status_.contourValid) {
    settings_.targetLateralM = status_.contourLateralM;
  }
  if (headingChanged) {
    settings_.headingReferenceRad = currentYawRad_;
  }
  if (!contourChanged && !headingChanged) {
    emit logMessage(QStringLiteral("设定参考失败：没有有效激光轮廓或 IMU"));
    return;
  }

  if (contourChanged) {
    filteredError_ = 0.0;
    lateralIntegralMs_ = 0.0;
    lateralFilterInitialized_ = true;
    status_.sensorLateralErrorM = 0.0;
    status_.lookaheadOffsetM = 0.0;
    status_.lateralErrorM = 0.0;
    status_.lateralIntegralMs = 0.0;
  }
  if (headingChanged) {
    filteredHeadingError_ = 0.0;
    headingFilterInitialized_ = true;
    status_.headingErrorRad = 0.0;
  }
  status_.reason = contourChanged && headingChanged
                       ? QStringLiteral("已将当前激光位置和 IMU 航向设为参考")
                       : contourChanged ? QStringLiteral("已将当前激光位置设为参考")
                                        : QStringLiteral("已将当前 IMU 航向设为参考");
  emit logMessage(status_.reason);
  publishStatus();
}

void LaserCorrectionController::processPointCloud(const QVector<QVector3D>& cloud) {
  const qint64 now = clock_.elapsed();
  lastProfileMs_ = now;
  const double dt = previousControlMs_ < 0
                        ? 0.033
                        : clamp((now - previousControlMs_) / 1000.0, 0.005, 0.2);
  previousControlMs_ = now;

  struct Sample {
    double lateral;
    double height;
  };

  QVector<Sample> samples;
  samples.reserve(cloud.size());
  const double low = std::min(settings_.lateralMinM, settings_.lateralMaxM);
  const double high = std::max(settings_.lateralMinM, settings_.lateralMaxM);
  for (const QVector3D& point : cloud) {
    const double lateral = settings_.lateralUsesY ? point.y() : point.x();
    const double height = settings_.heightUsesZ ? point.z() : point.y();
    if (std::isfinite(lateral) && std::isfinite(height) && lateral >= low && lateral <= high) {
      samples.append({lateral, height});
    }
  }

  std::sort(samples.begin(), samples.end(),
            [](const Sample& left, const Sample& right) { return left.lateral < right.lateral; });
  if (samples.size() < 6) {
    status_.contourValid = false;
    if (status_.active) stop(QStringLiteral("有效轮廓点不足，已停止自动纠偏"));
    return;
  }

  QVector<double> heights;
  heights.reserve(samples.size());
  for (const Sample& sample : samples) heights.append(sample.height);
  QVector<double> sortedHeights = heights;
  std::sort(sortedHeights.begin(), sortedHeights.end());
  const int baseCount = std::max(3, int(sortedHeights.size() * 0.60));
  double base = 0.0;
  for (int i = 0; i < baseCount; ++i) base += sortedHeights[i];
  base /= baseCount;

  int peak = -1;
  double prominence = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < samples.size(); ++i) {
    const double value = samples[i].height - base;
    if (value > prominence) {
      prominence = value;
      peak = i;
    }
  }
  if (peak < 0 || !std::isfinite(prominence) ||
      prominence < settings_.prominenceThresholdM) {
    status_.contourValid = false;
    if (status_.active) stop(QStringLiteral("未检测到有效凸起焊缝，已停止自动纠偏"));
    return;
  }

  double total = 0.0;
  double lateralSum = 0.0;
  int count = 0;
  const double minProminence = std::max(settings_.prominenceThresholdM, prominence * 0.35);
  for (const Sample& sample : samples) {
    const double relativeHeight = sample.height - base;
    if (std::abs(sample.lateral - samples[peak].lateral) <=
            std::max(0.001, settings_.contourHalfWidthM) &&
        relativeHeight >= minProminence) {
      const double weight = relativeHeight - minProminence + 1e-9;
      total += weight;
      lateralSum += sample.lateral * weight;
      ++count;
    }
  }
  if (count < 3 || total <= 0.0) {
    status_.contourValid = false;
    if (status_.active) stop(QStringLiteral("焊缝轮廓置信度不足，已停止自动纠偏"));
    return;
  }

  const double measured = lateralSum / total;
  const double sensorLateralError =
      settings_.steeringSign * (measured - settings_.targetLateralM);
  const double lookaheadOffset =
      std::max(0.0, settings_.lookaheadDistanceM) * std::sin(filteredHeadingError_);
  const double rawLookaheadError = sensorLateralError + lookaheadOffset;
  if (!lateralFilterInitialized_) {
    filteredError_ = rawLookaheadError;
    lateralFilterInitialized_ = true;
  } else {
    const double alpha = filterAlpha(settings_.lateralFilterCutoffHz, dt,
                                     settings_.filterAlpha);
    filteredError_ += alpha * (rawLookaheadError - filteredError_);
  }
  status_.contourValid = true;
  status_.contourLateralM = measured;
  status_.sensorLateralErrorM = sensorLateralError;
  status_.lookaheadOffsetM = lookaheadOffset;
  status_.lateralErrorM = filteredError_;
  status_.headingErrorRad = filteredHeadingError_;
  status_.gyroRadps = settings_.gyroSign * filteredGyroRadps_;
  status_.candidatePoints = count;
  status_.confidence =
      clamp(prominence / std::max(0.004, settings_.prominenceThresholdM * 2.0), 0.0, 1.0) *
      clamp(double(count) / 9.0, 0.0, 1.0);

  if (!status_.active) {
    status_.reason = QStringLiteral("轮廓有效，自动纠偏未启动");
    publishStatus();
    return;
  }
  if (std::abs(filteredError_) > settings_.maxLateralErrorM) {
    stop(QStringLiteral("横向偏差超限，已停止自动纠偏"));
    return;
  }

  const bool imuFresh = lastImuMs_ >= 0 && now - lastImuMs_ <= settings_.imuTimeoutMs;
  if (!imuFresh) {
    stop(QStringLiteral("IMU 数据超时，已停止自动纠偏"));
    return;
  }
  if (std::abs(filteredHeadingError_) > settings_.maxHeadingErrorRad) {
    stop(QStringLiteral("航向误差超限，已停止自动纠偏"));
    return;
  }

  const double lateralError = applyDeadband(filteredError_, settings_.deadbandM);
  const double headingError =
      applyDeadband(filteredHeadingError_, settings_.headingDeadbandRad);

  // Positive angular command is a physical right turn in DifferentialMixer.
  // The laser and heading signs have already been applied in their error
  // definitions. gyroSign is independent and the leading minus makes Kr a
  // damping term rather than positive angular-rate feedback.
  const double lateralTerm = settings_.proportionalGain * lateralError;
  const double headingTerm = settings_.headingGain * headingError;
  const double gyroTerm = -settings_.derivativeGain * settings_.gyroSign *
                          filteredGyroRadps_;
  const double integralGain = std::max(0.0, settings_.integralGain);
  if (integralGain > 0.0) {
    const double integralLimit = std::max(0.0, settings_.integralLimitMs);
    const double candidate = clamp(lateralIntegralMs_ + lateralError * dt,
                                   -integralLimit, integralLimit);
    const double candidateAngular =
        lateralTerm + headingTerm + gyroTerm + integralGain * candidate;
    const bool windsUpPositive =
        candidateAngular > settings_.maxAngularRadps && lateralError > 0.0;
    const bool windsUpNegative =
        candidateAngular < -settings_.maxAngularRadps && lateralError < 0.0;
    if (!windsUpPositive && !windsUpNegative) lateralIntegralMs_ = candidate;
  } else {
    lateralIntegralMs_ = 0.0;
  }
  const double integralTerm = integralGain * lateralIntegralMs_;
  const double targetAngular = clamp(
      lateralTerm + integralTerm + headingTerm + gyroTerm,
      -settings_.maxAngularRadps, settings_.maxAngularRadps);
  status_.lateralIntegralMs = lateralIntegralMs_;

  const double lateralScale = clamp(
      std::abs(lateralError) / std::max(0.001, settings_.maxLateralErrorM), 0.0, 1.0);
  const double headingScale = clamp(
      std::abs(headingError) / std::max(0.01, settings_.maxHeadingErrorRad), 0.0, 1.0);
  const double scale = clamp(1.0 - settings_.speedReductionGain *
                                       std::max(lateralScale, headingScale),
                             settings_.minSpeedScale, 1.0);
  currentLinear_ = rate(currentLinear_, std::max(0.0, settings_.targetSpeedMps) * scale,
                        std::max(0.005, settings_.maxLinearAccelerationMps2) * dt);
  currentAngular_ = rate(currentAngular_, targetAngular,
                         std::max(0.01, settings_.maxAngularAccelerationRadps2) * dt);
  status_.linearCommandMps = currentLinear_;
  status_.angularCommandRadps = currentAngular_;
  status_.reason = QStringLiteral("自动纠偏运行中");
  emit commandChanged(currentLinear_, currentAngular_);
  publishStatus();
}

void LaserCorrectionController::watchdogTick() {
  if (!status_.active) return;
  const qint64 now = clock_.elapsed();
  if (lastProfileMs_ < 0 || now - lastProfileMs_ > settings_.profileTimeoutMs) {
    stop(QStringLiteral("激光数据超时，已停止自动纠偏"));
    return;
  }
  if (lastImuMs_ < 0 || now - lastImuMs_ > settings_.imuTimeoutMs) {
    stop(QStringLiteral("IMU 数据超时，已停止自动纠偏"));
  }
}

void LaserCorrectionController::stop(const QString& reason) {
  currentLinear_ = 0.0;
  currentAngular_ = 0.0;
  status_.active = false;
  status_.linearCommandMps = 0.0;
  status_.angularCommandRadps = 0.0;
  status_.reason = reason;
  emit commandChanged(0.0, 0.0);
  emit logMessage(reason);
  publishStatus();
}

void LaserCorrectionController::publishStatus() {
  emit statusChanged(status_);
}

}  // namespace crawling
