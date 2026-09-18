#include "laser_correction_controller.h"
#include "utf8_compat.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kPi = 3.14159265358979323846;
// These are device-side conventions, not operator-tunable correction
// parameters. The lateral value is the camera's calibration span and the
// sign maps image coordinates to the drive yaw convention.
constexpr double kInternalImageLateralSpanM = 0.20;
constexpr int kInternalSteeringSign = 1;
constexpr double kSoftSpeedReductionAngleRad = 25.0 * kPi / 180.0;
constexpr int kMaximumInitialSurveyRetries = 3;
constexpr qint64 kControlDiagnosticIntervalMs = 500;
constexpr qint64 kDetectionDiagnosticIntervalMs = 1000;

double clamp(double value, double low, double high) {
  return std::max(low, std::min(value, high));
}

double rate(double current, double target, double maximumStep) {
  return current + clamp(target - current, -maximumStep, maximumStep);
}

double normalizedAngle(double angle) {
  while (angle > kPi) angle -= 2.0 * kPi;
  while (angle < -kPi) angle += 2.0 * kPi;
  return angle;
}

}  // namespace

LaserCorrectionController::LaserCorrectionController(QObject* parent)
    : QObject(parent) {
  clock_.start();
  watchdog_.setInterval(20);
  watchdog_.setTimerType(Qt::PreciseTimer);
  connect(&watchdog_, &QTimer::timeout, this,
          &LaserCorrectionController::watchdogTick);
  watchdog_.start();
  status_.phase = phaseName(phase_);
  status_.reason = CRAWLING_TEXT("自动纠偏未启动");
}

void LaserCorrectionController::setSettings(
    const LaserCorrectionSettings& value) {
  settings_ = value;
  settings_.imageLateralSpanM = kInternalImageLateralSpanM;
  settings_.segmentLengthM = clamp(settings_.segmentLengthM, 0.02, 2.0);
  const double adaptiveSampleSpacingM =
      clamp(settings_.segmentLengthM * 0.075, 0.00075, 0.004);
  settings_.sampleSpacingM = clamp(settings_.sampleSpacingM, 0.0005,
                                   adaptiveSampleSpacingM);
  const int adaptiveMinimumFitSamples = settings_.segmentLengthM <= 0.03
                                            ? 4
                                            : (settings_.segmentLengthM <= 0.06
                                                   ? 6
                                                   : 8);
  settings_.minimumFitSamples =
      std::min(std::max(4, settings_.minimumFitSamples),
               adaptiveMinimumFitSamples);
  const double adaptiveMinimumSpanRatio = settings_.segmentLengthM <= 0.03
                                              ? 0.25
                                              : (settings_.segmentLengthM <= 0.06
                                                     ? 0.35
                                                     : 0.45);
  settings_.minimumFitSpanRatio =
      clamp(std::min(settings_.minimumFitSpanRatio,
                     adaptiveMinimumSpanRatio),
            0.2, 1.0);
  const double adaptiveMaximumFitRmsErrorM =
      clamp(settings_.segmentLengthM * 0.15, 0.002, 0.006);
  settings_.maximumFitRmsErrorM =
      clamp(settings_.maximumFitRmsErrorM, 0.0005,
            adaptiveMaximumFitRmsErrorM);
  settings_.returnToleranceM = clamp(settings_.returnToleranceM, 0.001,
                                     settings_.segmentLengthM * 0.25);
  settings_.settleTimeMs = std::max(100, settings_.settleTimeMs);
  settings_.wheelRadiusM = std::max(0.001, settings_.wheelRadiusM);
  settings_.trackWidthM = std::max(0.01, settings_.trackWidthM);
  settings_.steeringSign = kInternalSteeringSign;
  settings_.targetSpeedMps = std::max(0.001, settings_.targetSpeedMps);
  settings_.proportionalGain = clamp(settings_.proportionalGain, 0.0, 10.0);
  settings_.derivativeGain = clamp(settings_.derivativeGain, 0.0, 2.0);
  settings_.derivativeAlpha = clamp(settings_.derivativeAlpha, 0.02, 1.0);
  settings_.headingFitBlend = clamp(settings_.headingFitBlend, 0.05, 0.50);
  settings_.maximumCurvatureRadPerM =
      clamp(settings_.maximumCurvatureRadPerM, 0.05, 1.0);
  settings_.laserCenterFeedbackGain =
      clamp(settings_.laserCenterFeedbackGain, 0.0, 1.5);
  settings_.laserCenterLookaheadM =
      clamp(settings_.laserCenterLookaheadM, 0.05, 1.0);
  settings_.minimumInnerWheelRatio =
      clamp(settings_.minimumInnerWheelRatio, 0.45, 0.90);
  settings_.maxAngularRadps = std::max(0.01, settings_.maxAngularRadps);
  settings_.detectionTimeoutMs = std::max(200, settings_.detectionTimeoutMs);
  settings_.transientDetectionHoldMs =
      clamp(settings_.transientDetectionHoldMs, 100,
            settings_.detectionTimeoutMs);
  settings_.motionStallTimeoutMs =
      std::max(1000, settings_.motionStallTimeoutMs);
}

void LaserCorrectionController::setEnabled(bool enabled) {
  if (!enabled) {
    stop(CRAWLING_TEXT("自动纠偏已停止"));
    return;
  }

  resetSession();
  beginTrajectorySession();
  status_.active = true;
  enabledAtMs_ = clock_.elapsed();
  emit logMessage(
      CRAWLING_TEXT("纠偏参数：速度 %1 mm/s，分段 %2 mm，Kp %3，Kd %4，"
                    "微分滤波 %5，内侧轮最低 %6%，角速度上限 %7 deg/s，"
                    "角加速度 %8 deg/s2，采样间距 %9 mm，最少样本 %10，"
                    "最小拟合跨度 %11%，最大拟合RMS %12 mm，"
                    "柔性曲率 %13 deg/m，拟合融合 %14%，中心反馈 %15%")
          .arg(settings_.targetSpeedMps * 1000.0, 0, 'f', 1)
          .arg(settings_.segmentLengthM * 1000.0, 0, 'f', 1)
          .arg(settings_.proportionalGain, 0, 'f', 2)
          .arg(settings_.derivativeGain, 0, 'f', 2)
          .arg(settings_.derivativeAlpha, 0, 'f', 2)
          .arg(settings_.minimumInnerWheelRatio * 100.0, 0, 'f', 0)
          .arg(settings_.maxAngularRadps * 180.0 / kPi, 0, 'f', 1)
          .arg(settings_.maxAngularAccelerationRadps2 * 180.0 / kPi, 0, 'f', 1)
          .arg(settings_.sampleSpacingM * 1000.0, 0, 'f', 1)
          .arg(settings_.minimumFitSamples)
          .arg(settings_.minimumFitSpanRatio * 100.0, 0, 'f', 0)
          .arg(settings_.maximumFitRmsErrorM * 1000.0, 0, 'f', 1)
          .arg(settings_.maximumCurvatureRadPerM * 180.0 / kPi, 0, 'f', 1)
          .arg(settings_.headingFitBlend * 100.0, 0, 'f', 0)
          .arg(settings_.laserCenterFeedbackGain * 100.0, 0, 'f', 0));
  setPhase(Phase::AwaitingInputs,
           CRAWLING_TEXT("自动纠偏已启动，等待新图像和轮端里程"));
  commandImmediateStop(status_.reason);
}

