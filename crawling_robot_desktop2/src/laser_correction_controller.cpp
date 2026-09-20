#include "laser_correction_controller.h"
#include "utf8_compat.h"

#include <QStringList>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kPi = 3.14159265358979323846;
// Nominal image scale and installed coordinate convention. Wheel command
// signs alone cannot establish camera mounting polarity or physical scale.
constexpr double kInternalImageLateralSpanM = 0.20;
// DifferentialMixer defines positive angular velocity as a right turn (left
// wheel faster). Keep the installed image-to-drive convention identical in
// the live loop, rolling cloud, boundary recovery and saved trajectory.
constexpr double kCameraLateralToVehicleSign = 1.0;
constexpr int kInitialGapConfirmationFrames = 3;
constexpr int kContourIdentityConfirmationFrames = 5;
constexpr double kInitialGapCenterToleranceRatio = 0.02;
constexpr double kInitialGapWidthToleranceRatio = 0.02;
constexpr qint64 kControlDiagnosticIntervalMs = 500;
constexpr qint64 kDetectionDiagnosticIntervalMs = 1000;
constexpr qint64 kRawFrameDiagnosticIntervalMs = 100;
constexpr int kRawProfileLogBuckets = 128;
constexpr double kCenterFilterTimeConstantS = 0.20;
// The weld can move in the image only gradually while the chassis is moving.
// Limit the filtered physical feedback so one bad frame cannot command a large
// correction, while a persistent error is still followed continuously.
constexpr double kMaximumCenterSlewMps = 0.05;
constexpr double kMinimumHeadingStepRad = 0.35 * kPi / 180.0;
constexpr double kMaximumHeadingStepRad = 2.5 * kPi / 180.0;
constexpr int kFitReversalConfirmationSegments = 2;
// A normal two-sided gap is usually around 0.10 confidence. Edge-break and
// reused detections are deliberately allowed to contribute, but only in
// proportion to their confidence so they cannot overturn the rolling fit.
constexpr double kFullCenterFeedbackConfidence = 0.10;
constexpr double kMinimumCenterFeedbackWeight = 0.03;
constexpr double kHeldCenterFeedbackWeight = 0.12;
constexpr double kMaximumCenterCorrectionRad = 4.0 * kPi / 180.0;
constexpr double kGuidanceHeldWeight = 0.25;
constexpr double kGuidanceLowConfidenceWeight = 0.12;
// Inferred one-edge centers are capped below 0.05; keep them out of the
// geometric point cloud even though the live center loop can still use them.
constexpr double kMinimumGeometrySampleConfidence = 0.08;
constexpr double kAngularBrakeAccelerationMultiplier = 2.5;
constexpr double kMaximumGuidanceEdgeAngleDifferenceRad = 8.0 * kPi / 180.0;
constexpr double kMaximumGuidanceWidthDriftM = 0.012;
// Boundary containment has priority over soft curvature and blending.
// Cruise remains constant while a measured seam can still be steered inward.
constexpr double kBoundaryRecoveryCurvatureRadPerM = 3.20;
constexpr double kMinimumContainmentAngularRadps = 0.04;
constexpr double kScanContainmentMarginRatio = 0.25;
constexpr double kScanContainmentReleaseRatio = 0.29;
constexpr double kScanStopMarginRatio = 0.04;
constexpr qint64 kBoundaryObservationGraceMs = 500;
constexpr qint64 kBoundaryResponseGraceMs = 700;
constexpr qint64 kMaximumUnmeasuredSeamMs = 1000;
constexpr double kLaserCenterDeadbandM = 0.0015;
constexpr double kLaserCenterTurnErrorM = 0.012;
constexpr double kLaserCenterAngularLimitRadps = 0.16;
constexpr double kLargeOffsetCurvatureRadPerM = 2.40;
constexpr double kCenterRecoveryWatchdogErrorM = 0.015;
constexpr double kCenterRecoveryWatchdogTravelM = 0.060;
constexpr double kCenterRecoveryMinimumImprovementM = 0.001;
constexpr double kCenterResponseWindowYawRad = 6.0 * kPi / 180.0;
constexpr double kCenterDivergingRateMps = 0.002;
constexpr double kCenterConvergingRateMps = -0.002;
constexpr double kCenterTrendFilterTimeConstantS = 0.30;
// Do not reverse the differential pair for sub-noise heading corrections.
// The live image loop still has full authority whenever the gap is displaced
// or moving toward a scan boundary.
constexpr double kSteeringDeadbandRadps = 0.004;
// The raw-image center is the fast safety loop. While the weld is visibly
// off-center, a stale point-cloud heading must not cancel the turn that keeps
// the gap inside the scanner. Authority is handed back smoothly near center.
constexpr double kCenterPriorityStartM = 0.003;
constexpr double kCenterPriorityFullM = 0.010;
constexpr double kMaximumOpposingHeadingFraction = 0.35;
constexpr double kCenterReversalHysteresisM = 0.0030;
constexpr double kCenterReversalReleaseM = 0.0050;
constexpr int kCenterReversalConfirmationFrames = 2;

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

QString encodeQuantizedProfile(const QVector<int>& profile) {
  if (profile.isEmpty()) return QStringLiteral("-");
  static constexpr char kHex[] = "0123456789abcdef";
  const int bucketCount = std::min(kRawProfileLogBuckets, profile.size());
  QString encoded;
  encoded.reserve(bucketCount);
  for (int bucket = 0; bucket < bucketCount; ++bucket) {
    const int start = bucket * profile.size() / bucketCount;
    const int end = std::max(start + 1,
                             (bucket + 1) * profile.size() / bucketCount);
    int sum = 0;
    for (int index = start; index < end; ++index) sum += profile[index];
    const int average = sum / std::max(1, end - start);
    const int quantized = std::max(0, std::min(15, (average + 8) / 16));
    encoded.append(QLatin1Char(kHex[quantized]));
  }
  return encoded;
}

QString encodePresentRuns(const QVector<bool>& present, int sampleStepPx,
                          int axisLengthPx) {
  if (present.isEmpty()) return QStringLiteral("-");
  QStringList runs;
  int index = 0;
  while (index < present.size()) {
    if (!present[index]) {
      ++index;
      continue;
    }
    const int start = index;
    while (index < present.size() && present[index]) ++index;
    const int end = index - 1;
    const int startPx = start * sampleStepPx;
    const int endPx = std::min(axisLengthPx - 1,
                               (end + 1) * sampleStepPx - 1);
    runs.append(QStringLiteral("%1-%2").arg(startPx).arg(endPx));
  }
  return runs.isEmpty() ? QStringLiteral("-") : runs.join(QLatin1Char(','));
}

}  // namespace

LaserCorrectionController::LaserCorrectionController(QObject* parent)
    : QObject(parent) {
  clock_.start();
  // Move the timer with its controller when the application assigns the
  // correction worker thread. Start it only from that thread on enable.
  watchdog_.setParent(this);
  watchdog_.setInterval(20);
  watchdog_.setTimerType(Qt::PreciseTimer);
  connect(&watchdog_, &QTimer::timeout, this,
          &LaserCorrectionController::watchdogTick);
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
  settings_.steeringSign =
      static_cast<int>(kCameraLateralToVehicleSign);
  settings_.targetSpeedMps = std::max(0.001, settings_.targetSpeedMps);
  settings_.proportionalGain = clamp(settings_.proportionalGain, 0.0, 10.0);
  settings_.derivativeGain = clamp(settings_.derivativeGain, 0.0, 2.0);
  settings_.derivativeAlpha = clamp(settings_.derivativeAlpha, 0.02, 1.0);
  settings_.headingFitBlend = clamp(settings_.headingFitBlend, 0.05, 0.50);
  settings_.maximumCurvatureRadPerM =
      clamp(settings_.maximumCurvatureRadPerM, 0.05, 1.0);
  settings_.laserCenterFeedbackGain =
      clamp(settings_.laserCenterFeedbackGain, 0.0, 1.5);
  settings_.laserCenterAngularGain =
      clamp(settings_.laserCenterAngularGain, 0.2, 4.0);
  settings_.laserCenterLookaheadM =
      clamp(settings_.laserCenterLookaheadM, 0.05, 1.0);
  settings_.minimumInnerWheelRatio =
      clamp(settings_.minimumInnerWheelRatio, 0.45, 0.90);
  settings_.maxAngularRadps = std::max(0.01, settings_.maxAngularRadps);
  settings_.detectionTimeoutMs = std::max(200, settings_.detectionTimeoutMs);
  settings_.transientDetectionHoldMs =
      std::clamp(settings_.transientDetectionHoldMs, 100,
                 std::max(settings_.detectionTimeoutMs,
                          settings_.detectionRecoveryTimeoutMs));
  settings_.detectionRecoveryTimeoutMs = std::clamp(
      settings_.detectionRecoveryTimeoutMs,
      std::max(settings_.detectionTimeoutMs, 1000), 5000);
  settings_.motionStallTimeoutMs =
      std::max(1000, settings_.motionStallTimeoutMs);
}

void LaserCorrectionController::setEnabled(bool enabled) {
  if (!enabled) {
    stop(CRAWLING_TEXT("自动纠偏已停止"));
    return;
  }

  resetSession();
  watchdog_.start();
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
  emit logMessage(
      CRAWLING_TEXT("激光检测恢复：普通沿用 %1 ms，候选跳变恢复 %2 ms，"
                    "原始图像超时 %3 ms")
          .arg(settings_.transientDetectionHoldMs)
          .arg(settings_.detectionRecoveryTimeoutMs)
          .arg(settings_.imageTimeoutMs));
  emit logMessage(
      CRAWLING_TEXT("原始图主缺口：填充短缺口比例 %1%，主候选至少为最大缺口 %2%；"
                    "实时回中在偏差 %3 mm 开始接管、%4 mm 完全接管，"
                    "反向慢环最多保留 %5%")
          .arg(settings_.detector.smallGapFillRatio * 100.0, 0, 'f', 2)
          .arg(settings_.detector.dominantGapMinimumRatio * 100.0, 0, 'f', 0)
          .arg(kCenterPriorityStartM * 1000.0, 0, 'f', 1)
          .arg(kCenterPriorityFullM * 1000.0, 0, 'f', 1)
          .arg(kMaximumOpposingHeadingFraction * 100.0, 0, 'f', 0));
  emit logMessage(
      CRAWLING_TEXT("原始图预处理参数：中值窗口 5，亮线最小对比度 %1，"
                    "固定填洞 %2 px，自适应填洞 %3%，最小亮段 %4 px，"
                    "最小主缺口 %5%，单侧最小支撑 %6%，"
                    "逐帧全图中心最大跳变 %7%，缺口宽度最大跳变 %8%")
          .arg(settings_.detector.minimumContrast)
          .arg(settings_.detector.maximumHoleLength)
          .arg(settings_.detector.smallGapFillRatio * 100.0, 0, 'f', 2)
          .arg(settings_.detector.minimumRunLength)
          .arg(settings_.detector.minimumGapRatio * 100.0, 0, 'f', 2)
          .arg(settings_.detector.minimumSideSupportRatio * 100.0, 0, 'f', 2)
          .arg(settings_.detector.maximumAbsoluteCenterJumpRatio * 100.0,
               0, 'f', 1)
          .arg(settings_.detector.maximumTrackingGapWidthJumpRatio * 100.0,
               0, 'f', 1));
  emit logMessage(
      CRAWLING_TEXT("相机横向坐标：原图右侧=车体右转正方向；双边缺口需连续 %1 帧确认，"
                    "焊道包络距有效激光端点小于 %2% 时优先回中；"
                    "不能可靠回中则停止，边界保护优先于柔性纠偏")
          .arg(kInitialGapConfirmationFrames)
          .arg(kScanContainmentMarginRatio * 100.0, 0, 'f', 0));
  emit logMessage(settings_.detector.allowOpenCvContour
      ? CRAWLING_TEXT("定位方式：相机原尺寸图像，主基线缺口 + OpenCV 断续凸起轮廓；轮廓需连续五帧确认")
      : settings_.detector.allowDisplacedContourFallback
      ? CRAWLING_TEXT("定位方式：相机原尺寸主基线缺口优先，辅助轮廓仅低权补充")
      : CRAWLING_TEXT("定位方式：相机原尺寸主基线缺口及双边中心；凸起轮廓接管已关闭"));
  emit logMessage(CRAWLING_TEXT(
      "纠偏连续运行策略：实时原始图负责快速回中，连续拼接点云负责慢速航向；"
      "前进纠偏保持设置速度，短时无效时衰减旧转向；"
      "扫描边界和观测丢失保护优先于柔性纠偏，无法保持有效观测时安全停止"));
  emit logMessage(CRAWLING_TEXT(
      "速度策略：前进纠偏按设置速度匀速巡航，边界距离、观测年龄和回中响应"
      "不再折减巡航速度；起停及返程保留必要过渡，安全保护独立执行停止"));
  emit diagnosticLogMessage(QStringLiteral(
      "event=laser_raw_log_format version=3 interval_ms=%1 "
      "profile_encoding=hex4_mean_128_bins "
      "present_runs_encoding=inclusive_pixel_ranges "
      "raw_q4_source=baseline_band_top3 smooth_q4_source=median5 "
      "snapshot_encoding=lossless_png snapshot_max_hz=5 "
      "purpose=diagnose_baseline_and_replay_original_camera_frame")
                                .arg(kRawFrameDiagnosticIntervalMs));
  const double guidanceWindowM =
      clamp(settings_.segmentLengthM * 4.0, 0.08, 0.30);
  const double guidanceMinimumSpanM =
      clamp(settings_.segmentLengthM * 1.75, 0.035, 0.12);
  const double maximumHeadingStepRad = clamp(
      settings_.maximumCurvatureRadPerM * settings_.segmentLengthM * 1.5,
      kMinimumHeadingStepRad, kMaximumHeadingStepRad);
  emit logMessage(
      CRAWLING_TEXT("纠偏平滑：中心滤波 %1 ms，滚动窗口 %2 mm，"
                    "滚动拟合最小跨度 %3 mm，单段航向最多更新 %4°，"
                    "反向需连续 %5 段确认")
          .arg(kCenterFilterTimeConstantS * 1000.0, 0, 'f', 0)
          .arg(guidanceWindowM * 1000.0, 0, 'f', 0)
          .arg(guidanceMinimumSpanM * 1000.0, 0, 'f', 0)
          .arg(maximumHeadingStepRad * 180.0 / kPi, 0, 'f', 2)
          .arg(kFitReversalConfirmationSegments));
  setPhase(Phase::AwaitingInputs,
           CRAWLING_TEXT("自动纠偏已启动，等待新图像和轮端里程"));
  commandImmediateStop(status_.reason);
}

void LaserCorrectionController::processCameraFrame(
    const QImage& image, quint32 sourceFrameNumber, qint64 receivedAtEpochMs) {
  // The inactive preview has its own latest-frame buffer in DeviceController.
  // Avoid queuing redundant perception/status work before an enable request.
  if (!status_.active) return;
  const qint64 queueAgeMs = QDateTime::currentMSecsSinceEpoch() - receivedAtEpochMs;
  // An old queued frame or repeated SDK frame must not refresh the camera
  // watchdog or masquerade as a new observation of the seam.
  if (image.isNull() || queueAgeMs < 0 || queueAgeMs > settings_.imageTimeoutMs ||
      (sourceFrameNumberValid_ && sourceFrameNumber == sourceFrameNumber_)) {
    const qint64 now = clock_.elapsed();
    if (status_.active && (lastSourceRejectLogMs_ < 0 ||
                          now - lastSourceRejectLogMs_ >= 1000)) {
      lastSourceRejectLogMs_ = now;
      emit diagnosticLogMessage(
          QStringLiteral("event=laser_source_rejected session=%1 sdk_frame=%2 previous_sdk_frame=%3 queue_age_ms=%4 reason=%5")
              .arg(trajectorySessionId_).arg(sourceFrameNumber).arg(sourceFrameNumber_)
              .arg(queueAgeMs)
              .arg(image.isNull() ? QStringLiteral("null_image") :
                   (queueAgeMs < 0 || queueAgeMs > settings_.imageTimeoutMs)
                       ? QStringLiteral("stale_or_invalid_receipt")
                       : QStringLiteral("duplicate_sdk_frame")));
    }
    return;
  }
  sourceFrameNumber_ = sourceFrameNumber;
  sourceFrameNumberValid_ = true;
  sourceReceivedAtEpochMs_ = receivedAtEpochMs;
  processingSourceFrame_ = true;
  processCameraImage(image);
  processingSourceFrame_ = false;
}

