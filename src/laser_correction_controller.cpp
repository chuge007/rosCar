#include "laser_correction_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

double clamp(double value, double low, double high) {
  return std::max(low, std::min(value, high));
}

double rate(double current, double target, double maximumStep) {
  return current + clamp(target - current, -maximumStep, maximumStep);
}

double wrapAngle(double angle) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;
  while (angle > kPi) angle -= kTwoPi;
  while (angle < -kPi) angle += kTwoPi;
  return angle;
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
}

void LaserCorrectionController::setEnabled(bool enabled) {
  status_.active = enabled;
  filteredError_ = 0.0;
  filteredHeadingError_ = 0.0;
  filteredGyroRadps_ = 0.0;
  currentLinear_ = 0.0;
  currentAngular_ = 0.0;
  previousControlMs_ = -1;

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

  const double alpha = clamp(settings_.filterAlpha, 0.02, 1.0);
  const double rawHeadingError = wrapAngle(currentYawRad_ - settings_.headingReferenceRad);
  filteredHeadingError_ = wrapAngle(
      filteredHeadingError_ + alpha * wrapAngle(rawHeadingError - filteredHeadingError_));
  const double gyroAlpha = clamp(settings_.derivativeAlpha, 0.02, 1.0);
  filteredGyroRadps_ += gyroAlpha * (currentGyroRadps_ - filteredGyroRadps_);
}

void LaserCorrectionController::resetReferenceToCurrentContour() {
  bool changed = false;
  if (status_.contourValid) {
    settings_.targetLateralM = status_.contourLateralM;
    changed = true;
  }
  if (lastImuMs_ >= 0 && clock_.elapsed() - lastImuMs_ <= settings_.imuTimeoutMs) {
    settings_.headingReferenceRad = currentYawRad_;
    changed = true;
  }
  if (!changed) {
    emit logMessage(QStringLiteral("设定参考失败：没有有效激光轮廓或 IMU"));
    return;
  }

  filteredError_ = 0.0;
  filteredHeadingError_ = 0.0;
  filteredGyroRadps_ = 0.0;
  status_.reason = QStringLiteral("已将当前激光位置和 IMU 航向设为参考");
  emit logMessage(status_.reason);
  publishStatus();
}

void LaserCorrectionController::processPointCloud(const QVector<QVector3D>& cloud) {
  const qint64 now = clock_.elapsed();
  lastProfileMs_ = now;

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
  const double rawError = measured - settings_.targetLateralM;
  const double alpha = clamp(settings_.filterAlpha, 0.02, 1.0);
  filteredError_ += alpha * (rawError - filteredError_);
  status_.contourValid = true;
  status_.contourLateralM = measured;
  status_.lateralErrorM = filteredError_;
  status_.headingErrorRad = filteredHeadingError_;
  status_.gyroRadps = filteredGyroRadps_;
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

  const double lateralError =
      std::abs(filteredError_) < settings_.deadbandM ? 0.0 : filteredError_;
  const double headingError =
      std::abs(filteredHeadingError_) < settings_.headingDeadbandRad
          ? 0.0
          : filteredHeadingError_;

  // Positive angular command is a physical right turn in DifferentialMixer.
  // steeringSign calibrates the laser lateral axis; headingSign calibrates the
  // IMU yaw convention independently.
  const double lateralTerm = settings_.steeringSign * settings_.proportionalGain * lateralError;
  const double headingTerm = settings_.headingSign * settings_.headingGain * headingError;
  const double gyroTerm = settings_.headingSign * settings_.derivativeGain * filteredGyroRadps_;
  const double targetAngular = clamp(lateralTerm + headingTerm + gyroTerm,
                                     -settings_.maxAngularRadps,
                                     settings_.maxAngularRadps);

  const double lateralScale = clamp(
      std::abs(filteredError_) / std::max(0.001, settings_.maxLateralErrorM), 0.0, 1.0);
  const double headingScale = clamp(
      std::abs(filteredHeadingError_) / std::max(0.01, settings_.maxHeadingErrorRad), 0.0, 1.0);
  const double scale = clamp(1.0 - settings_.speedReductionGain *
                                       std::max(lateralScale, headingScale),
                             settings_.minSpeedScale, 1.0);
  const double dt = previousControlMs_ < 0
                        ? 0.033
                        : clamp((now - previousControlMs_) / 1000.0, 0.005, 0.2);
  previousControlMs_ = now;
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