void LaserCorrectionController::processCameraImage(const QImage& image) {
  const qint64 now = clock_.elapsed();
  lastImageMs_ = now;
  LaserGapDetectorConfig detectorConfig = settings_.detector;
  if (gapTrackerValid_) {
    detectorConfig.expectedCenterRatio = trackedGapCenterRatio_;
    detectorConfig.expectedGapWidthRatio = trackedGapWidthRatio_;
    detectorConfig.expectedAbsoluteCenterRatio =
        trackedGapAbsoluteCenterRatio_;
    detectorConfig.referenceAbsoluteCenterRatio =
        referenceGapAbsoluteCenterRatio_;
    detectorConfig.expectedLineStartRatio = trackedLineStartRatio_;
    detectorConfig.expectedLineEndRatio = trackedLineEndRatio_;
    detectorConfig.expectedAxis = trackedGapHorizontal_ ? 1 : 2;
  }
  const LaserGapDetection detection =
      LaserGapDetector::detect(image, detectorConfig);

  status_.gapValid = detection.valid;
  status_.horizontalLaser = detection.valid ? detection.horizontal
                                            : trackedGapHorizontal_;
  status_.confidence = detection.valid ? detection.confidence : 0.0;
  if (detection.valid) {
    status_.gapStartPx = detection.gapStartPx;
    status_.gapEndPx = detection.gapEndPx;
    status_.supportingSamples = detection.supportingSamples;
  }

  const int lineSpanPx = detection.lineEndPx - detection.lineStartPx;
  if (status_.gapValid && lineSpanPx > 0) {
    const double leftRatio = clamp(
        static_cast<double>(detection.gapStartPx - detection.lineStartPx) /
            lineSpanPx,
        0.0, 1.0);
    const double rightRatio = clamp(
        static_cast<double>(detection.gapEndPx - detection.lineStartPx) /
            lineSpanPx,
        0.0, 1.0);
    latestLeftEdgeM_ =
        (leftRatio - 0.5) * settings_.imageLateralSpanM;
    latestRightEdgeM_ =
        (rightRatio - 0.5) * settings_.imageLateralSpanM;
    if (!(latestLeftEdgeM_ < latestRightEdgeM_)) status_.gapValid = false;
  } else {
    status_.gapValid = false;
  }

  if (!status_.gapValid) {
    ++invalidDetectionCount_;
    status_.confidence = 0.0;
    if (!status_.active) {
      status_.reason = CRAWLING_TEXT("未检测到激光线中间断口，自动纠偏未启动");
      publishStatus();
      return;
    }
    const qint64 heldDetectionAgeMs = now - lastTrackedGapMs_;
    if (gapTrackerValid_ && heldDetectionAgeMs >= 0 &&
        heldDetectionAgeMs <= settings_.transientDetectionHoldMs) {
      // A short occlusion or rejected bright run must not pulse the motor
      // command to zero. Keep steering from the last real detection, but mark
      // it as held so stale edge points are not added to the next path fit.
      status_.gapValid = true;
      status_.confidence = trackedGapConfidence_ * 0.5;
      detectionHeld_ = true;
      ++reusedDetectionCount_;
      status_.leftEdgeLateralM = latestLeftEdgeM_;
      status_.rightEdgeLateralM = latestRightEdgeM_;
      status_.reason = detection.continuityRejected
                           ? CRAWLING_TEXT("激光候选跳变，平滑沿用上一帧断口")
                           : CRAWLING_TEXT("激光短时遮挡，平滑沿用上一帧断口");
      if (lastDetectionDiagnosticMs_ < 0 ||
          now - lastDetectionDiagnosticMs_ >= kDetectionDiagnosticIntervalMs) {
        lastDetectionDiagnosticMs_ = now;
        emit logMessage(
            CRAWLING_TEXT("激光诊断：阶段 %1，模式 previous_gap_reuse，"
                          "中心 %2%，宽度 %3%，置信度 %4，无效帧 %5，沿用帧 %6")
                .arg(phaseName(phase_))
                .arg(trackedGapCenterRatio_ * 100.0, 0, 'f', 2)
                .arg(trackedGapWidthRatio_ * 100.0, 0, 'f', 2)
                .arg(status_.confidence, 0, 'f', 3)
                .arg(invalidDetectionCount_)
                .arg(reusedDetectionCount_));
        invalidDetectionCount_ = 0;
        reusedDetectionCount_ = 0;
      }
      publishStatus();
      return;
    }
    detectionHeld_ = false;
    if (phaseNeedsLaser()) {
      resetControllerError();
      commandImmediateStop(CRAWLING_TEXT("激光断口暂时无效，已暂停并等待恢复"));
    } else {
      publishStatus();
    }
    return;
  }

  const bool firstValidAfterEnable =
      status_.active && lastValidDetectionMs_ < enabledAtMs_;
  detectionHeld_ = false;
  lastValidDetectionMs_ = now;
  status_.gapCenterRatio = detection.normalizedCenter;
  status_.gapLateralM =
      (detection.normalizedCenter - 0.5) * settings_.imageLateralSpanM;
  status_.leftEdgeLateralM = latestLeftEdgeM_;
  status_.rightEdgeLateralM = latestRightEdgeM_;
  trackedGapCenterRatio_ = detection.normalizedCenter;
  trackedGapAbsoluteCenterRatio_ = detection.absoluteCenterRatio;
  if (referenceGapAbsoluteCenterRatio_ < 0.0) {
    referenceGapAbsoluteCenterRatio_ = detection.absoluteCenterRatio;
  }
  trackedGapHorizontal_ = detection.horizontal;
  trackedGapWidthRatio_ =
      static_cast<double>(detection.gapEndPx - detection.gapStartPx + 1) /
      std::max(1, detection.lineEndPx - detection.lineStartPx + 1);
  const int axisLength = detection.horizontal ? image.width() : image.height();
  trackedLineStartRatio_ =
      static_cast<double>(detection.lineStartPx) / std::max(1, axisLength - 1);
  trackedLineEndRatio_ =
      static_cast<double>(detection.lineEndPx) / std::max(1, axisLength - 1);
  trackedGapConfidence_ = detection.confidence;
  gapTrackerValid_ = true;
  lastTrackedGapMs_ = now;
  logDetectionDiagnostic(detection, now);

  if (!status_.active) {
    status_.reason = CRAWLING_TEXT("激光断口有效，自动纠偏未启动");
    publishStatus();
    return;
  }

  if (firstValidAfterEnable) {
    emit logMessage(CRAWLING_TEXT("检测到激光断口：左边缘 %1 px，右边缘 %2 px")
                        .arg(status_.gapStartPx)
                        .arg(status_.gapEndPx));
  }
  status_.reason = runningReason();
  tryStartSurvey();
  if (phase_ == Phase::SurveyForward || phase_ == Phase::TrackingForward) {
    appendCurrentEdgeSample();
  }
  publishStatus();
}