void LaserCorrectionController::processCameraImage(const QImage& image) {
  if (image.isNull() || boundaryStopLatched_) return;
  boundaryObservationReliable_ = false;
  const qint64 now = clock_.elapsed();
  const qint64 queueAgeMs = processingSourceFrame_
      ? std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - sourceReceivedAtEpochMs_)
      : 0;
  lastFrameQueueAgeMs_ = queueAgeMs;
  lastImageMs_ = now - queueAgeMs;
  ++cameraFrameSequence_;
  // A succession of inferred edges must not renew the real weld identity
  // forever. A measured, associated raised contour can remain live without
  // a dark gap: do not discard it solely because its optical mode changed.
  const bool liveContourTrack = lastObservedWasContour_ &&
      !reacquisitionPending_ &&
      lastValidDetectionMs_ >= 0 &&
      now - lastValidDetectionMs_ <= settings_.detectionTimeoutMs;
  if (status_.active && gapTrackerValid_ && lastTwoEdgeDetectionMs_ >= 0 &&
      !liveContourTrack &&
      now - lastTwoEdgeDetectionMs_ >
          std::min<qint64>(settings_.detectionRecoveryTimeoutMs,
                           kMaximumUnmeasuredSeamMs / 2)) {
    // Leave time for multi-frame confirmation before the independent
    // measured-seam timeout. Reacquisition must not extend that deadline.
    beginSeamReacquisition(CRAWLING_TEXT("真实双边缘持续缺失，解除旧候选锁定"));
  }
  LaserGapDetectorConfig detectorConfig = settings_.detector;
  // Global re-localization unlocks position, not the laser orientation or
  // the scale of a confirmed seam. Otherwise a 30 px speckle hole can replace
  // a 500 px weld just because the latter is temporarily clipped at the edge.
  detectorConfig.referenceAbsoluteCenterRatio = -1.0;
  if (gapTrackerValid_) {
    detectorConfig.expectedAxis = trackedGapHorizontal_ ? 1 : 2;
    if (reacquisitionPending_) {
      const double previousAbsoluteWidth = trackedGapWidthRatio_ *
          std::max(0.0, trackedLineEndRatio_ - trackedLineStartRatio_);
      detectorConfig.minimumGapRatio = std::max(
          detectorConfig.minimumGapRatio, previousAbsoluteWidth * 0.40);
      detectorConfig.minimumSideSupportRatio = std::min(
          detectorConfig.minimumSideSupportRatio, 0.012);
    }
  }
  if (gapTrackerValid_ && !reacquisitionPending_) {
    detectorConfig.expectedCenterRatio = trackedGapCenterRatio_;
    detectorConfig.expectedGapWidthRatio = trackedGapWidthRatio_;
    detectorConfig.expectedAbsoluteCenterRatio =
        liveContourTrack ? lastObservedGapAbsoluteCenterRatio_
                         : trackedGapAbsoluteCenterRatio_;
    if (liveContourTrack) {
      detectorConfig.expectedCenterRatio = detectorConfig.expectedAbsoluteCenterRatio;
    }
    // Keep the previous gap width in the full camera coordinate system too.
    // The visible laser span can shrink at either optical edge; using only
    // the local span ratio then changes the expected physical weld width and
    // rejects the very edge observation needed to recover it.
    detectorConfig.expectedAbsoluteGapWidthRatio =
        trackedGapWidthRatio_ *
        std::max(0.0, trackedLineEndRatio_ - trackedLineStartRatio_);
    // A slanted seam moves across the image during both survey and return.
    // Its startup position is not a stationary landmark. Keep the local
    // frame-to-frame gate, but never reject accumulated real displacement.
    // Edge recovery must be bounded by the camera axis, not by the last
    // visible stripe span. The latter may already be clipped at the side
    // where the weld is leaving the scan and would clamp the inferred gap
    // back inside the stale span.
    detectorConfig.expectedLineStartRatio = 0.0;
    detectorConfig.expectedLineEndRatio = 1.0;
    detectorConfig.expectedAxis = trackedGapHorizontal_ ? 1 : 2;
  }
  LaserSeamPose observationPose{odometryLongitudinalM_, odometryLateralM_,
                               yawRad_};
  if (haveTelemetry_ && lastTelemetryMs_ >= enabledAtMs_ &&
      std::abs(lastImageMs_ - lastTelemetryMs_) <= settings_.telemetryTimeoutMs) {
    const double dt = clamp(
        (lastImageMs_ - lastTelemetryMs_) / 1000.0, -0.5, 0.5);
    const double speed = (leftSpeedMps_ + rightSpeedMps_) * 0.5;
    const double yawDelta =
        (leftSpeedMps_ - rightSpeedMps_) / settings_.trackWidthM * dt;
    observationPose.longitudinalM +=
        std::cos(observationPose.yawRad + yawDelta * 0.5) * speed * dt;
    observationPose.lateralM +=
        std::sin(observationPose.yawRad + yawDelta * 0.5) * speed * dt;
    observationPose.yawRad = normalizedAngle(observationPose.yawRad + yawDelta);
  }
  seamPrediction_ = status_.active && gapTrackerValid_ && !reacquisitionPending_ &&
                     haveTelemetry_
      ? seamTrajectory_.predict(observationPose, lastImageMs_,
                                settings_.laserCenterLookaheadM,
                                settings_.imageLateralSpanM,
                                kCameraLateralToVehicleSign)
      : LaserSeamPrediction{};
  if (seamPrediction_.valid) {
    // Reproject the registered real-edge cloud into this camera pose. The
    // predicted corridor is only a bounded association prior. The current
    // raw image remains primary, so a stale or slightly wrong cloud fit
    // cannot reject a real gap that has just moved in the scan.
    const double lineSpanRatio =
        std::max(0.01, trackedLineEndRatio_ - trackedLineStartRatio_);
    const double trajectoryPriorWeight = clamp(
        seamPrediction_.confidence * 0.35, 0.0, 0.35);
    detectorConfig.expectedAbsoluteCenterRatio =
        (1.0 - trajectoryPriorWeight) * detectorConfig.expectedAbsoluteCenterRatio +
        trajectoryPriorWeight * seamPrediction_.absoluteCenterRatio;
    // The detector range below is the full raw-image axis. Keep the local
    // prior in that same coordinate system instead of normalizing it by the
    // clipped span from the previous frame.
    detectorConfig.expectedCenterRatio = clamp(
        detectorConfig.expectedAbsoluteCenterRatio, 0.0, 1.0);
    const double associatedWidthRatio =
        (1.0 - trajectoryPriorWeight) *
            (trackedGapWidthRatio_ * lineSpanRatio) +
        trajectoryPriorWeight * seamPrediction_.absoluteWidthRatio;
    detectorConfig.expectedGapWidthRatio = associatedWidthRatio / lineSpanRatio;
    detectorConfig.expectedAbsoluteGapWidthRatio =
        associatedWidthRatio;
    detectorConfig.expectedLineStartRatio = 0.0;
    detectorConfig.expectedLineEndRatio = 1.0;
    detectorConfig.maximumAbsoluteCenterJumpRatio = std::max(
        detectorConfig.maximumAbsoluteCenterJumpRatio,
        seamPrediction_.uncertaintyRatio);
  }
  LaserRawFrameDiagnostic rawFrameDiagnostic;
  LaserRawFrameDiagnostic* rawFrameDiagnosticOutput =
      status_.active ? &rawFrameDiagnostic : nullptr;
  QElapsedTimer detectorTimer;
  detectorTimer.start();
  const LaserGapDetection detection =
      LaserGapDetector::detect(image, detectorConfig, rawFrameDiagnosticOutput);
  const bool realTwoEdge = detection.valid && !detection.edgeBreakFallback &&
                           !detection.contourFallback;
  lastDetectorDurationMs_ = detectorTimer.elapsed();
  QElapsedTimer publicationTimer;
  publicationTimer.start();
  if (status_.active) emit cameraObservationReady(image, detection);
  if (rawFrameDiagnosticOutput) {
    logRawFrameDiagnostic(rawFrameDiagnostic, detection, detectorConfig, now);
    queueRawFrame(image, rawFrameDiagnostic, detection, detectorConfig, now);
  }
  // Separate detector time from synchronous overlay/log subscribers. This
  // makes a slow disk/UI distinguishable from a slow contour extraction.
  if (status_.active && (lastDetectorDurationMs_ >= 50 ||
                         publicationTimer.elapsed() >= 50 || queueAgeMs >= 50)) {
    emit diagnosticLogMessage(
        QStringLiteral("event=laser_pipeline_slow session=%1 camera_seq=%2 queue_age_at_entry_ms=%3 detector_ms=%4 publication_ms=%5 image_age_ms=%6")
            .arg(trajectorySessionId_).arg(cameraFrameSequence_).arg(queueAgeMs)
            .arg(lastDetectorDurationMs_).arg(publicationTimer.elapsed())
            .arg(clock_.elapsed() - lastImageMs_));
  }

  status_.gapValid = detection.valid;
  trajectoryPredictionActive_ = false;
  status_.horizontalLaser = detection.valid ? detection.horizontal
                                            : trackedGapHorizontal_;
  status_.confidence = detection.valid ? detection.confidence : 0.0;
  status_.edgeBreakFallback = detection.valid && detection.edgeBreakFallback;
  status_.contourFallback = detection.valid && detection.contourFallback;
  status_.detectionHeld = false;
  status_.baselineSupported = detection.baselineSupported;
  status_.baselineOffsetPx = detection.baselineOffsetPx;
  status_.baselineSlope = detection.baselineSlope;
  status_.baselineHalfWidthPx = detection.baselineHalfWidthPx;
  const int lineSpanPx = detection.lineEndPx - detection.lineStartPx;
  bool confirmedContourIdentity = false;
  if (status_.active && ((phase_ == Phase::AwaitingInputs &&
                         !gapTrackerValid_) || reacquisitionPending_)) {
    const bool confirmableContour = detection.valid && detection.contourFallback &&
        detection.baselineSupported &&
        (!detection.contourConflict || detection.contourDominant) &&
        detection.confidence >= 0.060;
    const int requiredFrames = confirmableContour
        ? kContourIdentityConfirmationFrames : kInitialGapConfirmationFrames;
    if ((!realTwoEdge && !confirmableContour) || lineSpanPx <= 0) {
      initialGapConfirmationCount_ = 0;
      status_.gapValid = false;
    } else {
      const double candidateWidthRatio =
          static_cast<double>(detection.gapEndPx - detection.gapStartPx + 1) /
          std::max(1, lineSpanPx + 1);
      const bool sameCandidate = initialGapConfirmationCount_ > 0 &&
          detection.horizontal == initialGapConfirmationHorizontal_ &&
          confirmableContour == initialGapConfirmationContour_ &&
          std::abs(detection.absoluteCenterRatio -
                   pendingInitialGapAbsoluteCenterRatio_) <=
              kInitialGapCenterToleranceRatio &&
          std::abs(candidateWidthRatio - pendingInitialGapWidthRatio_) <=
              std::max(kInitialGapWidthToleranceRatio,
                       pendingInitialGapWidthRatio_ * 0.40);
      if (sameCandidate) {
        ++initialGapConfirmationCount_;
      } else {
        initialGapConfirmationCount_ = 1;
      }
      initialGapConfirmationHorizontal_ = detection.horizontal;
      initialGapConfirmationContour_ = confirmableContour;
      pendingInitialGapAbsoluteCenterRatio_ =
          detection.absoluteCenterRatio;
      pendingInitialGapWidthRatio_ = candidateWidthRatio;
      if (initialGapConfirmationCount_ < requiredFrames) {
        // A re-localization candidate is not yet a measurement. Keep forward
        // motion with decaying authority from the last confirmed observation.
        status_.gapValid = reacquisitionPending_ && gapTrackerValid_;
        status_.confidence = status_.gapValid ? trackedGapConfidence_ * 0.10 : 0.0;
        detectionHeld_ = status_.gapValid;
        status_.detectionHeld = detectionHeld_;
        status_.contourFallback = false;
        detectionEdgeBreakFallback_ = status_.gapValid;
        status_.edgeBreakFallback = detectionEdgeBreakFallback_;
        status_.reason =
            CRAWLING_TEXT("正在确认原始图最大缺口（%1/%2）")
                .arg(initialGapConfirmationCount_)
                .arg(requiredFrames);
        // An unconfirmed candidate cannot renew measured guidance or expand
        // the old weld into an unrelated edge. Keep its last measured
        // envelope and deadline until the candidate is confirmed.
        updateScanBoundaryObservation(detection,
            detection.horizontal ? image.width() : image.height(), lastImageMs_, false);
        if (stopIfScanBoundaryUnsafe(now)) return;
        publishStatus();
        return;
      }
      confirmedContourIdentity = confirmableContour;
      if (reacquisitionPending_) {
        reacquisitionPending_ = false;
        centerRecoveryProtectionLogged_ = false;
        centerRecoveryWindowActive_ = false;
        filteredEdgeValid_ = false;
        filteredGapValid_ = false;
        centerErrorTrendValid_ = false;
        lastCenterTrendSampleMs_ = -1;
        emit logMessage(CRAWLING_TEXT("主激光基线焊缝重新确认，丢弃旧候选锁定；等待实际回中效果后恢复转向权重"));
      }
      emit logMessage(
          CRAWLING_TEXT("主焊缝确认完成：连续 %1 帧，图像中心 %2%，宽度 %3%，来源=%4")
              .arg(initialGapConfirmationCount_)
              .arg(detection.absoluteCenterRatio * 100.0, 0, 'f', 2)
              .arg(candidateWidthRatio * 100.0, 0, 'f', 2)
              .arg(confirmableContour ? CRAWLING_TEXT("辅助轮廓，不写入暗缺口拟合点")
                                       : CRAWLING_TEXT("真实双边缺口")));
    }
  }
  if (detection.valid && !reacquisitionPending_) {
    status_.gapStartPx = detection.gapStartPx;
    status_.gapEndPx = detection.gapEndPx;
    status_.supportingSamples = detection.supportingSamples;
    // OpenCV supplies a measured two-shoulder contour, not an inferred single
    // edge. Its confidence and initial five-frame gate still limit authority.
    detectionEdgeBreakFallback_ = detection.edgeBreakFallback ||
        (detection.contourFallback && !detection.opencvContour);
  }
  // An unconfirmed fallback cannot bypass the multi-frame identity gate.
  if (reacquisitionPending_) status_.gapValid = false;
  if (status_.gapValid && lineSpanPx > 0) {
    const int axisLength = detection.horizontal ? image.width() : image.height();
    latestImageAxisLengthPx_ = axisLength;
    latestLineStartPx_ = detection.lineStartPx;
    latestLineEndPx_ = detection.lineEndPx;
    updateScanBoundaryObservation(detection, axisLength, lastImageMs_);
    const double axisLastPixel = std::max(1, axisLength - 1);
    // Edge samples are physical camera coordinates. Do not normalize them by
    // the detected laser span: that span changes when reflections/occlusion
    // hide a part of the line and would make a stationary weld jump laterally.
    const double leftRatio = clamp(
        static_cast<double>(detection.gapStartPx) / axisLastPixel,
        0.0, 1.0);
    const double rightRatio = clamp(
        static_cast<double>(detection.gapEndPx) / axisLastPixel,
        0.0, 1.0);
    const double firstEdgeM = kCameraLateralToVehicleSign *
                              (leftRatio - 0.5) *
                              settings_.imageLateralSpanM;
    const double secondEdgeM = kCameraLateralToVehicleSign *
                               (rightRatio - 0.5) *
                               settings_.imageLateralSpanM;
    const double measuredLeftEdgeM = std::min(firstEdgeM, secondEdgeM);
    const double measuredRightEdgeM = std::max(firstEdgeM, secondEdgeM);
    measuredLeftEdgeM_ = measuredLeftEdgeM;
    measuredRightEdgeM_ = measuredRightEdgeM;
    if (!filteredEdgeValid_ || lastGapFilterMs_ < 0) {
      filteredLeftEdgeM_ = measuredLeftEdgeM;
      filteredRightEdgeM_ = measuredRightEdgeM;
      filteredEdgeValid_ = true;
    } else {
      const double elapsedSeconds = clamp(
          (now - lastGapFilterMs_) / 1000.0, 0.001, 0.5);
      const double maximumStep =
          kMaximumCenterSlewMps * std::min(elapsedSeconds, 0.10);
      const double confidenceScale =
          clamp(detection.confidence / kFullCenterFeedbackConfidence,
                kMinimumCenterFeedbackWeight, 1.0);
      const double filterWeight =
          (1.0 - std::exp(-elapsedSeconds / kCenterFilterTimeConstantS)) *
          confidenceScale;
      filteredLeftEdgeM_ += clamp(
          filterWeight * (measuredLeftEdgeM - filteredLeftEdgeM_),
          -maximumStep, maximumStep);
      filteredRightEdgeM_ += clamp(
          filterWeight * (measuredRightEdgeM - filteredRightEdgeM_),
          -maximumStep, maximumStep);
    }
    latestLeftEdgeM_ = filteredLeftEdgeM_;
    latestRightEdgeM_ = filteredRightEdgeM_;
    if (!(latestLeftEdgeM_ < latestRightEdgeM_)) status_.gapValid = false;
  } else {
    status_.gapValid = false;
  }

  if (!status_.gapValid) {
    boundaryObservationReliable_ = false;
    if (stopIfScanBoundaryUnsafe(now)) return;
    ++invalidDetectionCount_;
    status_.confidence = 0.0;
    if (!status_.active) {
      status_.reason = CRAWLING_TEXT("未检测到激光线中间断口，自动纠偏未启动");
      publishStatus();
      return;
    }
    const qint64 heldDetectionAgeMs = now - lastValidDetectionMs_;
    // The detected laser span normally touches the image border (for example
    // x=2..1933 in a 2048 px frame). That is not evidence that the weld gap
    // itself is at the optical boundary. Use the gap position, not the line
    // endpoints, for the boundary recovery policy.
    const bool trackedNearLaserBoundary =
        trackedGapCenterRatio_ < 0.10 || trackedGapCenterRatio_ > 0.90 ||
        trackedGapAbsoluteCenterRatio_ < 0.12 ||
        trackedGapAbsoluteCenterRatio_ > 0.88;
    const bool candidateJump = detection.continuityRejected ||
                               detection.widthRejected;
    const qint64 detectionHoldLimitMs =
        candidateJump
            ? std::max<qint64>(settings_.detectionRecoveryTimeoutMs,
                               settings_.transientDetectionHoldMs)
            : (trackedNearLaserBoundary
                   ? std::max<qint64>(settings_.transientDetectionHoldMs,
                                      settings_.detectionTimeoutMs)
                   : settings_.transientDetectionHoldMs);
    if (candidateJump &&
        (lastDetectionRejectLogMs_ < 0 ||
         now - lastDetectionRejectLogMs_ >= kDetectionDiagnosticIntervalMs)) {
      lastDetectionRejectLogMs_ = now;
      emit logMessage(
          CRAWLING_TEXT("激光候选拒绝：连续性=%1，宽度=%2，"
                        "图像仍在到达，进入 %3 ms 恢复窗口")
              .arg(detection.continuityRejected ? CRAWLING_TEXT("是")
                                                 : CRAWLING_TEXT("否"))
              .arg(detection.widthRejected ? CRAWLING_TEXT("是")
                                           : CRAWLING_TEXT("否"))
              .arg(detectionHoldLimitMs));
    }
    if (gapTrackerValid_ && heldDetectionAgeMs >= 0 &&
        heldDetectionAgeMs <= detectionHoldLimitMs) {
      // A short occlusion or rejected bright run must not pulse the motor
      // command to zero. Keep steering from the last real detection, but mark
      // it as held so stale edge points are not added to the next path fit.
      status_.gapValid = true;
      status_.confidence = trackedGapConfidence_ * 0.5;
      detectionHeld_ = true;
      status_.detectionHeld = true;
      status_.contourFallback = false;
      ++reusedDetectionCount_;
      status_.leftEdgeLateralM = latestLeftEdgeM_;
      status_.rightEdgeLateralM = latestRightEdgeM_;
      detectionEdgeBreakFallback_ = true;
      status_.edgeBreakFallback = true;
      const QString reuseReason =
          detection.widthRejected
              ? CRAWLING_TEXT("缺口宽度突变")
              : (detection.continuityRejected
                     ? CRAWLING_TEXT("候选跳变")
                     : CRAWLING_TEXT("短时遮挡"));
      status_.reason = detection.widthRejected
                           ? CRAWLING_TEXT("激光缺口宽度突变，平滑沿用上一帧断口")
                           : (detection.continuityRejected
                                  ? CRAWLING_TEXT("激光候选跳变，平滑沿用上一帧断口")
                                  : CRAWLING_TEXT("激光短时遮挡，平滑沿用上一帧断口"));
      if (lastDetectionDiagnosticMs_ < 0 ||
          now - lastDetectionDiagnosticMs_ >= kDetectionDiagnosticIntervalMs) {
        lastDetectionDiagnosticMs_ = now;
        emit logMessage(
            CRAWLING_TEXT("激光诊断：阶段 %1，模式 previous_gap_reuse，"
                          "线内中心 %2%，图像中心 %3%，宽度 %4%，置信度 %5，"
                          "无效帧 %6，沿用帧 %7，原因 %8，沿用上限 %9 ms")
                .arg(phaseName(phase_))
                .arg(trackedGapCenterRatio_ * 100.0, 0, 'f', 2)
                .arg(trackedGapAbsoluteCenterRatio_ * 100.0, 0, 'f', 2)
                .arg(trackedGapWidthRatio_ * 100.0, 0, 'f', 2)
                .arg(status_.confidence, 0, 'f', 3)
                .arg(invalidDetectionCount_)
                .arg(reusedDetectionCount_)
                .arg(reuseReason)
                .arg(detectionHoldLimitMs));
        invalidDetectionCount_ = 0;
        reusedDetectionCount_ = 0;
      }
      publishStatus();
      return;
    }
    detectionHeld_ = false;
    // A registered rolling cloud is stronger than a frozen last pixel when
    // the laser stripe is displaced by a raised/occluded weld. Use it as a
    // short-lived, low-authority observation while the detector searches for
    // a fresh image candidate. It is deliberately marked held, so it cannot
    // renew lastValidDetectionMs_ or enter the next point-cloud fit.
    if (seamPrediction_.valid && phaseMoves() && !reacquisitionPending_) {
      const double predictedLateralM = kCameraLateralToVehicleSign *
          (seamPrediction_.absoluteCenterRatio - 0.5) *
          settings_.imageLateralSpanM;
      if (!filteredGapValid_ || lastGapFilterMs_ < 0) {
        filteredGapLateralM_ = predictedLateralM;
        filteredGapValid_ = true;
      } else {
        const double elapsedSeconds = clamp(
            (now - lastGapFilterMs_) / 1000.0, 0.001, 0.5);
        const double maximumStep =
            kMaximumCenterSlewMps * std::min(elapsedSeconds, 0.10);
        filteredGapLateralM_ += clamp(
            0.65 * (predictedLateralM - filteredGapLateralM_),
            -maximumStep, maximumStep);
      }
      lastGapFilterMs_ = now;
      status_.gapValid = true;
      const double trackedLineSpanRatio =
          std::max(0.01, trackedLineEndRatio_ - trackedLineStartRatio_);
      status_.gapCenterRatio = clamp(
          (seamPrediction_.absoluteCenterRatio - trackedLineStartRatio_) /
              trackedLineSpanRatio,
          0.0, 1.0);
      status_.gapAbsoluteCenterRatio = seamPrediction_.absoluteCenterRatio;
      status_.gapLateralM = predictedLateralM;
      const double predictedWidthM = seamPrediction_.absoluteWidthRatio *
                                     settings_.imageLateralSpanM;
      status_.leftEdgeLateralM = predictedLateralM - predictedWidthM * 0.5;
      status_.rightEdgeLateralM = predictedLateralM + predictedWidthM * 0.5;
      if (latestImageAxisLengthPx_ > 1) {
        const double axisLastPixel = latestImageAxisLengthPx_ - 1.0;
        status_.gapStartPx = static_cast<int>(std::lround(
            std::clamp(seamPrediction_.absoluteCenterRatio -
                           seamPrediction_.absoluteWidthRatio * 0.5,
                       0.0, 1.0) * axisLastPixel));
        status_.gapEndPx = static_cast<int>(std::lround(
            std::clamp(seamPrediction_.absoluteCenterRatio +
                           seamPrediction_.absoluteWidthRatio * 0.5,
                       0.0, 1.0) * axisLastPixel));
      }
      status_.supportingSamples = 0;
      status_.confidence = std::clamp(seamPrediction_.confidence * 0.45,
                                     0.015, 0.08);
      detectionHeld_ = true;
      detectionEdgeBreakFallback_ = true;
      trajectoryPredictionActive_ = true;
      status_.detectionHeld = true;
      status_.contourFallback = false;
      status_.edgeBreakFallback = true;
      // Prediction cannot prove recovery, but it can expose additional risk.
      boundaryGapStartRatio_ = std::min(boundaryGapStartRatio_,
          seamPrediction_.absoluteCenterRatio - seamPrediction_.absoluteWidthRatio * 0.5);
      boundaryGapEndRatio_ = std::max(boundaryGapEndRatio_,
          seamPrediction_.absoluteCenterRatio + seamPrediction_.absoluteWidthRatio * 0.5);
      if (stopIfScanBoundaryUnsafe(now)) return;
      ++reusedDetectionCount_;
      status_.reason = CRAWLING_TEXT(
          "当前激光缺口被凸起遮挡，沿连续点云预测中心低权重前进并重新搜索");
      if (lastDetectionDiagnosticMs_ < 0 ||
          now - lastDetectionDiagnosticMs_ >= kDetectionDiagnosticIntervalMs) {
        lastDetectionDiagnosticMs_ = now;
        emit logMessage(
            CRAWLING_TEXT("激光诊断：模式 trajectory_prior，预测中心 %1%，"
                          "宽度 %2%，置信度 %3，样本 %4，年龄 %5 ms，"
                          "外推 %6 mm；不写入新点云")
                .arg(seamPrediction_.absoluteCenterRatio * 100.0, 0, 'f', 2)
                .arg(seamPrediction_.absoluteWidthRatio * 100.0, 0, 'f', 2)
                .arg(seamPrediction_.confidence, 0, 'f', 3)
                .arg(seamPrediction_.sampleCount)
                .arg(seamPrediction_.observationAgeMs)
                .arg(seamPrediction_.extrapolationM * 1000.0, 0, 'f', 1));
        invalidDetectionCount_ = 0;
        reusedDetectionCount_ = 0;
      }
      publishStatus();
      return;
    }
    if (gapTrackerValid_) {
      beginSeamReacquisition(CRAWLING_TEXT("沿用窗口结束仍无有效候选，重新搜索主激光基线"));
    }
    if (status_.active && phaseMoves() && gapTrackerValid_) {
      // The camera is still delivering frames, but the detector cannot form a
      // trustworthy new gap. Do not pulse the motors to zero: keep the last
      // raw-image center as a decaying prediction while searching globally.
      // Neither stale points nor an untrusted fit can steer the next segment.
      status_.gapValid = true;
      status_.gapCenterRatio = trackedGapCenterRatio_;
      status_.gapAbsoluteCenterRatio = trackedGapAbsoluteCenterRatio_;
      status_.gapLateralM = kCameraLateralToVehicleSign *
                            (trackedGapAbsoluteCenterRatio_ - 0.5) *
                            settings_.imageLateralSpanM;
      status_.confidence = std::max(0.001, trackedGapConfidence_ * 0.10);
      status_.leftEdgeLateralM = latestLeftEdgeM_;
      status_.rightEdgeLateralM = latestRightEdgeM_;
      detectionHeld_ = true;
      detectionEdgeBreakFallback_ = true;
      status_.detectionHeld = true;
      status_.contourFallback = false;
      status_.edgeBreakFallback = true;
      ++reusedDetectionCount_;
      status_.reason = CRAWLING_TEXT(
          "实时焊道暂时无效，短时保持巡航并优先回中；持续失去有效观测时停止");
      if (!detectionDegradedLogged_) {
        detectionDegradedLogged_ = true;
        emit logMessage(
            CRAWLING_TEXT("连续运行降级：新原始图仍在到达，但主缺口已连续 %1 ms "
                          "未通过检测；安全范围内短时保持巡航，禁止写入陈旧点云，"
                          "丢弃旧拟合并重新确认真实双边缘")
                .arg(heldDetectionAgeMs));
      }
      publishStatus();
      return;
    }
    if (phaseNeedsLaser()) {
      emit logMessage(
          CRAWLING_TEXT("激光候选无效：连续性拒绝 %1，宽度突变拒绝 %2，"
                        "已沿用 %3 ms 后仍未恢复")
              .arg(detection.continuityRejected ? CRAWLING_TEXT("是")
                                                : CRAWLING_TEXT("否"))
              .arg(detection.widthRejected ? CRAWLING_TEXT("是")
                                           : CRAWLING_TEXT("否"))
              .arg(detectionHoldLimitMs));
      resetControllerError();
      commandImmediateStop(CRAWLING_TEXT("尚无可沿用的主缺口，等待原始图恢复"));
    } else {
      publishStatus();
    }
    return;
  }

  const bool firstValidAfterEnable =
      status_.active && lastValidDetectionMs_ < enabledAtMs_;
  detectionHeld_ = false;
  status_.detectionHeld = false;
  lastValidDetectionMs_ = now;
  lastObservedGapAbsoluteCenterRatio_ = detection.absoluteCenterRatio;
  lastObservedWasContour_ = detection.contourFallback && detection.confidence >= 0.060;
  if (detectionDegradedLogged_) {
    emit logMessage(
        CRAWLING_TEXT("实时缺口检测恢复：退出上一帧预测模式，重新使用当前原始图闭环"));
    detectionDegradedLogged_ = false;
  }
  status_.gapCenterRatio = detection.normalizedCenter;
  status_.gapAbsoluteCenterRatio = detection.absoluteCenterRatio;
  status_.gapLateralM = kCameraLateralToVehicleSign *
                        (detection.absoluteCenterRatio - 0.5) *
                        settings_.imageLateralSpanM;
  if (!filteredGapValid_ || lastGapFilterMs_ < 0) {
    filteredGapLateralM_ = status_.gapLateralM;
    filteredGapValid_ = true;
  } else {
    const double elapsedSeconds =
        clamp((now - lastGapFilterMs_) / 1000.0, 0.001, 0.5);
    const double maximumStep =
        kMaximumCenterSlewMps * std::min(elapsedSeconds, 0.10);
    const double confidenceScale =
        clamp(detection.confidence / kFullCenterFeedbackConfidence,
              kMinimumCenterFeedbackWeight, 1.0);
    const double filterWeight =
        (1.0 - std::exp(-elapsedSeconds / kCenterFilterTimeConstantS)) *
        confidenceScale;
    filteredGapLateralM_ += clamp(
        filterWeight * (status_.gapLateralM - filteredGapLateralM_),
        -maximumStep, maximumStep);
  }
  lastGapFilterMs_ = now;
  status_.leftEdgeLateralM = latestLeftEdgeM_;
  status_.rightEdgeLateralM = latestRightEdgeM_;
  if (realTwoEdge || confirmedContourIdentity ||
      (detection.opencvContour && liveContourTrack)) {
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
    if (realTwoEdge) lastTwoEdgeDetectionMs_ = now;
    // A five-frame contour may seed/reacquire identity once. Ordinary
    // fallback frames cannot repeatedly rewrite this width or span.
  } else if (detection.edgeBreakFallback && !detection.contourFallback &&
             gapTrackerValid_ && detection.horizontal == trackedGapHorizontal_) {
    // A visible edge can move the known seam, but cannot redefine its width
    // or coordinate span. Contour-only observations remain frame-local.
    const double maximumCenterStep =
        std::max(0.0, detectorConfig.maximumAbsoluteCenterJumpRatio);
    trackedGapAbsoluteCenterRatio_ += clamp(
        detection.absoluteCenterRatio - trackedGapAbsoluteCenterRatio_,
        -maximumCenterStep, maximumCenterStep);
    trackedGapCenterRatio_ = clamp(
        (trackedGapAbsoluteCenterRatio_ - trackedLineStartRatio_) /
            std::max(0.01, trackedLineEndRatio_ - trackedLineStartRatio_),
        0.0, 1.0);
    lastTrackedGapMs_ = now;
  }
  status_.referenceGapAbsoluteCenterRatio =
      trackingReferenceGapAbsoluteCenterRatio_ >= 0.0
          ? trackingReferenceGapAbsoluteCenterRatio_
          : referenceGapAbsoluteCenterRatio_;
  if (status_.active && realTwoEdge &&
      (phase_ == Phase::SurveyForward || phase_ == Phase::TrackingForward)) {
    seamTrajectory_.observe(observationPose, lastImageMs_, measuredLeftEdgeM_,
                            measuredRightEdgeM_,
                            settings_.laserCenterLookaheadM,
                            detection.confidence);
  }
  updateCenterErrorTrend(now);
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
  if (stopIfScanBoundaryUnsafe(now)) return;
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
                    "断口 %6..%7 px，线内中心 %8%，图像中心 %9%，宽度 %10%，"
                    "置信度 %11，支撑点 %12，无效帧 %13，沿用帧 %14，"
                    "轮廓候选 %15（%16..%17 px，置信度 %18，冲突=%19）")
          .arg(phaseName(phase_))
          .arg(detection.opencvContour
                   ? CRAWLING_TEXT("opencv_raised_contour")
                   : detection.contourFallback
                   ? CRAWLING_TEXT("contour_fallback")
                   : detection.edgeBreakFallback
                   ? (detection.widthRejected
                          ? CRAWLING_TEXT("edge_break_width_guard")
                          : CRAWLING_TEXT("edge_break_fallback"))
                   : (detection.widthRejected ? CRAWLING_TEXT("width_guard")
                                               : CRAWLING_TEXT("normal_gap")))
          .arg(detection.horizontal ? CRAWLING_TEXT("横向")
                                    : CRAWLING_TEXT("纵向"))
          .arg(detection.lineStartPx)
          .arg(detection.lineEndPx)
          .arg(detection.gapStartPx)
          .arg(detection.gapEndPx)
          .arg(detection.normalizedCenter * 100.0, 0, 'f', 2)
          .arg(detection.absoluteCenterRatio * 100.0, 0, 'f', 2)
          .arg(lineSpanPx > 0 ? gapWidthPx * 100.0 / lineSpanPx : 0.0,
               0, 'f', 2)
          .arg(detection.confidence, 0, 'f', 3)
          .arg(detection.supportingSamples)
          .arg(invalidDetectionCount_)
          .arg(reusedDetectionCount_)
          .arg(detection.contourSupported ? CRAWLING_TEXT("是")
                                           : CRAWLING_TEXT("否"))
          .arg(detection.contourStartPx)
          .arg(detection.contourEndPx)
          .arg(detection.contourConfidence, 0, 'f', 3)
          .arg(detection.contourConflict ? CRAWLING_TEXT("是")
                                         : CRAWLING_TEXT("否")));
  invalidDetectionCount_ = 0;
  reusedDetectionCount_ = 0;
}