void LaserCorrectionController::shutdown() {
  watchdog_.stop();
  if (status_.active) {
    stop(CRAWLING_TEXT("软件正在关闭，自动纠偏已停止"));
    return;
  }
  commandImmediateStop(QString());
}

void LaserCorrectionController::logDetectionDiagnostic(
    const LaserGapDetection& detection, qint64 now) {
  if (!status_.active ||
      (lastDetectionDiagnosticMs_ >= 0 &&
       now - lastDetectionDiagnosticMs_ < kDetectionDiagnosticIntervalMs)) {
    return;
  }
  lastDetectionDiagnosticMs_ = now;
  const int lineSpanPx = detection.lineEndPx - detection.lineStartPx + 1;
  const int gapWidthPx = detection.gapEndPx - detection.gapStartPx + 1;
  emit logMessage(
      CRAWLING_TEXT("激光诊断：阶段 %1，模式 %2，方向 %3，激光范围 %4..%5 px，"
                    "断口 %6..%7 px，中心 %8%，宽度 %9%，置信度 %10，"
                    "支撑点 %11，无效帧 %12，沿用帧 %13")
          .arg(phaseName(phase_))
          .arg(detection.edgeBreakFallback ? CRAWLING_TEXT("edge_break_fallback")
                                           : CRAWLING_TEXT("normal_gap"))
          .arg(detection.horizontal ? CRAWLING_TEXT("横向")
                                    : CRAWLING_TEXT("纵向"))
          .arg(detection.lineStartPx)
          .arg(detection.lineEndPx)
          .arg(detection.gapStartPx)
          .arg(detection.gapEndPx)
          .arg(detection.normalizedCenter * 100.0, 0, 'f', 2)
          .arg(lineSpanPx > 0 ? gapWidthPx * 100.0 / lineSpanPx : 0.0,
               0, 'f', 2)
          .arg(detection.confidence, 0, 'f', 3)
          .arg(detection.supportingSamples)
          .arg(invalidDetectionCount_)
          .arg(reusedDetectionCount_));
  invalidDetectionCount_ = 0;
  reusedDetectionCount_ = 0;
}

void LaserCorrectionController::processDriveTelemetry(
    const DriveTelemetry& telemetry) {
  const qint64 now = clock_.elapsed();
  if (!status_.active) return;

  if (telemetry.state != DriveState::Enabled) {
    stop(CRAWLING_TEXT("底盘已离开使能状态，自动纠偏已停止"));
    return;
  }
  if (!telemetryUsable(telemetry)) {
    if (now - enabledAtMs_ > settings_.telemetryTimeoutMs &&
        (lastTelemetryMs_ < enabledAtMs_ ||
         now - lastTelemetryMs_ > settings_.telemetryTimeoutMs)) {
      stop(CRAWLING_TEXT("轮端里程反馈超时，已停止自动纠偏"));
    } else {
      commandImmediateStop(CRAWLING_TEXT("等待有效轮端里程反馈"));
    }
    return;
  }

  const qint64 previousTelemetryMs = lastTelemetryMs_;
  const double previousLeftSpeedMps = leftSpeedMps_;
  const double previousRightSpeedMps = rightSpeedMps_;
  leftSpeedMps_ = telemetry.left.wheelSpeedMps;
  rightSpeedMps_ = telemetry.right.wheelSpeedMps;
  driveAppliedLinearMps_ = telemetry.appliedLinearMps;
  driveAppliedAngularRadps_ = telemetry.appliedAngularRadps;
  leftTargetMps_ = telemetry.leftTargetMps;
  rightTargetMps_ = telemetry.rightTargetMps;
  integrateWheelMotion(now, previousTelemetryMs, previousLeftSpeedMps,
                       previousRightSpeedMps);
  haveTelemetry_ = true;
  lastTelemetryMs_ = now;

  tryStartSurvey();
  // Capture the segment-end point before advanceFromTelemetry() evaluates the
  // fit. Previously the final odometry update could end a short segment before
  // its latest valid laser edge pair was added.
  if ((phase_ == Phase::SurveyForward ||
       phase_ == Phase::TrackingForward) &&
      status_.gapValid) {
    appendCurrentEdgeSample();
  }
  advanceFromTelemetry(now);
  publishStatus();
}

void LaserCorrectionController::trajectorySessionStarted(
    quint64 sessionId, const QString& directory) {
  if (sessionId != trajectorySessionId_) return;
  status_.trajectoryDirectory = directory;
  emit logMessage(CRAWLING_TEXT("激光轨迹图目录：%1").arg(directory));
  publishStatus();
}

void LaserCorrectionController::trajectoryImageSaved(quint64 sessionId,
                                                      const QString& path) {
  if (sessionId != trajectorySessionId_) return;
  status_.lastTrajectoryImage = path;
  emit logMessage(CRAWLING_TEXT("直线段点云图已保存：%1").arg(path));
  publishStatus();
}

void LaserCorrectionController::trajectorySaveFailed(
    quint64 sessionId, const QString& error) {
  if (sessionId != trajectorySessionId_) return;
  emit logMessage(CRAWLING_TEXT("轨迹图保存失败：%1").arg(error));
}

bool LaserCorrectionController::telemetryUsable(
    const DriveTelemetry& telemetry) const {
  return telemetry.feedbackFresh && telemetry.left.valid &&
         telemetry.right.valid &&
         std::isfinite(telemetry.left.wheelPositionRad) &&
         std::isfinite(telemetry.right.wheelPositionRad) &&
         std::isfinite(telemetry.left.wheelSpeedMps) &&
         std::isfinite(telemetry.right.wheelSpeedMps);
}

bool LaserCorrectionController::phaseNeedsLaser() const {
  return phase_ == Phase::AwaitingInputs || phase_ == Phase::SurveyForward ||
         phase_ == Phase::TrackingForward;
}

bool LaserCorrectionController::phaseMoves() const {
  return phase_ == Phase::SurveyForward || phase_ == Phase::SurveyReturn ||
         phase_ == Phase::TrackingForward;
}

bool LaserCorrectionController::wheelsStopped() const {
  constexpr double kStoppedSpeedMps = 0.0005;
  return std::abs(leftSpeedMps_) <= kStoppedSpeedMps &&
         std::abs(rightSpeedMps_) <= kStoppedSpeedMps;
}

void LaserCorrectionController::tryStartSurvey() {
  if (!status_.active || phase_ != Phase::AwaitingInputs || !haveTelemetry_ ||
      !status_.gapValid || lastValidDetectionMs_ < enabledAtMs_ ||
      lastTelemetryMs_ < enabledAtMs_ || !wheelsStopped()) {
    return;
  }
  beginInitialSurvey();
}