void LaserCorrectionController::logRawFrameDiagnostic(
    const LaserRawFrameDiagnostic& diagnostic,
    const LaserGapDetection& detection,
    const LaserGapDetectorConfig& config, qint64 now) {
  if (!status_.active || !diagnostic.available ||
      (lastRawFrameDiagnosticMs_ >= 0 &&
       now - lastRawFrameDiagnosticMs_ < kRawFrameDiagnosticIntervalMs)) {
    return;
  }
  lastRawFrameDiagnosticMs_ = now;
  ++rawFrameDiagnosticSequence_;

  const int axisLengthPx = diagnostic.horizontal ? diagnostic.imageWidth
                                                  : diagnostic.imageHeight;
  const QString mode =
      detection.valid
          ? (detection.contourFallback
                 ? QStringLiteral("contour_fallback")
                 : (detection.edgeBreakFallback
                        ? QStringLiteral("edge_inferred")
                        : QStringLiteral("raw_two_edge")))
          : (detection.continuityRejected
                 ? QStringLiteral("continuity_rejected")
                 : (detection.widthRejected
                        ? QStringLiteral("width_rejected")
                        : (diagnostic.threshold < 0
                               ? QStringLiteral("low_contrast")
                               : QStringLiteral("no_supported_gap"))));
  const double expectedCenterPercent =
      config.expectedAbsoluteCenterRatio >= 0.0
          ? config.expectedAbsoluteCenterRatio * 100.0 : -1.0;
  emit diagnosticLogMessage(
      QStringLiteral(
          "event=laser_raw_frame version=3 diag_seq=%1 camera_seq=%2 "
          "session=%3 phase=%4 clock_ms=%5 image=%6x%7 axis=%8 "
          "sample_step_px=%9 profile_samples=%10 background=%11 "
          "line_level=%12 threshold=%13 contrast=%14 "
          "hole_fill_samples=%15 minimum_run_samples=%16 mode=%17 "
          "detector_valid=%18 edge_inferred=%19 continuity_rejected=%20 "
          "contour_fallback=%21 contour_confidence=%22 width_rejected=%23 "
          "line_px=%24:%25 gap_px=%26:%27 gap_center_pct=%28 "
          "expected_center_pct=%29 confidence=%30 support_samples=%31 "
          "present_runs=%32 raw_q4=%33 smooth_q4=%34 contour_q4=%35 "
          "contour_threshold_px=%36 contour_min_run=%37 "
          "contour_supported=%38 contour_agrees=%39 contour_conflict=%40 "
          "contour_px=%41:%42 contour_dominant=%43")
          .arg(rawFrameDiagnosticSequence_)
          .arg(cameraFrameSequence_)
          .arg(trajectorySessionId_)
          .arg(phaseName(phase_))
          .arg(now)
          .arg(diagnostic.imageWidth)
          .arg(diagnostic.imageHeight)
          .arg(diagnostic.horizontal ? QStringLiteral("horizontal")
                                     : QStringLiteral("vertical"))
          .arg(diagnostic.sampleStepPx)
          .arg(diagnostic.rawProfile.size())
          .arg(diagnostic.backgroundLevel)
          .arg(diagnostic.lineLevel)
          .arg(diagnostic.threshold)
          .arg(diagnostic.lineLevel - diagnostic.backgroundLevel)
          .arg(diagnostic.maximumHoleSamples)
          .arg(diagnostic.minimumRunSamples)
          .arg(mode)
          .arg(detection.valid ? 1 : 0)
          .arg(detection.edgeBreakFallback ? 1 : 0)
          .arg(detection.continuityRejected ? 1 : 0)
          .arg(detection.contourFallback ? 1 : 0)
          .arg(detection.contourConfidence, 0, 'f', 4)
          .arg(detection.widthRejected ? 1 : 0)
          .arg(detection.lineStartPx)
          .arg(detection.lineEndPx)
          .arg(detection.gapStartPx)
          .arg(detection.gapEndPx)
          .arg(detection.absoluteCenterRatio * 100.0, 0, 'f', 2)
          .arg(expectedCenterPercent, 0, 'f', 2)
          .arg(detection.confidence, 0, 'f', 4)
          .arg(detection.supportingSamples)
          .arg(encodePresentRuns(diagnostic.presentProfile,
                                 diagnostic.sampleStepPx, axisLengthPx))
          .arg(encodeQuantizedProfile(diagnostic.rawProfile))
          .arg(encodeQuantizedProfile(diagnostic.filteredProfile))
          .arg(encodePresentRuns(diagnostic.contourProfile,
                                 diagnostic.sampleStepPx, axisLengthPx))
          .arg(diagnostic.contourDisplacementThresholdPx)
          .arg(diagnostic.contourMinimumRunSamples)
          .arg(detection.contourSupported ? 1 : 0)
          .arg(detection.contourAgreesWithGap ? 1 : 0)
          .arg(detection.contourConflict ? 1 : 0)
          .arg(detection.contourStartPx)
          .arg(detection.contourEndPx)
          .arg(detection.contourDominant ? 1 : 0));
  emit diagnosticLogMessage(QStringLiteral(
      "event=laser_baseline_frame session=%1 camera_seq=%2 source_frame=%3 "
      "source_received_epoch_ms=%4 queue_age_ms=%5 baseline_supported=%6 "
      "baseline_offset_px=%7 baseline_slope=%8 band_half_width_px=%9 "
      "baseline_support=%10 baseline_residual_px=%11 full_projection_q4=%12 "
      "profile_kind=baseline_band image_span_m=%13 lookahead_m=%14 geometry_source=assumed "
      "queue_age_at_entry_ms=%15 detector_ms=%16")
      .arg(trajectorySessionId_).arg(cameraFrameSequence_)
      .arg(processingSourceFrame_ ? QString::number(sourceFrameNumber_) : QStringLiteral("unknown"))
      .arg(processingSourceFrame_ ? sourceReceivedAtEpochMs_ : -1)
      .arg(processingSourceFrame_ ? QDateTime::currentMSecsSinceEpoch() - sourceReceivedAtEpochMs_ : -1)
      .arg(detection.baselineSupported ? 1 : 0)
      .arg(diagnostic.baselineOffsetPx, 0, 'f', 3)
      .arg(diagnostic.baselineSlope, 0, 'f', 6)
      .arg(diagnostic.baselineHalfWidthPx, 0, 'f', 2)
      .arg(diagnostic.baselineSupportRatio, 0, 'f', 3)
      .arg(diagnostic.baselineResidualPx, 0, 'f', 3)
      .arg(encodeQuantizedProfile(diagnostic.fullProjectionProfile))
      .arg(settings_.imageLateralSpanM).arg(settings_.laserCenterLookaheadM)
      .arg(lastFrameQueueAgeMs_).arg(lastDetectorDurationMs_));
  emit diagnosticLogMessage(QStringLiteral(
      "event=seam_trajectory_prior session=%1 camera_seq=%2 valid=%3 "
      "center_ratio=%4 width_ratio=%5 uncertainty_ratio=%6 confidence=%7 "
      "samples=%8 span_m=%9 extrapolation_m=%10 observation_age_ms=%11 "
      "role=candidate_association_bounded_fallback")
      .arg(trajectorySessionId_).arg(cameraFrameSequence_)
      .arg(seamPrediction_.valid ? 1 : 0)
      .arg(seamPrediction_.absoluteCenterRatio, 0, 'f', 6)
      .arg(seamPrediction_.absoluteWidthRatio, 0, 'f', 6)
      .arg(seamPrediction_.uncertaintyRatio, 0, 'f', 6)
      .arg(seamPrediction_.confidence, 0, 'f', 4)
      .arg(seamPrediction_.sampleCount)
      .arg(seamPrediction_.observedSpanM, 0, 'f', 6)
      .arg(seamPrediction_.extrapolationM, 0, 'f', 6)
      .arg(seamPrediction_.observationAgeMs));
}