void LaserCorrectionController::beginInitialSurvey() {
  samples_.clear();
  visualSamples_.clear();
  resetControllerError();
  segmentStartYawRad_ = yawRad_;
  surveyTravelM_ = 0.0;
  resetPhaseTravel();
  lastSampleLongitudinalM_ = -1.0;
  lastVisualLongitudinalM_ = -1.0;
  status_.segmentProgressM = 0.0;
  status_.collectedSamples = 0;
  setPhase(Phase::SurveyForward,
           CRAWLING_TEXT("首段直行采集双边缘点云"));
  appendCurrentEdgeSample();
}

void LaserCorrectionController::beginInitialSurveyRetry() {
  emit logMessage(CRAWLING_TEXT("首段拟合未通过，自动重新采集第 %1/%2 段")
                      .arg(initialSurveyRetryCount_)
                      .arg(kMaximumInitialSurveyRetries));
  beginInitialSurvey();
}

void LaserCorrectionController::beginTrackingSegment() {
  samples_.clear();
  visualSamples_.clear();
  segmentStartYawRad_ = yawRad_;
  resetPhaseTravel();
  angularLimitLogged_ = false;
  lastControlDiagnosticMs_ = -1;
  angularDirectionChangeCount_ = 0;
  lastAngularDirection_ = 0;
  steeringMismatchCount_ = 0;
  lastSampleLongitudinalM_ = -1.0;
  lastVisualLongitudinalM_ = -1.0;
  status_.segmentProgressM = 0.0;
  status_.collectedSamples = 0;
  setPhase(Phase::TrackingForward,
           CRAWLING_TEXT("柔性纠偏前进并采集下一段"));
}

void LaserCorrectionController::resetGapTracker() {
  gapTrackerValid_ = false;
  trackedGapHorizontal_ = true;
  trackedGapCenterRatio_ = 0.5;
  trackedGapWidthRatio_ = 0.0;
  trackedLineStartRatio_ = -1.0;
  trackedLineEndRatio_ = -1.0;
  trackedGapConfidence_ = 0.0;
  lastTrackedGapMs_ = -1;
}

void LaserCorrectionController::resetPhaseTravel() {
  phaseTravelM_ = 0.0;
  lastMotionProgressMs_ = clock_.elapsed();
}

void LaserCorrectionController::integrateWheelMotion(
    qint64 now, qint64 previousTelemetryMs, double previousLeftSpeedMps,
    double previousRightSpeedMps) {
  if (previousTelemetryMs < enabledAtMs_ || now <= previousTelemetryMs) return;

  const double deltaSeconds =
      clamp((now - previousTelemetryMs) / 1000.0, 0.0, 0.75);
  const double meanLeftMps =
      (previousLeftSpeedMps + leftSpeedMps_) * 0.5;
  const double meanRightMps =
      (previousRightSpeedMps + rightSpeedMps_) * 0.5;

  // The mixer defines positive yaw as the left wheel moving faster. Integrate
  // the same convention from fresh speed feedback so steering does not depend
  // on a controller-specific absolute encoder scale.
  yawRad_ = normalizedAngle(
      yawRad_ + (meanLeftMps - meanRightMps) /
                    settings_.trackWidthM * deltaSeconds);

  const double wheelTravel =
      (std::abs(previousLeftSpeedMps) + std::abs(leftSpeedMps_) +
       std::abs(previousRightSpeedMps) + std::abs(rightSpeedMps_)) *
      0.25 * deltaSeconds;
  if (wheelTravel <= 1e-8) return;
  if (phase_ == Phase::PauseBeforeReturn) {
    // Include the short stopping coast in the distance that must be driven
    // back; otherwise every cycle would restart slightly ahead of its origin.
    surveyTravelM_ += wheelTravel;
    return;
  }
  if (!phaseMoves()) return;
  phaseTravelM_ += wheelTravel;
  const double averageWheelSpeedMps = wheelTravel / deltaSeconds;
  const double meaningfulSpeedMps =
      std::max(0.0001, settings_.targetSpeedMps * 0.1);
  if (averageWheelSpeedMps >= meaningfulSpeedMps) {
    lastMotionProgressMs_ = now;
  }
}

bool LaserCorrectionController::stopIfMotionStalled(qint64 now) {
  const double commandedMotionThreshold =
      std::max(0.0001,
               settings_.targetSpeedMps * settings_.minSpeedScale * 0.5);
  if (!phaseMoves() || lastMotionProgressMs_ < 0 ||
      std::abs(currentLinearMps_) < commandedMotionThreshold ||
      now - lastMotionProgressMs_ <= settings_.motionStallTimeoutMs) {
    return false;
  }
  stop(CRAWLING_TEXT("轮速长时间无行驶进展，已停止自动纠偏"));
  return true;
}