void LaserCorrectionController::queueRawFrame(
    const QImage& image, const LaserRawFrameDiagnostic& diagnostic,
    const LaserGapDetection& detection, const LaserGapDetectorConfig& config,
    qint64 now) {
  if (!status_.active || rawArchiveDisabled_ || rawFrameSavePending_ ||
      (lastRawFrameSaveMs_ >= 0 && now - lastRawFrameSaveMs_ < 200)) return;
  const int axisLengthPx = diagnostic.horizontal ? diagnostic.imageWidth
                                                 : diagnostic.imageHeight;
  QJsonObject record;
  QJsonObject detector;
  detector.insert(QStringLiteral("minimumGapRatio"), config.minimumGapRatio);
  detector.insert(QStringLiteral("minimumSideSupportRatio"), config.minimumSideSupportRatio);
  detector.insert(QStringLiteral("minimumContrast"), config.minimumContrast);
  detector.insert(QStringLiteral("allowOpenCvContour"), config.allowOpenCvContour);
  detector.insert(QStringLiteral("maximumHoleLength"), config.maximumHoleLength);
  detector.insert(QStringLiteral("smallGapFillRatio"), config.smallGapFillRatio);
  detector.insert(QStringLiteral("minimumRunLength"), config.minimumRunLength);
  detector.insert(QStringLiteral("dominantGapMinimumRatio"), config.dominantGapMinimumRatio);
  detector.insert(QStringLiteral("expectedCenterRatio"), config.expectedCenterRatio);
  detector.insert(QStringLiteral("expectedGapWidthRatio"), config.expectedGapWidthRatio);
  detector.insert(QStringLiteral("expectedAbsoluteGapWidthRatio"),
                 config.expectedAbsoluteGapWidthRatio);
  detector.insert(QStringLiteral("maximumTrackingCenterJumpRatio"), config.maximumTrackingCenterJumpRatio);
  detector.insert(QStringLiteral("expectedAbsoluteCenterRatio"), config.expectedAbsoluteCenterRatio);
  detector.insert(QStringLiteral("referenceAbsoluteCenterRatio"), config.referenceAbsoluteCenterRatio);
  detector.insert(QStringLiteral("maximumAbsoluteCenterJumpRatio"), config.maximumAbsoluteCenterJumpRatio);
  detector.insert(QStringLiteral("maximumReferenceCenterDriftRatio"), config.maximumReferenceCenterDriftRatio);
  detector.insert(QStringLiteral("trackingAbsoluteCenterWeight"), config.trackingAbsoluteCenterWeight);
  detector.insert(QStringLiteral("trackingWidthWeight"), config.trackingWidthWeight);
  detector.insert(QStringLiteral("allowEdgeBreakFallback"), config.allowEdgeBreakFallback);
  detector.insert(QStringLiteral("allowDisplacedContourFallback"), config.allowDisplacedContourFallback);
  detector.insert(QStringLiteral("edgeBreakGapRatio"), config.edgeBreakGapRatio);
  detector.insert(QStringLiteral("minimumEdgeBreakRunRatio"), config.minimumEdgeBreakRunRatio);
  detector.insert(QStringLiteral("maximumTrackingGapWidthJumpRatio"), config.maximumTrackingGapWidthJumpRatio);
  detector.insert(QStringLiteral("expectedLineStartRatio"), config.expectedLineStartRatio);
  detector.insert(QStringLiteral("expectedLineEndRatio"), config.expectedLineEndRatio);
  detector.insert(QStringLiteral("expectedAxis"), config.expectedAxis);
  record.insert(QStringLiteral("detector_config"), detector);
  record.insert(QStringLiteral("clock_ms"), static_cast<double>(now));
  record.insert(QStringLiteral("epoch_ms"), QString::number(QDateTime::currentMSecsSinceEpoch()));
  record.insert(QStringLiteral("phase"), phaseName(phase_));
  record.insert(QStringLiteral("camera_seq"), QString::number(cameraFrameSequence_));
  record.insert(QStringLiteral("source_frame"), processingSourceFrame_ ? QString::number(sourceFrameNumber_) : QStringLiteral("unknown"));
  record.insert(QStringLiteral("source_received_epoch_ms"), QString::number(processingSourceFrame_ ? sourceReceivedAtEpochMs_ : -1));
  record.insert(QStringLiteral("queue_age_at_entry_ms"), lastFrameQueueAgeMs_);
  record.insert(QStringLiteral("detector_ms"), lastDetectorDurationMs_);
  record.insert(QStringLiteral("valid"), detection.valid);
  record.insert(QStringLiteral("horizontal"), diagnostic.horizontal);
  record.insert(QStringLiteral("gap_start_px"), detection.gapStartPx);
  record.insert(QStringLiteral("gap_end_px"), detection.gapEndPx);
  record.insert(QStringLiteral("center_ratio"), detection.absoluteCenterRatio);
  record.insert(QStringLiteral("confidence"), detection.confidence);
  record.insert(QStringLiteral("inferred"), detection.edgeBreakFallback);
  record.insert(QStringLiteral("contour_supported"), detection.contourSupported);
  record.insert(QStringLiteral("contour_fallback"), detection.contourFallback);
  record.insert(QStringLiteral("opencv_contour"), detection.opencvContour);
  record.insert(QStringLiteral("contour_dominant"), detection.contourDominant);
  record.insert(QStringLiteral("contour_start_px"), detection.contourStartPx);
  record.insert(QStringLiteral("contour_end_px"), detection.contourEndPx);
  record.insert(QStringLiteral("contour_confidence"), detection.contourConfidence);
  record.insert(QStringLiteral("contour_agrees_with_gap"),
                detection.contourAgreesWithGap);
  record.insert(QStringLiteral("contour_conflict"), detection.contourConflict);
  record.insert(QStringLiteral("contour_threshold_px"),
                diagnostic.contourDisplacementThresholdPx);
  record.insert(QStringLiteral("contour_minimum_run_samples"),
                diagnostic.contourMinimumRunSamples);
  record.insert(QStringLiteral("present_runs"),
                encodePresentRuns(diagnostic.presentProfile,
                                  diagnostic.sampleStepPx, axisLengthPx));
  record.insert(QStringLiteral("contour_runs"),
                encodePresentRuns(diagnostic.contourProfile,
                                  diagnostic.sampleStepPx, axisLengthPx));
  record.insert(QStringLiteral("continuity_rejected"), detection.continuityRejected);
  record.insert(QStringLiteral("width_rejected"), detection.widthRejected);
  record.insert(QStringLiteral("baseline_supported"), detection.baselineSupported);
  record.insert(QStringLiteral("baseline_offset_px"), diagnostic.baselineOffsetPx);
  record.insert(QStringLiteral("baseline_slope"), diagnostic.baselineSlope);
  record.insert(QStringLiteral("band_half_width_px"), diagnostic.baselineHalfWidthPx);
  record.insert(QStringLiteral("threshold"), diagnostic.threshold);
  record.insert(QStringLiteral("yaw_rad"), yawRad_);
  record.insert(QStringLiteral("linear_command_mps"), currentLinearMps_);
  record.insert(QStringLiteral("angular_command_radps"), currentAngularRadps_);
  record.insert(QStringLiteral("left_feedback_mps"), leftSpeedMps_);
  record.insert(QStringLiteral("right_feedback_mps"), rightSpeedMps_);
  record.insert(QStringLiteral("reacquiring"), reacquisitionPending_);
  record.insert(QStringLiteral("control_state_timing"), QStringLiteral("before_observation_update"));
  record.insert(QStringLiteral("image_span_m"), settings_.imageLateralSpanM);
  record.insert(QStringLiteral("lookahead_m"), settings_.laserCenterLookaheadM);
  record.insert(QStringLiteral("geometry_source"), QStringLiteral("assumed"));
  record.insert(QStringLiteral("track_width_m"), settings_.trackWidthM);
  record.insert(QStringLiteral("camera_lateral_sign"), kCameraLateralToVehicleSign);
  record.insert(QStringLiteral("baseline_support_ratio"), diagnostic.baselineSupportRatio);
  record.insert(QStringLiteral("baseline_residual_px"), diagnostic.baselineResidualPx);
  QJsonObject trajectoryPrior;
  trajectoryPrior.insert(QStringLiteral("valid"), seamPrediction_.valid);
  trajectoryPrior.insert(QStringLiteral("center_ratio"), seamPrediction_.absoluteCenterRatio);
  trajectoryPrior.insert(QStringLiteral("width_ratio"), seamPrediction_.absoluteWidthRatio);
  trajectoryPrior.insert(QStringLiteral("uncertainty_ratio"), seamPrediction_.uncertaintyRatio);
  trajectoryPrior.insert(QStringLiteral("confidence"), seamPrediction_.confidence);
  trajectoryPrior.insert(QStringLiteral("sample_count"), seamPrediction_.sampleCount);
  trajectoryPrior.insert(QStringLiteral("span_m"), seamPrediction_.observedSpanM);
  trajectoryPrior.insert(QStringLiteral("extrapolation_m"), seamPrediction_.extrapolationM);
  trajectoryPrior.insert(QStringLiteral("observation_age_ms"), seamPrediction_.observationAgeMs);
  trajectoryPrior.insert(QStringLiteral("role"), QStringLiteral("candidate_association_bounded_fallback"));
  record.insert(QStringLiteral("trajectory_prior"), trajectoryPrior);
  lastRawFrameSaveMs_ = now;
  rawFrameSavePending_ = true;
  rawFramePendingSessionId_ = trajectorySessionId_;
  rawFramePendingSequence_ = cameraFrameSequence_;
  emit rawFrameReady(trajectorySessionId_, cameraFrameSequence_, image,
      QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
}

void LaserCorrectionController::rawFrameSaved(
    quint64 sessionId, quint64 frameSequence, const QString& path,
    const QString& error) {
  if (!rawFrameSavePending_ || sessionId != rawFramePendingSessionId_ ||
      frameSequence != rawFramePendingSequence_) return;
  rawFrameSavePending_ = false;
  rawFramePendingSessionId_ = 0;
  rawFramePendingSequence_ = 0;
  // An earlier task's acknowledgement releases the one shared in-flight
  // slot, but its failure must not disable archiving for a newer task.
  if (sessionId == trajectorySessionId_ && !error.isEmpty()) {
    rawArchiveDisabled_ = true;
    emit logMessage(CRAWLING_TEXT("实时原图归档停止：%1；纠偏控制继续运行").arg(error));
  }
  emit diagnosticLogMessage(QStringLiteral(
      "event=laser_raw_snapshot session=%1 camera_seq=%2 result=%3 path=\"%4\" error=\"%5\"")
      .arg(sessionId).arg(frameSequence)
      .arg(error.isEmpty() ? QStringLiteral("OK") : QStringLiteral("FAILED"))
      .arg(path, error));
}

void LaserCorrectionController::processDriveTelemetry(
    const DriveTelemetry& telemetry) {
  const qint64 now = clock_.elapsed();
  if (!status_.active) return;

  // A queued telemetry callback can run before the delayed watchdog timer.
  // Never emit one more moving command using an already-expired image.
  if (stopIfImageTimedOut(now)) return;

  if (telemetry.state != DriveState::Enabled) {
    stop(CRAWLING_TEXT("底盘已离开使能状态，自动纠偏已停止"));
    return;
  }
  if (!telemetryUsable(telemetry)) {
    haveTelemetry_ = false;
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
  return status_.active;
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
      !status_.gapValid || !boundaryObservationReliable_ ||
      lastValidDetectionMs_ < enabledAtMs_ ||
      lastTelemetryMs_ < enabledAtMs_ || !wheelsStopped()) {
    return;
  }
  beginInitialSurvey();
}

void LaserCorrectionController::beginInitialSurvey() {
  samples_.clear();
  visualSamples_.clear();
  guidanceSamples_.clear();
  guidanceTravelSamples_.clear();
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

void LaserCorrectionController::beginTrackingSegment() {
  const bool firstTrackingSegment = phase_ != Phase::TrackingForward;
  if (firstTrackingSegment) {
    guidanceSamples_.clear();
    guidanceTravelSamples_.clear();
    trackingTravelM_ = 0.0;
    lastGuidanceLongitudinalM_ = -1.0;
    // The tracking objective is the optical center of the full laser axis,
    // not the vehicle's possibly already-offset position at the end of the
    // survey. Store the same target for status and for non-tracking detector
    // phases; tracking itself uses previous-frame continuity while returning.
    trackingReferenceGapCenterRatio_ = 0.5;
    trackingReferenceGapAbsoluteCenterRatio_ = 0.5;
    referenceGapAbsoluteCenterRatio_ = 0.5;
    status_.referenceGapAbsoluteCenterRatio =
        trackingReferenceGapAbsoluteCenterRatio_;
    // The survey/return is only for finding the path. Do not carry its
    // transient center drift into the live correction loop.
    filteredGapLateralM_ = status_.gapLateralM;
    filteredGapValid_ = status_.gapValid;
    lastGapFilterMs_ = clock_.elapsed();
    centerRecoveryWindowActive_ = false;
    centerRecoveryProtectionLogged_ = false;
    centerResponseUntrusted_ = false;
    lastCenterTrendErrorM_ = status_.gapLateralM;
    centerErrorDeltaM_ = 0.0;
    filteredCenterErrorRateMps_ = 0.0;
    filteredSignedCenterRateMps_ = 0.0;
    centerErrorTrendValid_ = false;
    lastCenterTrendSampleMs_ = clock_.elapsed();
    emit logMessage(
        CRAWLING_TEXT("跟踪横向目标已设为激光全幅中心：线内 %1%，全图 %2%，"
                      "实时缺口将持续向中心收敛")
            .arg(trackingReferenceGapCenterRatio_ * 100.0, 0, 'f', 2)
            .arg(trackingReferenceGapAbsoluteCenterRatio_ * 100.0, 0, 'f', 2));
  }
  samples_.clear();
  visualSamples_.clear();
  segmentStartYawRad_ = yawRad_;
  resetPhaseTravel();
  angularLimitLogged_ = false;
  lastControlDiagnosticMs_ = -1;
  steeringMismatchCount_ = 0;
  if (firstTrackingSegment) {
    centerControlDirection_ = 0;
    pendingCenterControlDirection_ = 0;
    pendingCenterControlDirectionCount_ = 0;
    lastCenterDirectionCameraSequence_ = 0;
  }
  lastSampleLongitudinalM_ = -1.0;
  lastVisualLongitudinalM_ = -1.0;
  // trackingTravelM_ and lastGuidanceLongitudinalM_ deliberately survive a
  // segment transition. guidanceSamples_ is one rolling cloud in a common
  // distance coordinate; resetting either value stacks different segments on
  // top of one another and creates false path slopes.
  status_.segmentProgressM = 0.0;
  status_.collectedSamples = 0;
  setPhase(Phase::TrackingForward,
           CRAWLING_TEXT("柔性纠偏前进并采集下一段"));
}

void LaserCorrectionController::resetGapTracker() {
  seamTrajectory_.clear();
  seamPrediction_ = {};
  gapTrackerValid_ = false;
  trackedGapHorizontal_ = true;
  trackedGapCenterRatio_ = 0.5;
  trackedGapWidthRatio_ = 0.0;
  trackedGapAbsoluteCenterRatio_ = 0.5;
  referenceGapAbsoluteCenterRatio_ = -1.0;
  trackedLineStartRatio_ = -1.0;
  trackedLineEndRatio_ = -1.0;
  trackedGapConfidence_ = 0.0;
  lastTrackedGapMs_ = -1;
  lastTwoEdgeDetectionMs_ = -1;
  detectionHeld_ = false;
  detectionEdgeBreakFallback_ = false;
  trajectoryPredictionActive_ = false;
  filteredGapLateralM_ = 0.0;
  filteredGapValid_ = false;
  lastGapFilterMs_ = -1;
  filteredLeftEdgeM_ = 0.0;
  filteredRightEdgeM_ = 0.0;
  filteredEdgeValid_ = false;
  trackingReferenceGapCenterRatio_ = -1.0;
  trackingReferenceGapAbsoluteCenterRatio_ = -1.0;
  status_.referenceGapAbsoluteCenterRatio = 0.5;
  status_.edgeBreakFallback = false;
  status_.contourFallback = false;
}

void LaserCorrectionController::beginSeamReacquisition(const QString& reason) {
  if (!status_.active || !gapTrackerValid_ || reacquisitionPending_) return;
  reacquisitionPending_ = true;
  centerResponseUntrusted_ = true;
  centerRecoveryWindowActive_ = false;
  initialGapConfirmationCount_ = 0;
  // Return images locate the seam for resuming, not for the completed
  // survey. Reacquiring on the way back must not erase that frozen cloud.
  const bool preserveSurvey = phase_ == Phase::PauseBeforeReturn ||
                              phase_ == Phase::SurveyReturn ||
                              phase_ == Phase::PauseBeforeTracking;
  const int previousSampleCount = samples_.size();
  if (!preserveSurvey) {
    samples_.clear();
    lastSampleLongitudinalM_ = -1.0;
    status_.collectedSamples = 0;
  }
  guidanceSamples_.clear();
  guidanceTravelSamples_.clear();
  lastGuidanceLongitudinalM_ = -1.0;
  lastGuidanceFitMs_ = -1;
  lastAcceptedFitDirection_ = 0;
  pendingFitDirection_ = 0;
  pendingFitDirectionCount_ = 0;
  desiredHeadingRad_ = yawRad_;
  centerErrorTrendValid_ = false;
  filteredSignedCenterRateMps_ = 0.0;
  lastCenterTrendSampleMs_ = -1;
  emit logMessage(CRAWLING_TEXT("焊缝重新定位：%1；%2，双边缺口三帧或无冲突凸起轮廓五帧确认后更新实时中心")
                      .arg(reason)
                      .arg(preserveSurvey
                               ? CRAWLING_TEXT("保留首段已采集点云并保持当前返回/停稳流程")
                               : CRAWLING_TEXT("清除当前旧拟合，保持当前运动流程")));
  emit diagnosticLogMessage(
      QStringLiteral("event=seam_reacquisition session=%1 camera_seq=%2 phase=%3 last_real_age_ms=%4 previous_center_ratio=%5 yaw_rad=%6 survey_preserved=%7 samples_before=%8 samples_after=%9")
          .arg(trajectorySessionId_).arg(cameraFrameSequence_).arg(phaseName(phase_))
          .arg(lastTwoEdgeDetectionMs_ < 0 ? -1 : clock_.elapsed() - lastTwoEdgeDetectionMs_)
          .arg(trackedGapAbsoluteCenterRatio_, 0, 'f', 6).arg(yawRad_, 0, 'f', 6)
          .arg(preserveSurvey ? 1 : 0).arg(previousSampleCount).arg(samples_.size()));
}

void LaserCorrectionController::updateCenterErrorTrend(qint64 now) {
  if (phase_ != Phase::TrackingForward ||
      trackingReferenceGapAbsoluteCenterRatio_ < 0.0 || detectionHeld_ ||
      detectionEdgeBreakFallback_) {
    return;
  }
  const double referenceLateralM =
      kCameraLateralToVehicleSign *
      (trackingReferenceGapAbsoluteCenterRatio_ - 0.5) *
      settings_.imageLateralSpanM;
  const double currentErrorM = status_.gapLateralM - referenceLateralM;
  if (lastCenterTrendSampleMs_ >= 0 && now > lastCenterTrendSampleMs_ &&
      now - lastCenterTrendSampleMs_ <= 250) {
    const double elapsedSeconds =
        clamp((now - lastCenterTrendSampleMs_) / 1000.0, 0.001, 0.5);
    centerErrorDeltaM_ =
        std::abs(currentErrorM) - std::abs(lastCenterTrendErrorM_);
    const double rawRateMps = centerErrorDeltaM_ / elapsedSeconds;
    const double signedRateMps = clamp(
        (currentErrorM - lastCenterTrendErrorM_) / elapsedSeconds, -0.08, 0.08);
    const double alpha =
        1.0 - std::exp(-elapsedSeconds / kCenterTrendFilterTimeConstantS);
    if (!centerErrorTrendValid_) {
      filteredCenterErrorRateMps_ = rawRateMps;
      filteredSignedCenterRateMps_ = signedRateMps;
      centerErrorTrendValid_ = true;
    } else {
      filteredCenterErrorRateMps_ +=
          alpha * (rawRateMps - filteredCenterErrorRateMps_);
      filteredSignedCenterRateMps_ +=
          alpha * (signedRateMps - filteredSignedCenterRateMps_);
    }
  } else {
    centerErrorTrendValid_ = false;
    filteredSignedCenterRateMps_ = 0.0;
    filteredCenterErrorRateMps_ = 0.0;
  }
  lastCenterTrendErrorM_ = currentErrorM;
  lastCenterTrendSampleMs_ = now;
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
  const double yawDeltaRad =
      (meanLeftMps - meanRightMps) /
      settings_.trackWidthM * deltaSeconds;
  const double forwardDeltaM =
      (meanLeftMps + meanRightMps) * 0.5 * deltaSeconds;
  const double middleYawRad = yawRad_ + yawDeltaRad * 0.5;
  odometryLongitudinalM_ += std::cos(middleYawRad) * forwardDeltaM;
  odometryLateralM_ += std::sin(middleYawRad) * forwardDeltaM;
  yawRad_ = normalizedAngle(yawRad_ + yawDeltaRad);
  if (phase_ == Phase::TrackingForward && forwardDeltaM > 0.0) {
    trackingTravelM_ += forwardDeltaM;
  }

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
  if (stopIfScanBoundaryUnsafe(now)) return;

  // Boundary containment changes the steering command, but it must not
  // bypass the mandatory initial survey. The first pass is always one full
  // forward sample followed by a settled reverse to the start point; only
  // after that pass may the rolling tracking phase begin. applyCommand()
  // enforces the inward turn while either survey leg is moving, and the
  // stop gate still handles a genuinely lost/out-of-range observation.

  switch (phase_) {
    case Phase::Idle:
    case Phase::AwaitingInputs:
      commandImmediateStop(status_.reason);
      return;

    case Phase::SurveyForward: {
      const double progress = phaseTravelM_;
      status_.segmentProgressM = clamp(progress, 0.0, settings_.segmentLengthM);
      if (progress >= settings_.segmentLengthM) {
        surveyTravelM_ = progress;
        commandImmediateStop(CRAWLING_TEXT("首段采集完成，等待车体停稳"));
        setPhase(Phase::PauseBeforeReturn,
                 CRAWLING_TEXT("首段采集完成，等待车体停稳"));
        return;
      }
      const double targetLinear = updateForwardSpeedSupervisor(
          now, deltaSeconds, 1.0);
      applyCommand(targetLinear, 0.0, deltaSeconds);
      if (!status_.active) return;
      if (!status_.gapValid) {
        status_.reason = CRAWLING_TEXT(
            "首段缺口暂时无效，按速度监督器连续前进并等待原始图恢复");
        return;
      }
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
          // Exactly one initial forward/return pass. A missing heading fit
          // must not disable the independent live image-center feedback.
          desiredHeadingRad_ = yawRad_;
          lastGuidanceFitMs_ = -1;
          emit logMessage(CRAWLING_TEXT("首段拟合未通过，不重复往返：%1；停稳后以实时缺口回中，前进中重新积累轨迹")
                              .arg(fitError));
          emit diagnosticLogMessage(
              QStringLiteral("event=initial_survey_complete fit_valid=0 samples=%1 travel_m=%2 next=live_tracking retry=0")
                  .arg(samples_.size()).arg(surveyTravelM_, 0, 'f', 6));
          setPhase(Phase::PauseBeforeTracking,
                   CRAWLING_TEXT("首段拟合不足，等待停稳后进入实时纠偏"));
          return;
        }
        emit diagnosticLogMessage(
            QStringLiteral("event=initial_survey_complete fit_valid=1 samples=%1 travel_m=%2 next=live_tracking retry=0")
                .arg(samples_.size()).arg(surveyTravelM_, 0, 'f', 6));
        setPhase(Phase::PauseBeforeTracking,
                 CRAWLING_TEXT("路径拟合完成，等待车体停稳"));
        return;
      }
      applyCommand(-settings_.targetSpeedMps, 0.0, deltaSeconds);
      if (!status_.active) return;
      status_.reason = runningReason();
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
        const double targetLinear = updateForwardSpeedSupervisor(
            now, deltaSeconds, 1.0);
        applyCommand(targetLinear, 0.0, deltaSeconds);
        if (!status_.active) return;
        status_.reason = CRAWLING_TEXT(
            "实时缺口暂无可用中心，短时使用最近实测回中；持续失去有效观测时停止");
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

      const double referenceGapAbsoluteCenterRatio =
          trackingReferenceGapAbsoluteCenterRatio_ >= 0.0
              ? trackingReferenceGapAbsoluteCenterRatio_
              : status_.gapAbsoluteCenterRatio;
      const double referenceLateralM =
          kCameraLateralToVehicleSign *
          (referenceGapAbsoluteCenterRatio - 0.5) *
          settings_.imageLateralSpanM;
      const double rawCenterErrorM =
          status_.gapLateralM - referenceLateralM;
      const double centerErrorM =
          (filteredGapValid_ ? filteredGapLateralM_
                             : status_.gapLateralM) -
          referenceLateralM;
      // The camera-center loop is the fast feedback path. A normal gap gets
      // full authority; an edge-break or a reused frame only nudges the
      // vehicle while the rolling point-cloud fit remains the slow path.
      const double centerConfidenceWeight = clamp(
          status_.confidence / kFullCenterFeedbackConfidence,
          0.0, 1.0);
      // A complete raw-image gap is the fast feedback path. A broken edge is
      // only an inferred center and must not command the same turn authority.
      const double edgeBreakWeight = detectionEdgeBreakFallback_ ? 0.45 : 1.0;
      const double centerHoldWeight = detectionHeld_
                                          ? (trajectoryPredictionActive_
                                                 ? 0.35
                                                 : kHeldCenterFeedbackWeight * clamp(
                                                       1.0 - (now - lastValidDetectionMs_) / 1000.0,
                                                       0.0, 1.0))
                                          : 1.0;
      const double centerErrorMagnitudeM = std::abs(centerErrorM);
      status_.laserCenterErrorM = centerErrorM;
      const bool centerDiverging =
          centerErrorTrendValid_ && !detectionHeld_ && !detectionEdgeBreakFallback_ &&
          filteredCenterErrorRateMps_ >= kCenterDivergingRateMps &&
          centerErrorMagnitudeM >= kCenterPriorityStartM;
      const bool centerConverging =
          centerErrorTrendValid_ && !detectionHeld_ && !detectionEdgeBreakFallback_ &&
          filteredCenterErrorRateMps_ <= kCenterConvergingRateMps;
      const bool recoveryWatchdogUsable =
          centerErrorMagnitudeM >= kCenterRecoveryWatchdogErrorM &&
          !detectionHeld_ && (!detectionEdgeBreakFallback_ || status_.contourFallback) && !reacquisitionPending_ &&
          centerConfidenceWeight >= 0.50;
      if (recoveryWatchdogUsable) {
        if (!centerRecoveryWindowActive_) {
          centerRecoveryWindowActive_ = true;
          centerRecoveryWindowStartTravelM_ = trackingTravelM_;
          centerRecoveryWindowStartYawRad_ = yawRad_;
          centerRecoveryWindowStartErrorM_ = centerErrorMagnitudeM;
          centerRecoveryBestErrorM_ = centerErrorMagnitudeM;
        } else {
          centerRecoveryBestErrorM_ =
              std::min(centerRecoveryBestErrorM_, centerErrorMagnitudeM);
        }
        const double recoveryTravelM =
            trackingTravelM_ - centerRecoveryWindowStartTravelM_;
        const double recoveryYawRad = std::abs(normalizedAngle(
            yawRad_ - centerRecoveryWindowStartYawRad_));
        // Confirm improvement against the current filtered position, not a
        // single best pixel in the window. A re-acquired real gap need not
        // spend a whole 60 mm segment at reduced authority before recovering.
        if (centerResponseUntrusted_ && recoveryTravelM >= 0.005 &&
            centerRecoveryWindowStartErrorM_ - centerErrorMagnitudeM >=
                kCenterRecoveryMinimumImprovementM) {
          centerResponseUntrusted_ = false;
          centerRecoveryProtectionLogged_ = false;
          emit logMessage(CRAWLING_TEXT("重新定位后实测偏差持续改善，恢复正常转向权重，输出仍按加速度平滑变化"));
        }
        if (recoveryTravelM >= kCenterRecoveryWatchdogTravelM ||
            recoveryYawRad >= kCenterResponseWindowYawRad) {
          const double improvementM =
              centerRecoveryWindowStartErrorM_ - centerErrorMagnitudeM;
          if (improvementM < kCenterRecoveryMinimumImprovementM) {
            centerResponseUntrusted_ = true;
            if (!centerRecoveryProtectionLogged_) {
              centerRecoveryProtectionLogged_ = true;
              beginSeamReacquisition(CRAWLING_TEXT("转向后缺口未向中心改善"));
              emit logMessage(
                  CRAWLING_TEXT("回中保护：已前进 %1 mm，但缺口偏差仅改善 %2 mm，"
                                "当前偏差 %3 mm；重新定位主激光基线，短时沿最近实测回中；持续无有效观测时停止")
                      .arg(recoveryTravelM * 1000.0, 0, 'f', 1)
                      .arg(improvementM * 1000.0, 0, 'f', 1)
                      .arg(centerErrorMagnitudeM * 1000.0, 0, 'f', 1));
            }
          } else if (centerResponseUntrusted_) {
            centerResponseUntrusted_ = false;
            centerRecoveryProtectionLogged_ = false;
            emit logMessage(
                CRAWLING_TEXT("回中趋势恢复：最近 %1 mm 偏差改善 %2 mm，"
                              "保持设置速度并平滑恢复正常曲率")
                    .arg(recoveryTravelM * 1000.0, 0, 'f', 1)
                    .arg(improvementM * 1000.0, 0, 'f', 1));
          }
          centerRecoveryWindowStartTravelM_ = trackingTravelM_;
          centerRecoveryWindowStartYawRad_ = yawRad_;
          centerRecoveryWindowStartErrorM_ = centerErrorMagnitudeM;
          centerRecoveryBestErrorM_ = centerErrorMagnitudeM;
        }
      } else if (!reacquisitionPending_ && !detectionHeld_ &&
                 (!detectionEdgeBreakFallback_ || status_.contourFallback) &&
                 centerErrorMagnitudeM < kCenterRecoveryWatchdogErrorM) {
        centerRecoveryWindowActive_ = false;
        centerRecoveryProtectionLogged_ = false;
        centerResponseUntrusted_ = false;
      }
      const double centerFeedbackWeight =
          centerConfidenceWeight * edgeBreakWeight * centerHoldWeight *
          (centerResponseUntrusted_ ? 0.15 : 1.0);
      const double unconstrainedCenterCorrectionRad =
          settings_.laserCenterFeedbackGain *
          centerFeedbackWeight *
          std::atan2(centerErrorM, settings_.laserCenterLookaheadM);
      const double centerHeadingCorrectionRad = clamp(
          unconstrainedCenterCorrectionRad, -kMaximumCenterCorrectionRad,
          kMaximumCenterCorrectionRad);
      const double headingError = normalizedAngle(
          desiredHeadingRad_ - yawRad_);
      const double measuredYawRateRadps =
          (leftSpeedMps_ - rightSpeedMps_) / settings_.trackWidthM;
      // Interpret the configured alpha at the nominal 20 ms control period;
      // the physical filter time constant then survives callback jitter.
      const double yawFilterAlpha =
          1.0 - std::pow(1.0 - settings_.derivativeAlpha, deltaSeconds / 0.020);
      filteredYawRateRadps_ +=
          yawFilterAlpha *
          (measuredYawRateRadps - filteredYawRateRadps_);
      status_.headingErrorRad = headingError;
      status_.laserCenterCorrectionRad = centerHeadingCorrectionRad;

      // The fitted path updates only at segment boundaries. Use the current
      // raw-image gap as a separate fast lateral loop so a large offset
      // starts turning immediately instead of waiting for another fit. A
      // small deadband prevents one-pixel noise from reversing the wheels.
      // A short signed prediction unwinds steering before the seam crosses
      // the optical center. Invalid/inferred detections cannot drive this term.
      const bool usableTrend = centerErrorTrendValid_ && !detectionHeld_ &&
                               !detectionEdgeBreakFallback_;
      const double predictedCenterErrorM = centerErrorM +
          (usableTrend ? clamp(filteredSignedCenterRateMps_ * 0.20, -0.008, 0.008) : 0.0);
      double centerErrorForControl =
          std::abs(predictedCenterErrorM) <= kLaserCenterDeadbandM
              ? 0.0
              : predictedCenterErrorM;
      // Do not let one-pixel quantization reverse the differential pair while
      // the weld is already within a few millimetres of the optical center.
      // A large opposite error is still accepted immediately; only the small
      // sign changes that caused the observed left/right chatter are held for
      // two fresh frames and allowed to decay toward zero in the meantime.
      const bool freshDirectionFrame = !detectionHeld_ && !reacquisitionPending_ &&
          cameraFrameSequence_ != lastCenterDirectionCameraSequence_;
      if (freshDirectionFrame) lastCenterDirectionCameraSequence_ = cameraFrameSequence_;
      if (centerFeedbackWeight > 0.05 && centerErrorForControl != 0.0) {
        const int requestedDirection = centerErrorForControl > 0.0 ? 1 : -1;
        if (centerControlDirection_ == 0) {
          centerControlDirection_ = requestedDirection;
          pendingCenterControlDirection_ = 0;
          pendingCenterControlDirectionCount_ = 0;
        } else if (requestedDirection != centerControlDirection_ &&
                   std::abs(centerErrorForControl) < kCenterReversalReleaseM) {
          if (freshDirectionFrame) {
            if (pendingCenterControlDirection_ == requestedDirection) {
              ++pendingCenterControlDirectionCount_;
            } else {
              pendingCenterControlDirection_ = requestedDirection;
              pendingCenterControlDirectionCount_ = 1;
            }
          }
          if (pendingCenterControlDirectionCount_ <
              kCenterReversalConfirmationFrames) {
            centerErrorForControl = 0.0;
          } else {
            centerControlDirection_ = requestedDirection;
            pendingCenterControlDirection_ = 0;
            pendingCenterControlDirectionCount_ = 0;
          }
        } else if (requestedDirection != centerControlDirection_) {
          centerControlDirection_ = requestedDirection;
          pendingCenterControlDirection_ = 0;
          pendingCenterControlDirectionCount_ = 0;
        } else {
          pendingCenterControlDirection_ = 0;
          pendingCenterControlDirectionCount_ = 0;
        }
      } else if (std::abs(predictedCenterErrorM) <=
                 kCenterReversalHysteresisM) {
        pendingCenterControlDirection_ = 0;
        pendingCenterControlDirectionCount_ = 0;
      }
      const double centerAngularTarget =
          settings_.laserCenterAngularGain * centerFeedbackWeight *
          std::max(0.0, currentLinearMps_) * 2.0 * centerErrorForControl /
          std::max(0.0001, settings_.laserCenterLookaheadM * settings_.laserCenterLookaheadM +
                           centerErrorForControl * centerErrorForControl);
      // The image error is already time-filtered. Apply the acceleration
      // limiter once, after fusion in applyCommand(); a second stored ramp
      // here keeps an old turn alive after the seam begins crossing center.
      filteredCenterAngularRadps_ = clamp(
          centerAngularTarget, -kLaserCenterAngularLimitRadps,
          kLaserCenterAngularLimitRadps);
      if (detectionHeld_ || detectionEdgeBreakFallback_ || centerResponseUntrusted_) {
        const double authorityLimit = std::abs(centerAngularTarget);
        filteredCenterAngularRadps_ = clamp(filteredCenterAngularRadps_,
                                            -authorityLimit, authorityLimit);
      }

      const double controlledError = headingError;
      // Use the measured raw-pixel envelope inside the actual laser span.
      // A clipped span means less information is available for guidance.
      const double laserRangeMargin = laserBoundaryMargin(now);
      const bool boundaryRecoveryActive = boundaryContainmentActive_;
      const double targetLinear = updateForwardSpeedSupervisor(
          now, deltaSeconds, 1.0);
      const double guidanceFreshness = lastGuidanceFitMs_ < 0 ? 0.0 :
          clamp(1.0 - (now - lastGuidanceFitMs_) / 6000.0, 0.0, 1.0);
      const double proportionalTerm = targetLinear *
          settings_.maximumCurvatureRadPerM * 0.35 * guidanceFreshness *
          std::tanh(settings_.proportionalGain * controlledError);
      // Wheel-speed yaw rate is considerably less noisy than differentiating
      // a heading estimate delivered at about 3 Hz. Opposing it provides
      // damping as the chassis approaches the desired heading.
      const double derivativeTerm =
          -settings_.derivativeGain * filteredYawRateRadps_;
      // The point-cloud fit is a slow heading loop, while the raw-image center
      // is the fast containment loop. If they disagree while the gap is far
      // from the optical center, progressively suppress only the opposing
      // heading component. Same-direction guidance is retained, and full
      // point-cloud authority returns as the gap approaches the center.
      const double headingLoopAngular = proportionalTerm;
      const double centerPriority = centerFeedbackWeight * clamp(
          (std::abs(centerErrorM) - kCenterPriorityStartM) /
              (kCenterPriorityFullM - kCenterPriorityStartM),
          0.0, 1.0);
      double effectiveHeadingAngular = headingLoopAngular;
      const bool fusionConflict =
          centerAngularTarget != 0.0 &&
          headingLoopAngular * centerAngularTarget < 0.0;
      if (fusionConflict) {
        effectiveHeadingAngular *= 1.0 - centerPriority;
        const double maximumOpposingHeading =
            std::abs(headingLoopAngular) * (1.0 - centerPriority) +
            std::abs(centerAngularTarget) * kMaximumOpposingHeadingFraction;
        effectiveHeadingAngular = clamp(effectiveHeadingAngular,
                                        -maximumOpposingHeading,
                                        maximumOpposingHeading);
      }
      const int centerReturnDirection = boundaryRecoveryActive
                                            ? boundaryReturnDirection(now)
                                            : centerErrorForControl > 0.0
                                            ? 1
                                            : (centerErrorForControl < 0.0
                                                   ? -1
                                                   : 0);
      const bool containmentActive =
          centerReturnDirection != 0 &&
          boundaryRecoveryActive && boundaryObservationReliable_;
      if (containmentActive &&
          effectiveHeadingAngular * centerReturnDirection < 0.0) {
        effectiveHeadingAngular = 0.0;
      }
      double centerAngularForFusion = filteredCenterAngularRadps_;
      if (containmentActive &&
          centerAngularForFusion * centerReturnDirection < 0.0) {
        centerAngularForFusion = 0.0;
      }
      double rawTargetAngular =
          effectiveHeadingAngular + centerAngularForFusion + derivativeTerm;
      if (!containmentActive &&
          std::abs(centerErrorForControl) <= kLaserCenterDeadbandM &&
          std::abs(rawTargetAngular) < kSteeringDeadbandRadps) {
        rawTargetAngular = 0.0;
      }
      const double containmentMinimum = kMinimumContainmentAngularRadps * clamp(
          (kScanContainmentReleaseRatio - laserRangeMargin) /
              (kScanContainmentReleaseRatio - kScanStopMarginRatio), 0.0, 1.0);
      if (containmentActive &&
          rawTargetAngular * centerReturnDirection < containmentMinimum) {
        rawTargetAngular =
            centerReturnDirection * containmentMinimum;
      }
      const double sameDirectionAngularLimit =
          (2.0 * std::abs(targetLinear) / settings_.trackWidthM) *
          ((1.0 - settings_.minimumInnerWheelRatio) /
           (1.0 + settings_.minimumInnerWheelRatio));
      // Increase the allowable curvature only while the live gap is clearly
      // displaced. This gives a large error a quicker but still rate-limited
      // return arc; as the gap approaches its reference, the limit falls
      // back to the nominal gentle curvature automatically.
      const double offsetRatio = clamp(
          std::abs(centerErrorM) / kLaserCenterTurnErrorM, 0.0, 1.0);
      double adaptiveCurvature =
          settings_.maximumCurvatureRadPerM +
          (kLargeOffsetCurvatureRadPerM -
           settings_.maximumCurvatureRadPerM) * offsetRatio;
      if (containmentActive) {
        adaptiveCurvature =
            std::max(adaptiveCurvature, kBoundaryRecoveryCurvatureRadPerM);
      }
      const double safeAngularLimit = std::min(
          {settings_.maxAngularRadps,
           std::max(1e-6, sameDirectionAngularLimit),
           containmentActive ? settings_.maxAngularRadps
                             : std::max(1e-6, std::abs(targetLinear) *
                                                 adaptiveCurvature)});
      if (!angularLimitLogged_ &&
          std::abs(rawTargetAngular) > safeAngularLimit + 1e-9) {
        emit logMessage(
            CRAWLING_TEXT("柔性转向限幅：原始角速度 %1 deg/s，安全上限 %2 deg/s，"
                          "前进速度 %3 mm/s，曲率上限 %4 deg/m")
                .arg(rawTargetAngular * 180.0 / kPi, 0, 'f', 1)
                .arg(safeAngularLimit * 180.0 / kPi, 0, 'f', 1)
                .arg(targetLinear * 1000.0, 0, 'f', 1)
                .arg(adaptiveCurvature * 180.0 / kPi, 0, 'f', 1));
        angularLimitLogged_ = true;
      }
      const double targetAngular = clamp(rawTargetAngular, -safeAngularLimit,
                                         safeAngularLimit);
      applyCommand(targetLinear, targetAngular, deltaSeconds,
                   adaptiveCurvature);
      if (!status_.active) return;
      // Do not store a large unreachable steering demand behind the output
      // limiter: it would delay braking even after the image error disappears.
      filteredCenterAngularRadps_ = clamp(filteredCenterAngularRadps_,
                                          -safeAngularLimit, safeAngularLimit);
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
        QJsonObject controlRecord;
        controlRecord.insert(QStringLiteral("session"), QString::number(trajectorySessionId_));
        controlRecord.insert(QStringLiteral("camera_seq"), QString::number(cameraFrameSequence_));
        controlRecord.insert(QStringLiteral("sdk_frame"), sourceFrameNumberValid_ ? QString::number(sourceFrameNumber_) : QStringLiteral("unknown"));
        controlRecord.insert(QStringLiteral("clock_ms"), static_cast<double>(now));
        controlRecord.insert(QStringLiteral("dt_s"), deltaSeconds);
        controlRecord.insert(QStringLiteral("image_age_ms"), static_cast<double>(now - lastImageMs_));
        controlRecord.insert(QStringLiteral("real_gap_age_ms"), static_cast<double>(lastTwoEdgeDetectionMs_ < 0 ? -1 : now - lastTwoEdgeDetectionMs_));
        controlRecord.insert(QStringLiteral("raw_error_m"), rawCenterErrorM);
        controlRecord.insert(QStringLiteral("filtered_error_m"), centerErrorM);
        controlRecord.insert(QStringLiteral("predicted_error_m"), predictedCenterErrorM);
        controlRecord.insert(QStringLiteral("boundary_envelope_margin_ratio"), laserRangeMargin);
        controlRecord.insert(QStringLiteral("boundary_priority"), containmentActive);
        controlRecord.insert(QStringLiteral("boundary_measurement_age_ms"),
                             static_cast<double>(now - boundaryObservationMs_));
        controlRecord.insert(QStringLiteral("boundary_line_start_ratio"), boundaryLineStartRatio_);
        controlRecord.insert(QStringLiteral("boundary_line_end_ratio"), boundaryLineEndRatio_);
        controlRecord.insert(QStringLiteral("contour_fallback"), status_.contourFallback);
        controlRecord.insert(QStringLiteral("center_direction_confirmation_frames"),
                             pendingCenterControlDirectionCount_);
        controlRecord.insert(QStringLiteral("signed_error_rate_mps"), filteredSignedCenterRateMps_);
        controlRecord.insert(QStringLiteral("center_weight"), centerFeedbackWeight);
        controlRecord.insert(QStringLiteral("center_target_radps"), centerAngularTarget);
        controlRecord.insert(QStringLiteral("heading_term_radps"), effectiveHeadingAngular);
        controlRecord.insert(QStringLiteral("yaw_damping_radps"), derivativeTerm);
        controlRecord.insert(QStringLiteral("angular_limit_radps"), safeAngularLimit);
        controlRecord.insert(QStringLiteral("linear_output_mps"), currentLinearMps_);
        controlRecord.insert(QStringLiteral("angular_output_radps"), currentAngularRadps_);
        controlRecord.insert(QStringLiteral("held"), detectionHeld_);
        controlRecord.insert(QStringLiteral("inferred"), detectionEdgeBreakFallback_);
        controlRecord.insert(QStringLiteral("reacquiring"), reacquisitionPending_);
        controlRecord.insert(QStringLiteral("response_untrusted"), centerResponseUntrusted_);
        controlRecord.insert(QStringLiteral("trajectory_prediction"), trajectoryPredictionActive_);
        controlRecord.insert(QStringLiteral("requested_speed_mps"), settings_.targetSpeedMps);
        controlRecord.insert(QStringLiteral("speed_supervisor_scale"), speedSupervisorScale_);
        controlRecord.insert(QStringLiteral("speed_supervisor_boundary_scale"), speedSupervisorBoundaryScale_);
        controlRecord.insert(QStringLiteral("speed_supervisor_observation_scale"), speedSupervisorObservationScale_);
        controlRecord.insert(QStringLiteral("speed_supervisor_observation_age_ms"),
                             static_cast<double>(speedSupervisorObservationAgeMs_));
        controlRecord.insert(QStringLiteral("speed_supervisor_target_mps"), targetLinear);
        controlRecord.insert(QStringLiteral("speed_ramped_output_mps"), currentLinearMps_);
        controlRecord.insert(QStringLiteral("speed_feedback_mps"),
                             (leftSpeedMps_ + rightSpeedMps_) * 0.5);
        controlRecord.insert(QStringLiteral("speed_supervisor_reason"), speedSupervisorReason_);
        emit diagnosticLogMessage(QStringLiteral("event=laser_control_output json=%1")
            .arg(QString::fromUtf8(QJsonDocument(controlRecord).toJson(QJsonDocument::Compact))));
        emit logMessage(
            CRAWLING_TEXT("纠偏控制 #%1：周期 %2，进度 %3 mm，期望航向 %4°，"
                          "估算航向 %5°，误差 %6°，轮速角速度 %7 deg/s，"
                          "P %8，D %9，原始角速度 %10，限幅 %11，输出 %12 deg/s，"
                          "线速度 %13 mm/s，底盘应用 %14 mm/s/%15 deg/s，"
                          "轮速目标 %16/%17 mm/s，反馈 %18/%19 mm/s，换向 %20 次，"
                          "激光中心(线内) %21%，图像中心 %22%，"
                          "横向基准(全图) %23%，车体横向偏差 %24 mm，"
                          "滤波相对偏差 %25 mm，中心修正 %26°，"
                          "实时转向 %27 deg/s，实时权重 %28%，中心优先 %29%，"
                          "航向慢环 %30/%31 deg/s，沿用 %32，缺口模式 %33，"
                          "边界余量 %34%，图像-轮速时间差 %35 ms")
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
                 .arg(angularDirectionChangeCount_)
                 .arg(status_.gapCenterRatio * 100.0, 0, 'f', 2)
                 .arg(status_.gapAbsoluteCenterRatio * 100.0, 0, 'f', 2)
                 .arg(referenceGapAbsoluteCenterRatio * 100.0, 0, 'f', 2)
                 .arg(rawCenterErrorM * 1000.0, 0, 'f', 1)
                .arg(status_.laserCenterErrorM * 1000.0, 0, 'f', 1)
                .arg(centerHeadingCorrectionRad * 180.0 / kPi, 0, 'f', 2)
                .arg(filteredCenterAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                .arg(centerFeedbackWeight * 100.0, 0, 'f', 1)
                .arg(centerPriority * 100.0, 0, 'f', 1)
                .arg(headingLoopAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(effectiveHeadingAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(detectionHeld_ ? CRAWLING_TEXT("是")
                                     : CRAWLING_TEXT("否"))
                .arg(status_.contourFallback
                         ? CRAWLING_TEXT("轮廓辅助")
                         : (detectionEdgeBreakFallback_
                                ? CRAWLING_TEXT("边缘推断")
                                : CRAWLING_TEXT("双边缘原始图")))
                .arg(laserRangeMargin * 100.0, 0, 'f', 1)
                .arg(lastTelemetryMs_ >= 0 ? now - lastTelemetryMs_ : -1));
        const QString detectionSource =
            detectionHeld_
                ? CRAWLING_TEXT("上一帧中心预测+连续拼接轨迹")
                : (status_.contourFallback
                       ? CRAWLING_TEXT("当前原始图轮廓辅助+连续拼接轨迹")
                       : (detectionEdgeBreakFallback_
                              ? CRAWLING_TEXT("当前原始图边缘推断+连续拼接轨迹")
                              : CRAWLING_TEXT("当前原始图双边缘+连续拼接轨迹")));
        const QString centerTrend =
            centerDiverging
                ? CRAWLING_TEXT("远离中心")
                : (centerConverging ? CRAWLING_TEXT("向中心收敛")
                                    : CRAWLING_TEXT("基本稳定/样本不足"));
        const double mixedLeftMps =
            currentLinearMps_ + currentAngularRadps_ * settings_.trackWidthM * 0.5;
        const double mixedRightMps =
            currentLinearMps_ - currentAngularRadps_ * settings_.trackWidthM * 0.5;
        emit logMessage(
            CRAWLING_TEXT("纠偏全链路-感知 #%1：数据源=%2，原图轴长=%3 px，"
                          "激光范围=%4..%5 px，主缺口=%6..%7 px，"
                          "图像中心=%8%，目标中心=%9%，坐标换算=图像右侧->车体右转正方向，"
                          "原始/滤波偏差=%10/%11 mm，帧间绝对误差变化=%12 mm，"
                          "误差变化率=%13 mm/s，趋势=%14，置信度=%15，"
                          "边界余量=%16%，图像年龄=%17 ms，有效检测年龄=%18 ms")
                .arg(controlDiagnosticSequence_)
                .arg(detectionSource)
                .arg(latestImageAxisLengthPx_)
                .arg(latestLineStartPx_)
                .arg(latestLineEndPx_)
                .arg(status_.gapStartPx)
                .arg(status_.gapEndPx)
                .arg(status_.gapAbsoluteCenterRatio * 100.0, 0, 'f', 2)
                .arg(referenceGapAbsoluteCenterRatio * 100.0, 0, 'f', 2)
                .arg(rawCenterErrorM * 1000.0, 0, 'f', 2)
                .arg(centerErrorM * 1000.0, 0, 'f', 2)
                .arg(centerErrorDeltaM_ * 1000.0, 0, 'f', 3)
                .arg(filteredCenterErrorRateMps_ * 1000.0, 0, 'f', 2)
                .arg(centerTrend)
                .arg(status_.confidence, 0, 'f', 3)
                .arg(laserRangeMargin * 100.0, 0, 'f', 1)
                .arg(lastImageMs_ >= 0 ? now - lastImageMs_ : -1)
                .arg(lastValidDetectionMs_ >= 0
                         ? now - lastValidDetectionMs_
                         : -1));
        emit logMessage(
            CRAWLING_TEXT("纠偏全链路-融合 #%1：拼接点云=%2 点，拟合角=%3°，"
                          "期望/估算航向=%4/%5°，航向误差=%6°，"
                          "实时中心原始/滤波转向=%7/%8 deg/s，中心权重=%9%，"
                          "点云慢环原始/有效=%10/%11 deg/s，两路冲突=%12，"
                          "中心优先=%13%，连续回中保护=%14，边界回中=%15，"
                          "融合目标=%16 deg/s，安全限幅=%17 deg/s，"
                          "动态曲率=%18 deg/m")
                .arg(controlDiagnosticSequence_)
                .arg(guidanceSamples_.size())
                .arg(status_.fittedAngleRad * 180.0 / kPi, 0, 'f', 2)
                .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
                .arg(yawRad_ * 180.0 / kPi, 0, 'f', 2)
                .arg(headingError * 180.0 / kPi, 0, 'f', 2)
                .arg(centerAngularTarget * 180.0 / kPi, 0, 'f', 2)
                .arg(centerAngularForFusion * 180.0 / kPi, 0, 'f', 2)
                .arg(centerFeedbackWeight * 100.0, 0, 'f', 1)
                .arg(headingLoopAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(effectiveHeadingAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(fusionConflict ? CRAWLING_TEXT("是")
                                    : CRAWLING_TEXT("否"))
                .arg(centerPriority * 100.0, 0, 'f', 1)
                .arg(centerResponseUntrusted_ ? CRAWLING_TEXT("启用")
                                                : CRAWLING_TEXT("关闭"))
                .arg(boundaryRecoveryActive ? CRAWLING_TEXT("启用")
                                            : CRAWLING_TEXT("关闭"))
                .arg(rawTargetAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(safeAngularLimit * 180.0 / kPi, 0, 'f', 2)
                .arg(adaptiveCurvature * 180.0 / kPi, 0, 'f', 1));
        emit logMessage(
            CRAWLING_TEXT("纠偏全链路-执行 #%1：持续前进=%2，目标/实际命令线速度=%3/%4 mm/s，"
                          "目标/实际命令角速度=%5/%6 deg/s，差速几何轨距=%7 mm，"
                          "混合计算左右轮=%8/%9 mm/s，底盘目标左右轮=%10/%11 mm/s，"
                          "实际左右轮=%12/%13 mm/s，实际轮速角速度=%14 deg/s，"
                          "命令-反馈方向一致=%15，底盘应用=%16 mm/s/%17 deg/s，"
                          "轮端反馈年龄=%18 ms，内侧轮最低比例=%19%")
                .arg(controlDiagnosticSequence_)
                .arg(currentLinearMps_ > 0.0 ? CRAWLING_TEXT("是")
                                             : CRAWLING_TEXT("否"))
                .arg(targetLinear * 1000.0, 0, 'f', 2)
                .arg(currentLinearMps_ * 1000.0, 0, 'f', 2)
                .arg(targetAngular * 180.0 / kPi, 0, 'f', 2)
                .arg(currentAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                .arg(settings_.trackWidthM * 1000.0, 0, 'f', 1)
                .arg(mixedLeftMps * 1000.0, 0, 'f', 2)
                .arg(mixedRightMps * 1000.0, 0, 'f', 2)
                .arg(leftTargetMps_ * 1000.0, 0, 'f', 2)
                .arg(rightTargetMps_ * 1000.0, 0, 'f', 2)
                .arg(leftSpeedMps_ * 1000.0, 0, 'f', 2)
                .arg(rightSpeedMps_ * 1000.0, 0, 'f', 2)
                .arg(measuredYawRateRadps * 180.0 / kPi, 0, 'f', 2)
                .arg(steeringDirectionMismatch ? CRAWLING_TEXT("否")
                                               : CRAWLING_TEXT("是"))
                .arg(driveAppliedLinearMps_ * 1000.0, 0, 'f', 2)
                .arg(driveAppliedAngularRadps_ * 180.0 / kPi, 0, 'f', 2)
                .arg(lastTelemetryMs_ >= 0 ? now - lastTelemetryMs_ : -1)
                .arg(settings_.minimumInnerWheelRatio * 100.0, 0, 'f', 0));
        emit logMessage(
            CRAWLING_TEXT("纠偏速度监督 #%1：设置 %2 mm/s，监督比例 %3%，"
                          "边界比例 %4%，观测比例 %5%，观测年龄 %6 ms，"
                          "监督目标 %7 mm/s，加速度平滑输出 %8 mm/s，"
                          "轮速反馈 %9 mm/s，原因=%10")
                .arg(controlDiagnosticSequence_)
                .arg(settings_.targetSpeedMps * 1000.0, 0, 'f', 2)
                .arg(speedSupervisorScale_ * 100.0, 0, 'f', 1)
                .arg(speedSupervisorBoundaryScale_ * 100.0, 0, 'f', 1)
                .arg(speedSupervisorObservationScale_ * 100.0, 0, 'f', 1)
                .arg(speedSupervisorObservationAgeMs_)
                .arg(targetLinear * 1000.0, 0, 'f', 2)
                .arg(currentLinearMps_ * 1000.0, 0, 'f', 2)
                .arg((leftSpeedMps_ + rightSpeedMps_) * 500.0, 0, 'f', 2)
                .arg(speedSupervisorReason_));
      }
      status_.reason = runningReason();
      return;
    }
  }
}

void LaserCorrectionController::appendCurrentEdgeSample() {
  if (!haveTelemetry_ || !status_.gapValid || detectionHeld_ || reacquisitionPending_ ||
      lastImageMs_ < phaseStartedMs_ ||
      lastCloudCameraSequence_ == cameraFrameSequence_ ||
      (phase_ != Phase::SurveyForward &&
       phase_ != Phase::TrackingForward)) {
    return;
  }
  lastCloudCameraSequence_ = cameraFrameSequence_;
  double sampledTravelM = phaseTravelM_;
  const qint64 observationMs = lastImageMs_;
  if (lastTelemetryMs_ >= enabledAtMs_ &&
      std::abs(observationMs - lastTelemetryMs_) <= settings_.telemetryTimeoutMs) {
    // Camera frames arrive faster than the wheel telemetry. Interpolate their
    // longitudinal coordinate at host receipt time, not at the end of image
    // processing. Negative dt backs up a recent telemetry pose for a queued
    // camera frame; this is constant-speed registration, not exposure timing.
    const double extrapolationSeconds =
        clamp((observationMs - lastTelemetryMs_) / 1000.0, -0.5, 0.5);
    const double forwardSpeedMps =
        (std::abs(leftSpeedMps_) + std::abs(rightSpeedMps_)) * 0.5;
    sampledTravelM += forwardSpeedMps * extrapolationSeconds;
  }
  const double longitudinal =
      clamp(sampledTravelM, 0.0, settings_.segmentLengthM);
  LaserEdgeSample sample;
  sample.longitudinalM = std::max(0.0, longitudinal);
  // Cloud geometry uses this frame's measured edges. Control-only smoothing
  // would otherwise assign old edge locations to the current vehicle pose.
  sample.leftLateralM = measuredLeftEdgeM_;
  sample.rightLateralM = measuredRightEdgeM_;
  if (visualSamples_.isEmpty() ||
      sample.longitudinalM - lastVisualLongitudinalM_ >= 0.00001) {
    visualSamples_.append(sample);
    lastVisualLongitudinalM_ = sample.longitudinalM;
  }
  // Keep weak frames in the diagnostic image, but do not let an edge-break
  // candidate with too little support enter either fitted point cloud.
  if (status_.edgeBreakFallback || status_.contourFallback ||
      status_.confidence < kMinimumGeometrySampleConfidence) return;
  if (lastSampleLongitudinalM_ >= 0.0 &&
      longitudinal - lastSampleLongitudinalM_ < settings_.sampleSpacingM) {
    return;
  }
  samples_.append(sample);
  lastSampleLongitudinalM_ = sample.longitudinalM;
  if (phase_ == Phase::TrackingForward) {
    double predictedLongitudinalM = odometryLongitudinalM_;
    double predictedLateralM = odometryLateralM_;
    double predictedYawRad = yawRad_;
    if (lastTelemetryMs_ >= enabledAtMs_ &&
        std::abs(observationMs - lastTelemetryMs_) <= settings_.telemetryTimeoutMs) {
      const double extrapolationSeconds =
          clamp((observationMs - lastTelemetryMs_) / 1000.0, -0.5, 0.5);
      const double forwardSpeedMps =
          (leftSpeedMps_ + rightSpeedMps_) * 0.5;
      const double yawRateRadps =
          (leftSpeedMps_ - rightSpeedMps_) / settings_.trackWidthM;
      const double yawDeltaRad = yawRateRadps * extrapolationSeconds;
      predictedLongitudinalM +=
          std::cos(predictedYawRad + yawDeltaRad * 0.5) *
          forwardSpeedMps * extrapolationSeconds;
      predictedLateralM +=
          std::sin(predictedYawRad + yawDeltaRad * 0.5) *
          forwardSpeedMps * extrapolationSeconds;
      predictedYawRad = normalizedAngle(predictedYawRad + yawDeltaRad);
    }

    // Convert the laser edges from the moving vehicle frame into the
    // odometry frame. This prevents a commanded turn from looking like a new
    // weld direction in the following 20 mm segment.
    LaserEdgeSample guidanceSample;
    const double edgeCenterM =
        (measuredLeftEdgeM_ + measuredRightEdgeM_) * 0.5;
    // Transform the complete laser point (look-ahead plus lateral offset),
    // not just its lateral component. Omitting the longitudinal rotation term
    // biases the rolling fit whenever the chassis is already turned.
    guidanceSample.longitudinalM =
        predictedLongitudinalM + std::cos(predictedYawRad) *
                                     settings_.laserCenterLookaheadM -
        std::sin(predictedYawRad) * edgeCenterM;
    guidanceSample.leftLateralM =
        predictedLateralM +
        std::sin(predictedYawRad) * settings_.laserCenterLookaheadM +
        std::cos(predictedYawRad) * measuredLeftEdgeM_;
    guidanceSample.rightLateralM =
        predictedLateralM +
        std::sin(predictedYawRad) * settings_.laserCenterLookaheadM +
        std::cos(predictedYawRad) * measuredRightEdgeM_;
    guidanceSample.leftLongitudinalM = predictedLongitudinalM +
        std::cos(predictedYawRad) * settings_.laserCenterLookaheadM -
        std::sin(predictedYawRad) * measuredLeftEdgeM_;
    guidanceSample.rightLongitudinalM = predictedLongitudinalM +
        std::cos(predictedYawRad) * settings_.laserCenterLookaheadM -
        std::sin(predictedYawRad) * measuredRightEdgeM_;
    const double sampleTravelM = std::max(0.0, trackingTravelM_ + sampledTravelM - phaseTravelM_);
    if (lastGuidanceLongitudinalM_ < 0.0 ||
        sampleTravelM - lastGuidanceLongitudinalM_ >=
            settings_.sampleSpacingM * 0.75) {
      guidanceSamples_.append(guidanceSample);
      guidanceTravelSamples_.append(sampleTravelM);
      lastGuidanceLongitudinalM_ = sampleTravelM;
    }

    const double guidanceWindowM =
        clamp(settings_.segmentLengthM * 4.0, 0.08, 0.30);
    const double oldestAllowedM = sampleTravelM - guidanceWindowM;
    int removeCount = 0;
    while (removeCount < guidanceSamples_.size() &&
           guidanceTravelSamples_[removeCount] < oldestAllowedM) {
      ++removeCount;
    }
    if (removeCount > 0) {
      guidanceSamples_.remove(0, removeCount);
      guidanceTravelSamples_.remove(0, removeCount);
    }
  }
  status_.collectedSamples = samples_.size();
}

bool LaserCorrectionController::fitCollectedPath(QString* error) {
  LaserPathFit segmentFit = LaserPathEstimator::fit(
      samples_, settings_.minimumFitSamples,
      settings_.segmentLengthM * settings_.minimumFitSpanRatio,
      settings_.maximumFitRmsErrorM);
  const auto failureReasonFor = [](const LaserPathFit& fit) {
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
        case 7:
          return CRAWLING_TEXT("左右边缘几何不一致");
        default:
          return CRAWLING_TEXT("未知拟合条件失败");
    }
  };

  const bool trackingFit = phase_ == Phase::TrackingForward;
  const LaserPathFit trajectoryFit = segmentFit;
  LaserPathFit guidanceFit;
  if (trackingFit) {
    // Refit in the current vehicle frame, so the world X axis need not be
    // monotonic and a large vehicle yaw cannot make the cloud nearly vertical.
    QVector<LaserEdgeSample> localGuidance;
    localGuidance.reserve(guidanceSamples_.size());
    const double cosine = std::cos(yawRad_);
    const double sine = std::sin(yawRad_);
    for (const auto& world : guidanceSamples_) {
      const double leftDx = world.leftLongitudinalM - odometryLongitudinalM_;
      const double rightDx = world.rightLongitudinalM - odometryLongitudinalM_;
      const double leftDy = world.leftLateralM - odometryLateralM_;
      const double rightDy = world.rightLateralM - odometryLateralM_;
      LaserEdgeSample local;
      local.leftLongitudinalM = cosine * leftDx + sine * leftDy;
      local.rightLongitudinalM = cosine * rightDx + sine * rightDy;
      local.longitudinalM = (local.leftLongitudinalM + local.rightLongitudinalM) * 0.5;
      local.leftLateralM = -sine * leftDx + cosine * leftDy;
      local.rightLateralM = -sine * rightDx + cosine * rightDy;
      localGuidance.append(local);
    }
    const double guidanceMinimumSpanM =
        clamp(settings_.segmentLengthM * 1.75, 0.035, 0.12);
    guidanceFit = LaserPathEstimator::fit(
        localGuidance, std::max(8, settings_.minimumFitSamples),
        guidanceMinimumSpanM,
        std::max(0.004, settings_.maximumFitRmsErrorM));
    if (guidanceFit.valid) {
      const double leftAngle = std::atan(guidanceFit.leftSlope);
      const double rightAngle = std::atan(guidanceFit.rightSlope);
      const double edgeAngleDifference = std::abs(leftAngle - rightAngle);
      const double widthDrift = std::abs(
          (guidanceFit.rightSlope - guidanceFit.leftSlope) *
          guidanceFit.spanM);
      if (edgeAngleDifference > kMaximumGuidanceEdgeAngleDifferenceRad ||
          widthDrift > kMaximumGuidanceWidthDriftM) {
        emit logMessage(
            CRAWLING_TEXT("滚动点云几何一致性不足，忽略本次航向更新："
                          "左右边缘角差 %1°，宽度漂移 %2 mm")
                .arg(edgeAngleDifference * 180.0 / kPi, 0, 'f', 2)
                .arg(widthDrift * 1000.0, 0, 'f', 2));
        guidanceFit.valid = false;
        guidanceFit.failureCode = 7;
      }
    }
  }
  if (segmentFit.valid) {
    const double leftAngle = std::atan(segmentFit.leftSlope);
    const double rightAngle = std::atan(segmentFit.rightSlope);
    const double edgeAngleDifference = std::abs(leftAngle - rightAngle);
    const double widthDrift = std::abs(
        (segmentFit.rightSlope - segmentFit.leftSlope) * segmentFit.spanM);
    if (edgeAngleDifference > kMaximumGuidanceEdgeAngleDifferenceRad ||
        widthDrift > kMaximumGuidanceWidthDriftM) {
      emit logMessage(
          CRAWLING_TEXT("当前分段几何一致性不足，仅保存图像不更新航向："
                        "左右边缘角差 %1°，宽度漂移 %2 mm")
              .arg(edgeAngleDifference * 180.0 / kPi, 0, 'f', 2)
              .arg(widthDrift * 1000.0, 0, 'f', 2));
      segmentFit.valid = false;
      segmentFit.failureCode = 7;
    }
  }
  const bool useGuidanceFit = trackingFit && guidanceFit.valid;
  const LaserPathFit& controlFit = useGuidanceFit ? guidanceFit : segmentFit;

  queueTrajectoryImage(trajectoryFit,
                       trajectoryFit.valid
                           ? (trackingFit ? CRAWLING_TEXT("纠偏跟踪")
                                          : CRAWLING_TEXT("首段探测"))
                           : CRAWLING_TEXT("拟合失败"));
  if (!controlFit.valid) {
    const QString failureReason = failureReasonFor(segmentFit);
    emit logMessage(
        CRAWLING_TEXT("拟合诊断：%1，有效样本 %2，异常剔除后 %3，跨度 %4 mm，RMS %5 mm")
            .arg(failureReason)
            .arg(segmentFit.validSampleCount)
            .arg(segmentFit.inlierCount)
            .arg(segmentFit.observedSpanM * 1000.0, 0, 'f', 1)
            .arg(segmentFit.observedRmsErrorM * 1000.0, 0, 'f', 1));
    if (error) {
      *error = CRAWLING_TEXT("双边缘点云拟合失败：%1（样本 %2），本段暂不更新纠偏方向")
                   .arg(failureReason)
                   .arg(samples_.size());
    }
    return false;
  }
  if (!segmentFit.valid && useGuidanceFit) {
    emit logMessage(
        CRAWLING_TEXT("本段%1，使用最近 %2 mm 连续点云的滚动拟合保持纠偏")
            .arg(failureReasonFor(segmentFit))
            .arg(guidanceFit.spanM * 1000.0, 0, 'f', 1));
  }

  // Edge coordinates were converted from the nominal raw-image axis into
  // the vehicle frame when the raw image was processed. The fitted slope is
  // therefore already in the odometry yaw convention.
  const double measuredPathAngleRad = controlFit.angleRad;
  const double measuredHeadingRad = useGuidanceFit
      ? normalizedAngle(yawRad_ + measuredPathAngleRad)
      : normalizedAngle(segmentStartYawRad_ + measuredPathAngleRad);
  const double targetUpdateRad =
      normalizedAngle(measuredHeadingRad - desiredHeadingRad_);
  const double fitRmsLimitM = useGuidanceFit
      ? std::max(0.004, settings_.maximumFitRmsErrorM)
      : settings_.maximumFitRmsErrorM;
  const double fitQuality = clamp(
      1.0 - controlFit.rmsErrorM / fitRmsLimitM, 0.10, 1.0);
  // The rolling point cloud is a slow feed-forward estimate. If the current
  // laser frame is an edge-break or a reused frame, reduce both its blend and
  // its per-segment step; otherwise one bad edge can keep walking the target
  // heading in the same direction for several overlapping windows.
  const double guidanceConfidenceWeight = clamp(
      trackedGapConfidence_ / kFullCenterFeedbackConfidence,
      kGuidanceLowConfidenceWeight, 1.0);
  const double edgeBreakGuidanceWeight =
      detectionEdgeBreakFallback_ ? 0.45 : 1.0;
  const double guidanceFreshnessWeight =
      detectionHeld_ ? kGuidanceHeldWeight : 1.0;
  const double guidanceWeight =
      guidanceConfidenceWeight * edgeBreakGuidanceWeight *
      guidanceFreshnessWeight;
  const double effectiveBlend =
      settings_.headingFitBlend * fitQuality * guidanceWeight;
  const double maximumHeadingStepRad = clamp(
      settings_.maximumCurvatureRadPerM * settings_.segmentLengthM * 1.5,
      kMinimumHeadingStepRad, kMaximumHeadingStepRad);
  const double confidenceLimitedHeadingStepRad =
      std::max(kMinimumHeadingStepRad,
               maximumHeadingStepRad * guidanceWeight);
  double appliedHeadingUpdateRad = clamp(
      effectiveBlend * targetUpdateRad,
      -confidenceLimitedHeadingStepRad, confidenceLimitedHeadingStepRad);
  // A single moving-frame 20 mm fit remains useful for the saved diagnostic
  // image, but is too short to steer from safely. During tracking, change the
  // heading only after the odometry-frame rolling cloud is long enough.
  const bool headingUpdateEnabled = !trackingFit || useGuidanceFit;
  if (headingUpdateEnabled) lastGuidanceFitMs_ = clock_.elapsed();
  if (!headingUpdateEnabled) appliedHeadingUpdateRad = 0.0;
  const int updateDirection = appliedHeadingUpdateRad > 1e-5
                                  ? 1
                                  : (appliedHeadingUpdateRad < -1e-5 ? -1 : 0);
  bool reversalHeld = false;
  if (updateDirection != 0 && lastAcceptedFitDirection_ != 0 &&
      updateDirection != lastAcceptedFitDirection_) {
    if (pendingFitDirection_ == updateDirection) {
      ++pendingFitDirectionCount_;
    } else {
      pendingFitDirection_ = updateDirection;
      pendingFitDirectionCount_ = 1;
    }
    if (pendingFitDirectionCount_ < kFitReversalConfirmationSegments) {
      appliedHeadingUpdateRad = 0.0;
      reversalHeld = true;
    } else {
      lastAcceptedFitDirection_ = updateDirection;
      pendingFitDirection_ = 0;
      pendingFitDirectionCount_ = 0;
    }
  } else if (updateDirection != 0) {
    lastAcceptedFitDirection_ = updateDirection;
    pendingFitDirection_ = 0;
    pendingFitDirectionCount_ = 0;
  }
  desiredHeadingRad_ = normalizedAngle(
      desiredHeadingRad_ + appliedHeadingUpdateRad);
  status_.fittedAngleRad = measuredPathAngleRad;
  status_.fitRmsErrorM = controlFit.rmsErrorM;
  status_.collectedSamples = segmentFit.valid
                                 ? segmentFit.sampleCount
                                 : samples_.size();
  if (status_.cycleCount == 0) status_.cycleCount = 1;
  emit logMessage(
      CRAWLING_TEXT("双边缘拟合完成：来源 %1，角度 %2°，目标航向 %3°，"
                    "本次更新 %4°，RMS %5/%6 mm，跨度 %7 mm，样本 %8，"
                    "有效融合 %9%，实时几何权重 %10%，反向等待 %11")
          .arg(useGuidanceFit
                   ? CRAWLING_TEXT("滚动点云")
                   : (trackingFit ? CRAWLING_TEXT("当前分段（仅诊断）")
                                  : CRAWLING_TEXT("当前分段")))
          .arg(measuredPathAngleRad * 180.0 / kPi, 0, 'f', 2)
          .arg(desiredHeadingRad_ * 180.0 / kPi, 0, 'f', 2)
          .arg(appliedHeadingUpdateRad * 180.0 / kPi, 0, 'f', 2)
          .arg(controlFit.rmsErrorM * 1000.0, 0, 'f', 2)
          .arg(fitRmsLimitM * 1000.0, 0, 'f', 2)
          .arg(controlFit.spanM * 1000.0, 0, 'f', 1)
          .arg(controlFit.sampleCount)
          .arg(headingUpdateEnabled ? effectiveBlend * 100.0 : 0.0,
               0, 'f', 1)
          .arg(guidanceWeight * 100.0, 0, 'f', 1)
          .arg(reversalHeld ? CRAWLING_TEXT("是") : CRAWLING_TEXT("否")));
  const LaserPathFit& detailFit = useGuidanceFit ? guidanceFit : segmentFit;
  const double leftAngleDeg = std::atan(detailFit.leftSlope) * 180.0 / kPi;
  const double rightAngleDeg = std::atan(detailFit.rightSlope) * 180.0 / kPi;
  const double startWidthM =
      detailFit.rightInterceptM - detailFit.leftInterceptM;
  const double endWidthM =
      startWidthM + (detailFit.rightSlope - detailFit.leftSlope) *
                        detailFit.spanM;
  emit logMessage(
      CRAWLING_TEXT("拟合详情：周期 %1，阶段 %2，左角 %3°，右角 %4°，"
                    "左/右斜率 %5/%6，中心截距 %7 mm，"
                    "断口宽度起点/终点 %8/%9 mm，原始/有效样本 %10/%11")
          .arg(status_.cycleCount)
          .arg(phaseName(phase_))
          .arg(leftAngleDeg, 0, 'f', 2)
          .arg(rightAngleDeg, 0, 'f', 2)
          .arg(detailFit.leftSlope, 0, 'f', 5)
          .arg(detailFit.rightSlope, 0, 'f', 5)
          .arg(detailFit.centerInterceptM * 1000.0, 0, 'f', 2)
          .arg(startWidthM * 1000.0, 0, 'f', 2)
          .arg(endWidthM * 1000.0, 0, 'f', 2)
          .arg(samples_.size())
          .arg(detailFit.sampleCount));
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

bool LaserCorrectionController::stopIfImageTimedOut(qint64 now) {
  if (phaseNeedsLaser() &&
      now - enabledAtMs_ > settings_.imageTimeoutMs &&
      (lastImageMs_ < enabledAtMs_ ||
       now - lastImageMs_ > settings_.imageTimeoutMs)) {
    const QString timeoutSnapshot =
        QStringLiteral("event=laser_input_timeout session=%1 camera_seq=%2 sdk_frame=%3 phase=%4 image_age_ms=%5 telemetry_age_ms=%6 command_age_ms=%7 watchdog_gap_ms=%8 detector_ms=%9 queue_age_at_entry_ms=%10")
            .arg(trajectorySessionId_).arg(cameraFrameSequence_).arg(sourceFrameNumber_)
            .arg(phaseName(phase_)).arg(lastImageMs_ < 0 ? -1 : now - lastImageMs_)
            .arg(lastTelemetryMs_ < 0 ? -1 : now - lastTelemetryMs_)
            .arg(lastCommandMs_ < 0 ? -1 : now - lastCommandMs_)
            .arg(lastWatchdogTickMs_ < 0 ? -1 : now - lastWatchdogTickMs_)
            .arg(lastDetectorDurationMs_).arg(lastFrameQueueAgeMs_);
    stop(CRAWLING_TEXT("激光原始图像超时，已停止自动纠偏"));
    emit diagnosticLogMessage(timeoutSnapshot);
    return true;
  }
  return false;
}

void LaserCorrectionController::watchdogTick() {
  if (!status_.active) return;
  const qint64 now = clock_.elapsed();
  if (stopIfImageTimedOut(now)) return;
  if (stopIfScanBoundaryUnsafe(now)) return;
  if (lastWatchdogTickMs_ >= 0 && now - lastWatchdogTickMs_ > 100) {
    emit diagnosticLogMessage(
        QStringLiteral("event=correction_dispatch_delay session=%1 watchdog_gap_ms=%2 detector_ms=%3 image_age_ms=%4 command_age_ms=%5")
            .arg(trajectorySessionId_).arg(now - lastWatchdogTickMs_)
            .arg(lastDetectorDurationMs_).arg(lastImageMs_ < 0 ? -1 : now - lastImageMs_)
            .arg(lastCommandMs_ < 0 ? -1 : now - lastCommandMs_));
  }
  lastWatchdogTickMs_ = now;
  if (now - enabledAtMs_ > settings_.telemetryTimeoutMs &&
      (lastTelemetryMs_ < enabledAtMs_ ||
       now - lastTelemetryMs_ > settings_.telemetryTimeoutMs)) {
    stop(CRAWLING_TEXT("轮端里程反馈超时，已停止自动纠偏"));
    return;
  }
  // Image receipt cannot renew the independent measured-seam safety age.

  // New camera centres must affect steering at the control heartbeat, not
  // wait for the next (much slower) motor reply. Reuse only fresh telemetry;
  // odometry is still integrated exclusively when an actual reply arrives.
  if (phaseMoves() && haveTelemetry_ && lastTelemetryMs_ >= enabledAtMs_ &&
      now - lastTelemetryMs_ <= settings_.telemetryTimeoutMs &&
      (previousControlMs_ < 0 || now - previousControlMs_ >= 15)) {
    advanceFromTelemetry(now);
    if (!status_.active) return;
  }

  if (lastCommandMs_ < 0 || now - lastCommandMs_ >= 100) {
    emit commandChanged(currentLinearMps_, currentAngularRadps_);
    lastCommandMs_ = now;
  }
}

void LaserCorrectionController::resetControllerError() {
  filteredYawRateRadps_ = 0.0;
  filteredCenterAngularRadps_ = 0.0;
  status_.headingErrorRad = 0.0;
}

void LaserCorrectionController::resetSession() {
  phase_ = Phase::Idle;
  enabledAtMs_ = -1;
  phaseStartedMs_ = -1;
  lastImageMs_ = -1;
  lastValidDetectionMs_ = -1;
  lastObservedGapAbsoluteCenterRatio_ = 0.5;
  lastObservedWasContour_ = false;
  lastTelemetryMs_ = -1;
  previousControlMs_ = -1;
  lastCommandMs_ = -1;
  lastControlDiagnosticMs_ = -1;
  lastDetectionDiagnosticMs_ = -1;
  lastDetectionRejectLogMs_ = -1;
  lastCenterTrendSampleMs_ = -1;
  lastRawFrameDiagnosticMs_ = -1;
  lastRawFrameSaveMs_ = -1;
  // Keep the writer's outstanding request across task resets. Its matching
  // session/sequence acknowledgement is the only event that releases it.
  rawArchiveDisabled_ = false;
  sourceReceivedAtEpochMs_ = -1;
  sourceFrameNumberValid_ = false;
  processingSourceFrame_ = false;
  lastSourceRejectLogMs_ = -1;
  lastDetectorDurationMs_ = -1;
  lastFrameQueueAgeMs_ = -1;
  lastWatchdogTickMs_ = -1;
  lastGuidanceFitMs_ = -1;
  lastCloudCameraSequence_ = 0;
  measuredLeftEdgeM_ = 0.0;
  measuredRightEdgeM_ = 0.0;
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
  odometryLongitudinalM_ = 0.0;
  odometryLateralM_ = 0.0;
  trackingTravelM_ = 0.0;
  lastMotionProgressMs_ = -1;
  lastSampleLongitudinalM_ = -1.0;
  desiredHeadingRad_ = 0.0;
  lastAcceptedFitDirection_ = 0;
  pendingFitDirection_ = 0;
  pendingFitDirectionCount_ = 0;
  currentLinearMps_ = 0.0;
  currentAngularRadps_ = 0.0;
  angularLimitLogged_ = false;
  trackingFitFailureCount_ = 0;
  controlDiagnosticSequence_ = 0;
  rawFrameDiagnosticSequence_ = 0;
  cameraFrameSequence_ = 0;
  invalidDetectionCount_ = 0;
  reusedDetectionCount_ = 0;
  angularDirectionChangeCount_ = 0;
  lastAngularDirection_ = 0;
  steeringMismatchCount_ = 0;
  centerControlDirection_ = 0;
  pendingCenterControlDirection_ = 0;
  pendingCenterControlDirectionCount_ = 0;
  lastCenterDirectionCameraSequence_ = 0;
  initialGapConfirmationCount_ = 0;
  initialGapConfirmationHorizontal_ = true;
  initialGapConfirmationContour_ = false;
  pendingInitialGapAbsoluteCenterRatio_ = 0.5;
  pendingInitialGapWidthRatio_ = 0.0;
  speedSupervisorScale_ = 1.0;
  speedSupervisorBoundaryScale_ = 1.0;
  speedSupervisorObservationScale_ = 1.0;
  speedSupervisorObservationAgeMs_ = -1;
  speedSupervisorRecoverySinceMs_ = -1;
  speedSupervisorReason_ = CRAWLING_TEXT("按跟踪速度运行");
  boundaryProtectionLogged_ = false;
  boundaryContainmentActive_ = false;
  boundaryObservationReliable_ = false;
  boundaryStopLatched_ = false;
  boundaryObservationMs_ = -1;
  boundaryLineStartRatio_ = 0.0;
  boundaryLineEndRatio_ = 1.0;
  boundaryGapStartRatio_ = boundaryGapEndRatio_ = 0.5;
  boundaryMeasuredMarginRatio_ = 0.5;
  boundarySignedRateRatioPerS_ = 0.0;
  boundaryRecoveryStartedMs_ = -1;
  boundaryRecoveryStartMargin_ = 0.0;
  boundaryRecoveryDirection_ = 0;
  centerRecoveryWindowActive_ = false;
  centerRecoveryProtectionLogged_ = false;
  centerResponseUntrusted_ = false;
  detectionDegradedLogged_ = false;
  centerRecoveryWindowStartTravelM_ = 0.0;
  centerRecoveryWindowStartYawRad_ = 0.0;
  reacquisitionPending_ = false;
  centerRecoveryWindowStartErrorM_ = 0.0;
  centerRecoveryBestErrorM_ = 0.0;
  lastCenterTrendErrorM_ = 0.0;
  centerErrorDeltaM_ = 0.0;
  filteredCenterErrorRateMps_ = 0.0;
  filteredSignedCenterRateMps_ = 0.0;
  centerErrorTrendValid_ = false;
  latestImageAxisLengthPx_ = 0;
  latestLineStartPx_ = -1;
  latestLineEndPx_ = -1;
  samples_.clear();
  visualSamples_.clear();
  guidanceSamples_.clear();
  guidanceTravelSamples_.clear();
  lastVisualLongitudinalM_ = -1.0;
  lastGuidanceLongitudinalM_ = -1.0;
  resetGapTracker();
  resetControllerError();
  status_ = {};
  status_.phase = phaseName(phase_);
}

void LaserCorrectionController::setPhase(Phase phase, const QString& reason,
                                         bool writeLog) {
  const Phase previousPhase = phase_;
  phase_ = phase;
  if (phase != previousPhase) {
    // Motion changes at survey/return/settling transitions invalidate its
    // image drift. Preserve the measured envelope, relearn only the rate.
    boundarySignedRateRatioPerS_ = 0.0;
    boundaryRecoveryStartedMs_ = -1;
  }
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
    case Phase::PauseBeforeTracking:
      return CRAWLING_TEXT("路径拟合完成，等待车体停稳");
    case Phase::TrackingForward:
      return CRAWLING_TEXT("柔性纠偏前进并采集下一段");
  }
  return {};
}

void LaserCorrectionController::updateScanBoundaryObservation(
    const LaserGapDetection& detection, int axisLengthPx, qint64 receivedMs,
    bool identityConfirmed) {
  if (!detection.valid || axisLengthPx < 2 ||
      detection.lineEndPx <= detection.lineStartPx ||
      detection.gapEndPx <= detection.gapStartPx) return;
  const double axis = axisLengthPx - 1.0;
  const double left = detection.gapStartPx / axis;
  const double right = detection.gapEndPx / axis;
  const bool measured = identityConfirmed &&
      (!detection.edgeBreakFallback || detection.contourFallback) &&
      detection.confidence >= 0.060 && !reacquisitionPending_;
  if (measured) {
    if (phaseMoves() && boundaryObservationMs_ >= 0 && receivedMs > boundaryObservationMs_ &&
        receivedMs - boundaryObservationMs_ <= 250) {
      const double dt = (receivedMs - boundaryObservationMs_) / 1000.0;
      const double rawRate = clamp(
          ((left + right) - (boundaryGapStartRatio_ + boundaryGapEndRatio_)) *
              0.5 / dt, -0.4, 0.4);
      boundarySignedRateRatioPerS_ += (1.0 - std::exp(-dt / 0.10)) *
          (rawRate - boundarySignedRateRatioPerS_);
    } else {
      boundarySignedRateRatioPerS_ = 0.0;
    }
    boundaryLineStartRatio_ = clamp(detection.lineStartPx / axis, 0.0, 1.0);
    boundaryLineEndRatio_ = clamp(detection.lineEndPx / axis, 0.0, 1.0);
    boundaryGapStartRatio_ = left;
    boundaryGapEndRatio_ = right;
    boundaryMeasuredMarginRatio_ = std::min(
        left - boundaryLineStartRatio_, boundaryLineEndRatio_ - right) /
        std::max(1e-6, boundaryLineEndRatio_ - boundaryLineStartRatio_);
    boundaryObservationMs_ = receivedMs;
    boundaryObservationReliable_ = true;
  } else if (identityConfirmed && detection.edgeBreakFallback &&
             boundaryObservationMs_ >= 0) {
    // Inferred/clipped edges may tighten the safety envelope, never erase
    // known risk or renew the last measured two-shoulder/contour timestamp.
    boundaryGapStartRatio_ = std::min(boundaryGapStartRatio_, left);
    boundaryGapEndRatio_ = std::max(boundaryGapEndRatio_, right);
    boundaryLineStartRatio_ = std::max(boundaryLineStartRatio_,
        clamp(detection.lineStartPx / axis, 0.0, 1.0));
    boundaryLineEndRatio_ = std::min(boundaryLineEndRatio_,
        clamp(detection.lineEndPx / axis, 0.0, 1.0));
  }
}

int LaserCorrectionController::boundaryReturnDirection(qint64 now) const {
  const double leftMargin = boundaryGapStartRatio_ - boundaryLineStartRatio_;
  const double rightMargin = boundaryLineEndRatio_ - boundaryGapEndRatio_;
  const double displacement = boundaryPredictionDisplacement(now);
  const double span = std::max(1e-6, boundaryLineEndRatio_ - boundaryLineStartRatio_);
  const double threatenedLeft = std::min(leftMargin, leftMargin + displacement);
  const double threatenedRight = std::min(rightMargin, rightMargin - displacement);
  // A wide weld can be near both endpoints even while perfectly centred.
  // Do not force alternating turns over sub-pixel left/right margin noise.
  if (std::abs(threatenedLeft - threatenedRight) <= span * 0.02) return 0;
  // Choose the threatened edge from the same conservative prediction that
  // triggered containment. The nearest measured edge can be the other side
  // while the weld is crossing the scan rapidly.
  return threatenedLeft < threatenedRight ? -1 : 1;
}

double LaserCorrectionController::boundaryPredictionDisplacement(qint64 now) const {
  // A stopped survey/return transition must not keep extrapolating the last
  // moving-frame drift and turn an in-range weld into a predicted edge loss.
  if (boundaryObservationMs_ < 0 || !phaseMoves()) return 0.0;
  // Reserve dispatch/actuation time plus a nominal braking interval. Actual
  // wheel response still requires commissioning; prediction only tightens
  // the measured envelope and cannot certify that an edge has recovered.
  const double speed = std::max({std::abs(currentLinearMps_),
      std::abs(driveAppliedLinearMps_), std::abs(leftSpeedMps_), std::abs(rightSpeedMps_)});
  const double horizon = std::max<qint64>(0, now - boundaryObservationMs_) / 1000.0 +
      0.20 + speed / std::max(0.005, settings_.maxLinearAccelerationMps2);
  return boundarySignedRateRatioPerS_ * horizon;
}

double LaserCorrectionController::laserBoundaryMargin(qint64 now) const {
  if (boundaryObservationMs_ < 0) return 0.5;
  const double span = std::max(1e-6, boundaryLineEndRatio_ - boundaryLineStartRatio_);
  const double left = boundaryGapStartRatio_ - boundaryLineStartRatio_;
  const double right = boundaryLineEndRatio_ - boundaryGapEndRatio_;
  const double displacement = boundaryPredictionDisplacement(now);
  return clamp(std::min({left, right, left + displacement, right - displacement}) / span,
               0.0, 0.5);
}

bool LaserCorrectionController::stopIfScanBoundaryUnsafe(qint64 now) {
  if (!status_.active) return false;
  const qint64 age = std::max<qint64>(0, now -
      (boundaryObservationMs_ >= 0 ? boundaryObservationMs_ : enabledAtMs_));
  const double margin = laserBoundaryMargin(now);
  const double measuredMargin = boundaryMeasuredMarginRatio_;
  const int returnDirection = boundaryReturnDirection(now);
  const double measuredAngularRadps =
      (leftSpeedMps_ - rightSpeedMps_) / settings_.trackWidthM;
  QString cause;
  const bool moving = phaseMoves();
  if (boundaryObservationMs_ >= 0 && margin <= kScanContainmentMarginRatio) {
    boundaryContainmentActive_ = true;
  } else if (margin > kScanContainmentReleaseRatio) {
    boundaryContainmentActive_ = false;
    boundaryRecoveryStartedMs_ = -1;
    boundaryRecoveryDirection_ = 0;
  }
  if (moving && measuredMargin <= 0.0) {
    cause = QStringLiteral("seam_outside_scan");
  } else if (moving && age > kMaximumUnmeasuredSeamMs) {
    cause = QStringLiteral("measured_seam_timeout");
  } else if (moving && boundaryContainmentActive_) {
    if (boundaryRecoveryStartedMs_ < 0 ||
        boundaryRecoveryDirection_ != returnDirection) {
      boundaryRecoveryStartedMs_ = now;
      boundaryRecoveryStartMargin_ = measuredMargin;
      boundaryRecoveryDirection_ = returnDirection;
    }
    const qint64 recoveryAge = now - boundaryRecoveryStartedMs_;
    if (age > kBoundaryObservationGraceMs) {
      cause = QStringLiteral("unreliable_seam_near_boundary");
    } else if (recoveryAge >= kBoundaryResponseGraceMs &&
               measuredMargin <= kScanStopMarginRatio &&
               measuredAngularRadps * returnDirection <
                   -kSteeringDeadbandRadps) {
      // Give a commanded reversal time to reach the wheels. Stop only if the
      // seam is nearly out of view and the measured wheels still turn out.
      cause = QStringLiteral("outward_wheel_response");
    } else if (recoveryAge >= 1500 &&
               // Prediction is an early-warning envelope only. It may raise
               // the inward-turn priority, but it cannot prove that the weld
               // has left the usable scan. Final parking requires the current
               // measured envelope itself to be critical and still worsening.
               measuredMargin <= kScanStopMarginRatio &&
               measuredMargin < boundaryRecoveryStartMargin_ - 0.01) {
      cause = QStringLiteral("containment_not_recovering");
    }
  } else if (!moving) {
    // A settling pause is not time spent applying a recovery command.
    boundaryRecoveryStartedMs_ = -1;
  }
  if (cause.isEmpty()) return false;
  boundaryStopLatched_ = true;
  emit diagnosticLogMessage(QStringLiteral(
      "event=scan_boundary_stop session=%1 camera_seq=%2 phase=%3 reason=%4 "
      "measured_margin_ratio=%5 predicted_margin_ratio=%6 line_ratio=%7:%8 "
      "gap_ratio=%9:%10 measured_age_ms=%11 reliable=%12 response_untrusted=%13 "
      "linear_before_mps=%14 angular_before_radps=%15 "
      "return_direction=%16 wheel_angular_radps=%17 drive_angular_radps=%18")
      .arg(trajectorySessionId_).arg(cameraFrameSequence_).arg(phaseName(phase_)).arg(cause)
      .arg(measuredMargin, 0, 'f', 6).arg(margin, 0, 'f', 6)
      .arg(boundaryLineStartRatio_, 0, 'f', 6).arg(boundaryLineEndRatio_, 0, 'f', 6)
      .arg(boundaryGapStartRatio_, 0, 'f', 6).arg(boundaryGapEndRatio_, 0, 'f', 6)
      .arg(age).arg(boundaryObservationReliable_ ? 1 : 0).arg(centerResponseUntrusted_ ? 1 : 0)
      .arg(currentLinearMps_, 0, 'f', 6).arg(currentAngularRadps_, 0, 'f', 6)
      .arg(returnDirection).arg(measuredAngularRadps, 0, 'f', 6)
      .arg(driveAppliedAngularRadps_, 0, 'f', 6));
  stop(CRAWLING_TEXT("扫描范围保护：焊道已出界或回中观测/响应无法保证，已停止；请检查焊道位置后重新启动（%1）")
           .arg(cause));
  return true;
}

double LaserCorrectionController::updateForwardSpeedSupervisor(
    qint64 now, double deltaSeconds, double boundaryScale) {
  Q_UNUSED(deltaSeconds);
  Q_UNUSED(boundaryScale);
  // Cruising speed is the configured chassis-centre speed. Observation and
  // scan-boundary safety are enforced by the separate stop gate; they must
  // not silently turn into a slower, open-loop crawl. Preserve the existing
  // diagnostic fields so recordings remain comparable across versions.
  speedSupervisorScale_ = 1.0;
  speedSupervisorBoundaryScale_ = 1.0;
  speedSupervisorObservationScale_ = 1.0;
  speedSupervisorObservationAgeMs_ =
      lastValidDetectionMs_ >= enabledAtMs_ && lastValidDetectionMs_ >= 0
          ? std::max<qint64>(0, now - lastValidDetectionMs_)
          : -1;
  speedSupervisorRecoverySinceMs_ = -1;
  speedSupervisorReason_ = CRAWLING_TEXT("按设置速度匀速巡航");
  return settings_.targetSpeedMps;
}

void LaserCorrectionController::applyCommand(double targetLinearMps,
                                              double targetAngularRadps,
                                              double deltaSeconds,
                                              double curvatureLimitRadPerM) {
  const qint64 now = clock_.elapsed();
  if (stopIfScanBoundaryUnsafe(now)) return;
  const double boundaryMargin = laserBoundaryMargin(now);
  const int returnDirection = boundaryReturnDirection(now);
  const bool containment = phaseMoves() && boundaryObservationMs_ >= 0 &&
      now - boundaryObservationMs_ <= kBoundaryObservationGraceMs &&
      boundaryContainmentActive_ && returnDirection != 0;
  if (containment) {
    const double wheelLimit =
        (2.0 * std::abs(targetLinearMps) / settings_.trackWidthM) *
        ((1.0 - settings_.minimumInnerWheelRatio) /
         (1.0 + settings_.minimumInnerWheelRatio));
    const double recoveryWeight = clamp(
        (kScanContainmentReleaseRatio - boundaryMargin) /
            (kScanContainmentReleaseRatio - kScanStopMarginRatio), 0.0, 1.0);
    const double minimumTurn = std::min(kMinimumContainmentAngularRadps,
                                        wheelLimit * 0.85) * recoveryWeight;
    targetAngularRadps = returnDirection * std::max(
        targetAngularRadps * returnDirection, minimumTurn);
    if (!boundaryProtectionLogged_) {
      boundaryProtectionLogged_ = true;
      emit diagnosticLogMessage(QStringLiteral(
          "event=scan_boundary_recenter session=%1 camera_seq=%2 phase=%3 "
          "margin_ratio=%4 measured_age_ms=%5 return_direction=%6 "
          "linear_target_mps=%7 angular_target_radps=%8")
          .arg(trajectorySessionId_).arg(cameraFrameSequence_).arg(phaseName(phase_))
          .arg(boundaryMargin, 0, 'f', 6).arg(now - boundaryObservationMs_)
          .arg(returnDirection).arg(targetLinearMps, 0, 'f', 6)
          .arg(targetAngularRadps, 0, 'f', 6));
      emit logMessage(CRAWLING_TEXT(
          "扫描范围回中：焊道余量 %1%，保持设置速度并向内转向")
          .arg(boundaryMargin * 100.0, 0, 'f', 1));
    }
  } else if (boundaryMargin > kScanContainmentReleaseRatio) {
    boundaryProtectionLogged_ = false;
  }
  currentLinearMps_ = rate(
      currentLinearMps_, targetLinearMps,
      std::max(0.005, settings_.maxLinearAccelerationMps2) * deltaSeconds);
  // Brake a stale turn faster than we build a new one. This shortens the
  // phase lag when the live laser feedback reverses, without making the
  // normal correction step more aggressive.
  const bool angularBraking =
      currentAngularRadps_ * targetAngularRadps < 0.0 ||
      std::abs(targetAngularRadps) < std::abs(currentAngularRadps_);
  const double angularAccelerationLimit =
      std::max(0.01, settings_.maxAngularAccelerationRadps2) *
      (angularBraking ? kAngularBrakeAccelerationMultiplier : 1.0);
  const double acceleratedAngularRadps = rate(
      currentAngularRadps_, targetAngularRadps,
      angularAccelerationLimit * deltaSeconds);
  const double currentLinearAngularLimit =
      (2.0 * std::abs(currentLinearMps_) / settings_.trackWidthM) *
      ((1.0 - settings_.minimumInnerWheelRatio) /
       (1.0 + settings_.minimumInnerWheelRatio));
  // Linear and angular commands accelerate at different rates. Re-apply both
  // steering limits using the current ramped linear speed so startup cannot
  // briefly turn much harder than the steady-state command.
  const double currentCurvatureAngularLimit =
      std::abs(currentLinearMps_) *
      (curvatureLimitRadPerM > 0.0 ? curvatureLimitRadPerM
                                   : settings_.maximumCurvatureRadPerM);
  const double currentSafeAngularLimit =
      std::min(settings_.maxAngularRadps,
               containment ? currentLinearAngularLimit
                           : std::min(currentLinearAngularLimit, currentCurvatureAngularLimit));
  currentAngularRadps_ = clamp(acceleratedAngularRadps,
                               -currentSafeAngularLimit,
                               currentSafeAngularLimit);
  // Recheck the final ramped command, including its changed braking horizon.
  // Every boundary stop uses the same complete diagnostic record and latch.
  if (stopIfScanBoundaryUnsafe(clock_.elapsed())) return;
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
  status_.detectionHeld = detectionHeld_;
  emit statusChanged(status_);
}

}  // namespace crawling