void LaserCorrectionController::advanceFromTelemetry(qint64 now) {
  const double deltaSeconds = previousControlMs_ < 0
                                  ? 0.05
                                  : clamp((now - previousControlMs_) / 1000.0,
                                          0.001, 0.75);
  previousControlMs_ = now;

  if (stopIfMotionStalled(now)) return;

  switch (phase_) {
    case Phase::Idle:
    case Phase::AwaitingInputs:
      commandImmediateStop(status_.reason);
      return;

    case Phase::SurveyForward: {
      const double progress = phaseTravelM_;
      status_.segmentProgressM = clamp(progress, 0.0, settings_.segmentLengthM);
      if (!status_.gapValid) {
        commandImmediateStop(CRAWLING_TEXT("激光断口暂时无效，已暂停采集"));
        return;
      }
      if (progress >= settings_.segmentLengthM) {
        surveyTravelM_ = progress;
        commandImmediateStop(CRAWLING_TEXT("首段采集完成，等待车体停稳"));
        setPhase(Phase::PauseBeforeReturn,
                 CRAWLING_TEXT("首段采集完成，等待车体停稳"));
        return;
      }
      applyCommand(settings_.targetSpeedMps, 0.0, deltaSeconds);
      status_.reason = runningReason();
      return;
    }

    case Phase::PauseBeforeReturn: {
      commandImmediateStop(status_.reason);
      const qint64 elapsed = now - phaseStartedMs_;
      if (elapsed >= settings_.motionStallTimeoutMs && !wheelsStopped()) {
        stop(CRAWLING_TEXT("首段采集后车轮未能停稳，已停止自动纠偏"));
        return;
      }
      if (elapsed >= settings_.settleTimeMs && wheelsStopped()) {
        resetPhaseTravel();
        setPhase(Phase::SurveyReturn,
                 CRAWLING_TEXT("原路直行返回采集起点"));
      }
      return;
    }

    case Phase::SurveyReturn: {
      status_.segmentProgressM =
          clamp(phaseTravelM_, 0.0, surveyTravelM_);
      if (phaseTravelM_ + settings_.returnToleranceM >= surveyTravelM_) {
        commandImmediateStop(CRAWLING_TEXT("已返回采集起点，正在拟合路径"));
        QString fitError;
        if (!fitCollectedPath(&fitError)) {
          if (initialSurveyRetryCount_ < kMaximumInitialSurveyRetries) {
            ++initialSurveyRetryCount_;
            commandImmediateStop(CRAWLING_TEXT("首段拟合暂不稳定，准备重新采集"));
            setPhase(Phase::PauseBeforeSurveyRetry,
                     CRAWLING_TEXT("首段拟合未通过，等待停稳后重新采集"));
          } else {
            // Do not deadlock the vehicle on a noisy startup segment. Start
            // conservatively along the current heading and let later segments
            // replace this fallback as soon as a valid fit is available.
            initialSurveyRetryCount_ = 0;
            desiredHeadingRad_ = yawRad_;
            emit logMessage(CRAWLING_TEXT(
                "首段连续拟合不稳定，沿当前航向继续前进，等待后续有效数据"));
            setPhase(Phase::PauseBeforeTracking,
                     CRAWLING_TEXT("首段未稳定拟合，沿当前航向等待后续纠偏"));
          }
          return;
        }
        initialSurveyRetryCount_ = 0;
        setPhase(Phase::PauseBeforeTracking,
                 CRAWLING_TEXT("路径拟合完成，等待车体停稳"));
        return;
      }
      applyCommand(-settings_.targetSpeedMps, 0.0, deltaSeconds);
      status_.reason = runningReason();
      return;
    }

    case Phase::PauseBeforeSurveyRetry: {
      commandImmediateStop(status_.reason);
      const qint64 elapsed = now - phaseStartedMs_;
      const bool freshImage = lastImageMs_ >= phaseStartedMs_ &&
                              now - lastImageMs_ <= settings_.imageTimeoutMs;
      const bool freshGap = status_.gapValid &&
                            lastValidDetectionMs_ >= phaseStartedMs_ &&
                            now - lastValidDetectionMs_ <=
                                settings_.detectionTimeoutMs;
      if (elapsed >= settings_.motionStallTimeoutMs && !wheelsStopped()) {
        stop(CRAWLING_TEXT("首段重采集前车轮未能停稳，已停止自动纠偏"));
      } else if (elapsed >= settings_.settleTimeMs && wheelsStopped() &&
                 freshImage && freshGap) {
        beginInitialSurveyRetry();
      }
      return;
    }

    case Phase::PauseBeforeTracking: {
      commandImmediateStop(status_.reason);
      const qint64 elapsed = now - phaseStartedMs_;
      if (elapsed >= settings_.motionStallTimeoutMs && !wheelsStopped()) {
        stop(CRAWLING_TEXT("返回起点后车轮未能停稳，已停止自动纠偏"));
        return;
      }
      const bool freshImage = lastImageMs_ >= phaseStartedMs_ &&
                              now - lastImageMs_ <= settings_.imageTimeoutMs;
      const bool freshGap = status_.gapValid &&
                            lastValidDetectionMs_ >= phaseStartedMs_ &&
                            now - lastValidDetectionMs_ <=
                                settings_.detectionTimeoutMs;
      if (elapsed >= settings_.settleTimeMs && freshImage && freshGap &&
          wheelsStopped()) {
        beginTrackingSegment();
      } else if (elapsed >= settings_.settleTimeMs &&
                 (!freshImage || !freshGap)) {
        status_.reason = CRAWLING_TEXT("路径拟合完成，等待新鲜有效的激光断口图像");
      }
      return;
    }

    case Phase::TrackingForward: {
      const double progress = phaseTravelM_;
      status_.segmentProgressM = clamp(progress, 0.0, settings_.segmentLengthM);
      if (!status_.gapValid) {
        commandImmediateStop(CRAWLING_TEXT("激光断口暂时无效，已暂停跟踪"));
        return;
      }
      if (progress >= settings_.segmentLengthM) {
        QString fitError;
        if (!fitCollectedPath(&fitError)) {
          ++trackingFitFailureCount_;
          emit logMessage(
              CRAWLING_TEXT("本段拟合未通过，保持上一段纠偏方向继续前进（连续失败 %1 次）")
                  .arg(trackingFitFailureCount_));
        } else {
          trackingFitFailureCount_ = 0;
          ++status_.cycleCount;
        }
        beginTrackingSegment();
      }

      const double headingError =
          normalizedAngle(desiredHeadingRad_ - yawRad_);
      const double measuredYawRateRadps =
          (leftSpeedMps_ - rightSpeedMps_) / settings_.trackWidthM;
      filteredYawRateRadps_ +=
          settings_.derivativeAlpha *
          (measuredYawRateRadps - filteredYawRateRadps_);
      status_.headingErrorRad = headingError;

      const double controlledError = headingError;
      const double speedScale = clamp(
          1.0 - settings_.speedReductionGain *
                    clamp(std::abs(headingError) /
                              kSoftSpeedReductionAngleRad,
                          0.0, 1.0),
          settings_.minSpeedScale, 1.0);
      const double targetLinear = settings_.targetSpeedMps * speedScale;
      const double proportionalTerm =
          settings_.proportionalGain * controlledError;
      // Wheel-speed yaw rate is considerably less noisy than differentiating
      // a heading estimate delivered at about 3 Hz. Opposing it provides
      // damping as the chassis approaches the desired heading.
      const double derivativeTerm =
          -settings_.derivativeGain * filteredYawRateRadps_;
      const double rawTargetAngular = proportionalTerm + derivativeTerm;
      const double sameDirectionAngularLimit =
          (2.0 * std::abs(targetLinear) / settings_.trackWidthM) *
          ((1.0 - settings_.minimumInnerWheelRatio) /
           (1.0 + settings_.minimumInnerWheelRatio));
      const double safeAngularLimit = std::min(
          {settings_.maxAngularRadps,
           std::max(1e-6, sameDirectionAngularLimit),
           std::max(1e-6, std::abs(targetLinear) *
                             settings_.maximumCurvatureRadPerM)});
      if (!angularLimitLogged_ &&
          std::abs(rawTargetAngular) > safeAngularLimit + 1e-9) {
        emit logMessage(
            CRAWLING_TEXT("柔性转向限幅：原始角速度 %1 deg/s，安全上限 %2 deg/s，"
                          "前进速度 %3 mm/s，曲率上限 %4 deg/m")
                .arg(rawTargetAngular * 180.0 / kPi, 0, 'f', 1)
                .arg(safeAngularLimit * 180.0 / kPi, 0, 'f', 1)
                .arg(targetLinear * 1000.0, 0, 'f', 1)
                .arg(settings_.maximumCurvatureRadPerM * 180.0 / kPi, 0, 'f', 1));
        angularLimitLogged_ = true;
      }
      const double targetAngular = clamp(rawTargetAngular, -safeAngularLimit,
                                         safeAngularLimit);
      applyCommand(targetLinear, targetAngular, deltaSeconds);
      const int angularDirection = currentAngularRadps_ > 0.003
                                       ? 1
                                       : (currentAngularRadps_ < -0.003 ? -1 : 0);
      if (angularDirection != 0 && lastAngularDirection_ != 0 &&
          angularDirection != lastAngularDirection_) {
        ++angularDirectionChangeCount_;
      }
      if (angularDirection != 0) lastAngularDirection_ = angularDirection;

      const bool steeringDirectionMismatch =
          std::abs(currentAngularRadps_) > 0.01 &&
          std::abs(measuredYawRateRadps) > 0.01 &&
          currentAngularRadps_ * measuredYawRateRadps < 0.0;
      steeringMismatchCount_ = steeringDirectionMismatch
                                   ? steeringMismatchCount_ + 1
                                   : 0;
      if (steeringMismatchCount_ == 3) {
        emit logMessage(CRAWLING_TEXT(
            "转向响应方向连续不一致：命令 %1 deg/s，轮速估算 %2 deg/s；"
            "请检查轮速反馈符号或机械响应延迟")
                            .arg(currentAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                            .arg(measuredYawRateRadps * 180.0 / kPi, 0, 'f', 2));
      }
      if (lastControlDiagnosticMs_ < 0 ||
          now - lastControlDiagnosticMs_ >= kControlDiagnosticIntervalMs) {
        lastControlDiagnosticMs_ = now;
        emit logMessage(
            CRAWLING_TEXT("纠偏控制 #%1：周期 %2，进度 %3 mm，期望航向 %4°，"
                          "估算航向 %5°，误差 %6°，轮速角速度 %7 deg/s，"
                          "P %8，D %9，原始角速度 %10，限幅 %11，输出 %12 deg/s，"
                          "线速度 %13 mm/s，底盘应用 %14 mm/s/%15 deg/s，"
                          "轮速目标 %16/%17 mm/s，反馈 %18/%19 mm/s，换向 %20 次")
                .arg(++controlDiagnosticSequence_)
                .arg(status_.cycleCount)
                .arg(status_.segmentProgressM * 1000.0, 0, 'f', 1)
                .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
                .arg(yawRad_ * 180.0 / kPi, 0, 'f', 2)
                .arg(headingError * 180.0 / kPi, 0, 'f', 2)
                .arg(measuredYawRateRadps * 180.0 / kPi, 0, 'f', 2)
                .arg(proportionalTerm, 0, 'f', 4)
                .arg(derivativeTerm, 0, 'f', 4)
                .arg(rawTargetAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(safeAngularLimit * 180.0 / kPi, 0, 'f', 2)
                .arg(currentAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                .arg(currentLinearMps_ * 1000.0, 0, 'f', 1)
                .arg(driveAppliedLinearMps_ * 1000.0, 0, 'f', 1)
                .arg(driveAppliedAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                .arg(leftTargetMps_ * 1000.0, 0, 'f', 1)
                .arg(rightTargetMps_ * 1000.0, 0, 'f', 1)
                .arg(leftSpeedMps_ * 1000.0, 0, 'f', 1)
                .arg(rightSpeedMps_ * 1000.0, 0, 'f', 1)
                .arg(angularDirectionChangeCount_));
      }
      status_.reason = runningReason();
      return;
    }
  }
}

void LaserCorrectionController::appendCurrentEdgeSample() {
  if (!haveTelemetry_ || !status_.gapValid ||
      (phase_ != Phase::SurveyForward &&
       phase_ != Phase::TrackingForward)) {
    return;
  }
  double sampledTravelM = phaseTravelM_;
  const qint64 now = clock_.elapsed();
  if (lastTelemetryMs_ >= enabledAtMs_ && now > lastTelemetryMs_ &&
      now - lastTelemetryMs_ <= settings_.telemetryTimeoutMs) {
    // Camera frames arrive faster than the wheel telemetry. Interpolate their
    // longitudinal coordinate from the freshest wheel speed so a 20 mm segment
    // still contains enough distinct points for a direction estimate.
    const double extrapolationSeconds =
        clamp((now - lastTelemetryMs_) / 1000.0, 0.0, 0.5);
    const double forwardSpeedMps =
        (std::abs(leftSpeedMps_) + std::abs(rightSpeedMps_)) * 0.5;
    sampledTravelM += forwardSpeedMps * extrapolationSeconds;
  }
  const double longitudinal =
      clamp(sampledTravelM, 0.0, settings_.segmentLengthM);
  LaserEdgeSample sample;
  sample.longitudinalM = std::max(0.0, longitudinal);
  sample.leftLateralM = latestLeftEdgeM_;
  sample.rightLateralM = latestRightEdgeM_;
  if (visualSamples_.isEmpty() ||
      sample.longitudinalM - lastVisualLongitudinalM_ >= 0.00001) {
    visualSamples_.append(sample);
    lastVisualLongitudinalM_ = sample.longitudinalM;
  }
  if (lastSampleLongitudinalM_ >= 0.0 &&
      longitudinal - lastSampleLongitudinalM_ < settings_.sampleSpacingM) {
    return;
  }
  samples_.append(sample);
  lastSampleLongitudinalM_ = sample.longitudinalM;
  status_.collectedSamples = samples_.size();
}

bool LaserCorrectionController::fitCollectedPath(QString* error) {
  const LaserPathFit fit = LaserPathEstimator::fit(
      samples_, settings_.minimumFitSamples,
      settings_.segmentLengthM * settings_.minimumFitSpanRatio,
      settings_.maximumFitRmsErrorM);
  if (!fit.valid) {
    queueTrajectoryImage(fit, CRAWLING_TEXT("拟合失败"));
    const QString failureReason = [&fit]() {
      switch (fit.failureCode) {
        case 1:
          return CRAWLING_TEXT("样本总数不足");
        case 2:
          return CRAWLING_TEXT("有效边缘样本不足");
        case 3:
          return CRAWLING_TEXT("纵向跨度不足");
        case 4:
          return CRAWLING_TEXT("边缘点退化");
        case 5:
          return CRAWLING_TEXT("跳变异常点过多");
        case 6:
          return CRAWLING_TEXT("RMS 误差超限");
        default:
          return CRAWLING_TEXT("未知拟合条件失败");
      }
    }();
    emit logMessage(
        CRAWLING_TEXT("拟合诊断：%1，有效样本 %2，异常剔除后 %3，跨度 %4 mm，RMS %5 mm")
            .arg(failureReason)
            .arg(fit.validSampleCount)
            .arg(fit.inlierCount)
            .arg(fit.observedSpanM * 1000.0, 0, 'f', 1)
            .arg(fit.observedRmsErrorM * 1000.0, 0, 'f', 1));
    if (error) {
      *error = CRAWLING_TEXT("双边缘点云拟合失败：%1（样本 %2），本段暂不更新纠偏方向")
                   .arg(failureReason)
                   .arg(samples_.size());
    }
    return false;
  }
  // Convert the camera lateral direction into the odometry yaw convention.
  const double measuredPathAngleRad = kInternalSteeringSign * fit.angleRad;
  const double measuredHeadingRad =
      normalizedAngle(segmentStartYawRad_ + measuredPathAngleRad);
  const double targetUpdateRad =
      normalizedAngle(measuredHeadingRad - desiredHeadingRad_);
  // Every valid deviation remains actionable, but a noisy segment cannot
  // replace the target heading in one step. Repeated segments continuously
  // move the target in the measured direction.
  desiredHeadingRad_ = normalizedAngle(
      desiredHeadingRad_ + settings_.headingFitBlend * targetUpdateRad);
  status_.fittedAngleRad = measuredPathAngleRad;
  status_.fitRmsErrorM = fit.rmsErrorM;
  status_.collectedSamples = fit.sampleCount;
  if (status_.cycleCount == 0) status_.cycleCount = 1;
  queueTrajectoryImage(
      fit, phase_ == Phase::SurveyReturn ? CRAWLING_TEXT("首段探测")
                                         : CRAWLING_TEXT("纠偏跟踪"));
  emit logMessage(
      CRAWLING_TEXT("双边缘拟合完成：角度 %1°，融合后目标航向 %2°，"
                    "RMS %3 mm，跨度 %4 mm，样本 %5")
          .arg(measuredPathAngleRad * 180.0 / kPi, 0, 'f', 2)
          .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
          .arg(fit.rmsErrorM * 1000.0, 0, 'f', 2)
          .arg(fit.spanM * 1000.0, 0, 'f', 1)
          .arg(fit.sampleCount));
  const double leftAngleDeg = std::atan(fit.leftSlope) * 180.0 / kPi;
  const double rightAngleDeg = std::atan(fit.rightSlope) * 180.0 / kPi;
  const double startWidthM = fit.rightInterceptM - fit.leftInterceptM;
  const double endWidthM =
      startWidthM + (fit.rightSlope - fit.leftSlope) * fit.spanM;
  emit logMessage(
      CRAWLING_TEXT("拟合详情：周期 %1，阶段 %2，左角 %3°，右角 %4°，"
                    "左/右斜率 %5/%6，中心截距 %7 mm，"
                    "断口宽度起点/终点 %8/%9 mm，原始/有效样本 %10/%11")
          .arg(status_.cycleCount)
          .arg(phaseName(phase_))
          .arg(leftAngleDeg, 0, 'f', 2)
          .arg(rightAngleDeg, 0, 'f', 2)
          .arg(fit.leftSlope, 0, 'f', 5)
          .arg(fit.rightSlope, 0, 'f', 5)
          .arg(fit.centerInterceptM * 1000.0, 0, 'f', 2)
          .arg(startWidthM * 1000.0, 0, 'f', 2)
          .arg(endWidthM * 1000.0, 0, 'f', 2)
          .arg(samples_.size())
          .arg(fit.sampleCount));
  return true;
}

void LaserCorrectionController::beginTrajectorySession() {
  ++trajectorySessionId_;
  status_.trajectoryDirectory.clear();
  status_.lastTrajectoryImage.clear();
  emit trajectorySessionRequested(trajectorySessionId_);
}

void LaserCorrectionController::queueTrajectoryImage(
    const LaserPathFit& fit, const QString& label) {
  if (visualSamples_.isEmpty()) {
    emit logMessage(CRAWLING_TEXT("轨迹图保存失败：本段没有连续激光样本"));
    return;
  }
  emit trajectorySegmentReady(trajectorySessionId_, visualSamples_, fit,
                              settings_.segmentLengthM,
                              settings_.imageLateralSpanM, label);
}

void LaserCorrectionController::watchdogTick() {
  if (!status_.active) return;
  const qint64 now = clock_.elapsed();

  if (phaseNeedsLaser() &&
      now - enabledAtMs_ > settings_.imageTimeoutMs &&
      (lastImageMs_ < enabledAtMs_ ||
       now - lastImageMs_ > settings_.imageTimeoutMs)) {
    stop(CRAWLING_TEXT("激光原始图像超时，已停止自动纠偏"));
    return;
  }
  if (now - enabledAtMs_ > settings_.telemetryTimeoutMs &&
      (lastTelemetryMs_ < enabledAtMs_ ||
       now - lastTelemetryMs_ > settings_.telemetryTimeoutMs)) {
    stop(CRAWLING_TEXT("轮端里程反馈超时，已停止自动纠偏"));
    return;
  }
  const qint64 validReference = lastValidDetectionMs_ >= enabledAtMs_
                                    ? lastValidDetectionMs_
                                    : enabledAtMs_;
  if (phaseNeedsLaser() &&
      now - validReference > settings_.detectionTimeoutMs) {
    stop(CRAWLING_TEXT("持续未检测到有效激光断口，已停止自动纠偏"));
    return;
  }

  if (lastCommandMs_ < 0 || now - lastCommandMs_ >= 100) {
    emit commandChanged(currentLinearMps_, currentAngularRadps_);
    lastCommandMs_ = now;
  }
}

void LaserCorrectionController::resetControllerError() {
  filteredYawRateRadps_ = 0.0;
  status_.headingErrorRad = 0.0;
}

void LaserCorrectionController::resetSession() {
  phase_ = Phase::Idle;
  enabledAtMs_ = -1;
  phaseStartedMs_ = -1;
  lastImageMs_ = -1;
  lastValidDetectionMs_ = -1;
  lastTelemetryMs_ = -1;
  previousControlMs_ = -1;
  lastCommandMs_ = -1;
  lastControlDiagnosticMs_ = -1;
  lastDetectionDiagnosticMs_ = -1;
  haveTelemetry_ = false;
  yawRad_ = 0.0;
  leftSpeedMps_ = 0.0;
  rightSpeedMps_ = 0.0;
  driveAppliedLinearMps_ = 0.0;
  driveAppliedAngularRadps_ = 0.0;
  leftTargetMps_ = 0.0;
  rightTargetMps_ = 0.0;
  phaseTravelM_ = 0.0;
  surveyTravelM_ = 0.0;
  segmentStartYawRad_ = 0.0;
  lastMotionProgressMs_ = -1;
  lastSampleLongitudinalM_ = -1.0;
  desiredHeadingRad_ = 0.0;
  currentLinearMps_ = 0.0;
  currentAngularRadps_ = 0.0;
  angularLimitLogged_ = false;
  initialSurveyRetryCount_ = 0;
  trackingFitFailureCount_ = 0;
  controlDiagnosticSequence_ = 0;
  invalidDetectionCount_ = 0;
  reusedDetectionCount_ = 0;
  angularDirectionChangeCount_ = 0;
  lastAngularDirection_ = 0;
  steeringMismatchCount_ = 0;
  samples_.clear();
  visualSamples_.clear();
  lastVisualLongitudinalM_ = -1.0;
  resetGapTracker();
  resetControllerError();
  status_ = {};
  status_.phase = phaseName(phase_);
}

void LaserCorrectionController::setPhase(Phase phase, const QString& reason,
                                         bool writeLog) {
  const Phase previousPhase = phase_;
  phase_ = phase;
  phaseStartedMs_ = clock_.elapsed();
  status_.phase = phaseName(phase_);
  status_.reason = reason;
  if (writeLog) {
    emit logMessage(reason);
    emit logMessage(
        CRAWLING_TEXT("纠偏阶段：%1 -> %2，进度 %3 mm，估算航向 %4°，"
                      "期望航向 %5°，样本 %6")
            .arg(phaseName(previousPhase))
            .arg(phaseName(phase_))
            .arg(phaseTravelM_ * 1000.0, 0, 'f', 1)
            .arg(yawRad_ * 180.0 / kPi, 0, 'f', 2)
            .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
            .arg(samples_.size()));
  }
}

QString LaserCorrectionController::phaseName(Phase phase) const {
  switch (phase) {
    case Phase::Idle:
      return CRAWLING_TEXT("未启动");
    case Phase::AwaitingInputs:
      return CRAWLING_TEXT("等待输入");
    case Phase::SurveyForward:
      return CRAWLING_TEXT("首段采集");
    case Phase::PauseBeforeReturn:
      return CRAWLING_TEXT("返回前停稳");
    case Phase::SurveyReturn:
      return CRAWLING_TEXT("原路返回");
    case Phase::PauseBeforeSurveyRetry:
      return CRAWLING_TEXT("重采集前停稳");
    case Phase::PauseBeforeTracking:
      return CRAWLING_TEXT("跟踪前停稳");
    case Phase::TrackingForward:
      return CRAWLING_TEXT("分段跟踪");
  }
  return CRAWLING_TEXT("未知");
}

QString LaserCorrectionController::runningReason() const {
  switch (phase_) {
    case Phase::Idle:
      return CRAWLING_TEXT("自动纠偏未启动");
    case Phase::AwaitingInputs:
      return CRAWLING_TEXT("等待新图像和轮端里程");
    case Phase::SurveyForward:
      return CRAWLING_TEXT("首段直行采集双边缘点云");
    case Phase::PauseBeforeReturn:
      return CRAWLING_TEXT("首段采集完成，等待车体停稳");
    case Phase::SurveyReturn:
      return CRAWLING_TEXT("原路直行返回采集起点");
    case Phase::PauseBeforeSurveyRetry:
      return CRAWLING_TEXT("首段拟合未通过，等待停稳后重新采集");
    case Phase::PauseBeforeTracking:
      return CRAWLING_TEXT("路径拟合完成，等待车体停稳");
    case Phase::TrackingForward:
      return CRAWLING_TEXT("柔性纠偏前进并采集下一段");
  }
  return {};
}

void LaserCorrectionController::applyCommand(double targetLinearMps,
                                              double targetAngularRadps,
                                              double deltaSeconds) {
  currentLinearMps_ = rate(
      currentLinearMps_, targetLinearMps,
      std::max(0.005, settings_.maxLinearAccelerationMps2) * deltaSeconds);
  const double acceleratedAngularRadps = rate(
      currentAngularRadps_, targetAngularRadps,
      std::max(0.01, settings_.maxAngularAccelerationRadps2) * deltaSeconds);
  const double currentLinearAngularLimit =
      (2.0 * std::abs(currentLinearMps_) / settings_.trackWidthM) *
      ((1.0 - settings_.minimumInnerWheelRatio) /
       (1.0 + settings_.minimumInnerWheelRatio));
  // Linear and angular commands accelerate at different rates. Re-apply both
  // steering limits using the current ramped linear speed so startup cannot
  // briefly turn much harder than the steady-state command.
  const double currentCurvatureAngularLimit =
      std::abs(currentLinearMps_) * settings_.maximumCurvatureRadPerM;
  const double currentSafeAngularLimit =
      std::min(currentLinearAngularLimit, currentCurvatureAngularLimit);
  currentAngularRadps_ = clamp(acceleratedAngularRadps,
                               -currentSafeAngularLimit,
                               currentSafeAngularLimit);
  status_.linearCommandMps = currentLinearMps_;
  status_.angularCommandRadps = currentAngularRadps_;
  emit commandChanged(currentLinearMps_, currentAngularRadps_);
  lastCommandMs_ = clock_.elapsed();
}

void LaserCorrectionController::commandImmediateStop(const QString& reason) {
  currentLinearMps_ = 0.0;
  currentAngularRadps_ = 0.0;
  status_.linearCommandMps = 0.0;
  status_.angularCommandRadps = 0.0;
  if (!reason.isEmpty()) status_.reason = reason;
  emit commandChanged(0.0, 0.0);
  lastCommandMs_ = clock_.elapsed();
  publishStatus();
}

void LaserCorrectionController::stop(const QString& reason) {
  const bool wasActive = status_.active;
  const Phase stoppedPhase = phase_;
  const double stoppedHeadingErrorRad = status_.headingErrorRad;
  const double stoppedLinearMps = status_.linearCommandMps;
  const double stoppedAngularRadps = status_.angularCommandRadps;
  currentLinearMps_ = 0.0;
  currentAngularRadps_ = 0.0;
  resetControllerError();
  status_.active = false;
  status_.linearCommandMps = 0.0;
  status_.angularCommandRadps = 0.0;
  phase_ = Phase::Idle;
  status_.phase = phaseName(phase_);
  status_.reason = reason;
  emit commandChanged(0.0, 0.0);
  lastCommandMs_ = clock_.elapsed();
  if (wasActive || reason != CRAWLING_TEXT("自动纠偏已停止")) {
    emit logMessage(reason);
  }
  if (wasActive) {
    emit logMessage(
        CRAWLING_TEXT("纠偏停止快照：阶段 %1，周期 %2，进度 %3 mm，"
                      "估算/期望航向 %4°/%5°，误差 %6°，"
                      "命令 %7 mm/s/%8 deg/s，轮速反馈 %9/%10 mm/s")
            .arg(phaseName(stoppedPhase))
            .arg(status_.cycleCount)
            .arg(status_.segmentProgressM * 1000.0, 0, 'f', 1)
            .arg(yawRad_ * 180.0 / kPi, 0, 'f', 2)
            .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
            .arg(stoppedHeadingErrorRad * 180.0 / kPi, 0, 'f', 2)
            .arg(stoppedLinearMps * 1000.0, 0, 'f', 1)
            .arg(stoppedAngularRadps * 180.0 / kPi, 0, 'f', 2)
            .arg(leftSpeedMps_ * 1000.0, 0, 'f', 1)
            .arg(rightSpeedMps_ * 1000.0, 0, 'f', 1));
  }
  publishStatus();
}

void LaserCorrectionController::publishStatus() {
  emit statusChanged(status_);
}

}  // namespace crawling
