#pragma once

#include "drive_types.h"
#include "laser_gap_detector.h"
#include "laser_path_estimator.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace crawling {

struct LaserCorrectionSettings {
  // Nominal image scale; physical calibration cannot be inferred from a
  // monochrome camera image. Pixel/ratio logs remain the primary evidence.
  double imageLateralSpanM = 0.20;
  double segmentLengthM = 0.10;
  double sampleSpacingM = 0.004;
  int minimumFitSamples = 8;
  double minimumFitSpanRatio = 0.45;
  double maximumFitRmsErrorM = 0.015;
  double returnToleranceM = 0.004;
  int settleTimeMs = 300;

  double proportionalGain = 3.0;
  double derivativeGain = 0.12;
  double derivativeAlpha = 0.18;
  // Kept for source/configuration compatibility. The vehicle coordinate
  // convention is fixed in the controller; this is no longer a UI setting.
  int steeringSign = 1;
  double targetSpeedMps = 0.005;
  // Keep making forward progress while correcting a large heading error.
  double minSpeedScale = 0.60;
  double speedReductionGain = 0.5;
  // Blend each newly fitted path into the current target instead of changing
  // the requested vehicle heading in one step.
  double headingFitBlend = 0.25;
  // Geometry-based steering limit. The controller applies this to the
  // current forward speed, so the turn remains a translating turn instead of
  // a pivot while still allowing stale measurements to be corrected promptly.
  // Keep the correction gentle, but do not make a 10--20 mm/s vehicle
  // effectively unable to turn. The previous 0.35 rad/m cap limited a
  // 12 mm/s run to about 0.24 deg/s, which made stale measurements dominate.
  double maximumCurvatureRadPerM = 0.90;
  // Keep the weld near the optical scan center. The nominal lookahead
  // converts lateral image error into a gentle steering curvature.
  double laserCenterFeedbackGain = 0.60;
  double laserCenterLookaheadM = 0.25;
  // Fast raw-image lateral feedback. This is intentionally separate from
  // the fitted-path heading blend: the gap position is the freshest measure
  // of whether the weld is leaving the laser's usable center region.
  double laserCenterAngularGain = 1.80;
  // Keep the inner wheel moving during correction. The previous 0.45 rad/s
  // cap was too aggressive at low speed and could turn almost in place.
  double minimumInnerWheelRatio = 0.45;
  double maxAngularRadps = 0.25;
  double maxAngularAccelerationRadps2 = 0.20;
  double maxLinearAccelerationMps2 = 0.05;

  double wheelRadiusM = 0.040;
  double trackWidthM = 0.300;
  int imageTimeoutMs = 180;
  int telemetryTimeoutMs = 600;
  int detectionTimeoutMs = 500;
  int transientDetectionHoldMs = 500;
  // A rejected candidate is not a camera outage. Reduce stale steering
  // authority while fresh frames are used to reacquire the main stripe.
  int detectionRecoveryTimeoutMs = 1800;
  int motionStallTimeoutMs = 5000;
  LaserGapDetectorConfig detector;
};

struct LaserCorrectionStatus {
  bool active = false;
  bool gapValid = false;
  bool horizontalLaser = true;
  // Position in the full camera axis. gapCenterRatio remains the position
  // inside the currently visible laser-line span for diagnostics/tracking.
  double gapAbsoluteCenterRatio = 0.5;
  double gapCenterRatio = 0.5;
  double referenceGapAbsoluteCenterRatio = 0.5;
  bool edgeBreakFallback = false;
  bool detectionHeld = false;
  bool baselineSupported = false;
  double baselineOffsetPx = 0.0;
  double baselineSlope = 0.0;
  double baselineHalfWidthPx = 0.0;
  // Full-image lateral coordinate converted into the vehicle frame. The
  // configured convention maps increasing raw-image X to positive vehicle
  // right-turn correction and is shared by the live loop and point cloud.
  double gapLateralM = 0.0;
  double leftEdgeLateralM = 0.0;
  double rightEdgeLateralM = 0.0;
  double confidence = 0.0;
  double segmentProgressM = 0.0;
  double fittedAngleRad = 0.0;
  double headingErrorRad = 0.0;
  double fitRmsErrorM = 0.0;
  double linearCommandMps = 0.0;
  double angularCommandRadps = 0.0;
  double laserCenterErrorM = 0.0;
  double laserCenterCorrectionRad = 0.0;
  int gapStartPx = -1;
  int gapEndPx = -1;
  int supportingSamples = 0;
  int collectedSamples = 0;
  int cycleCount = 0;
  QString phase;
  QString reason;
  QString trajectoryDirectory;
  QString lastTrajectoryImage;
};

class LaserCorrectionController final : public QObject {
  Q_OBJECT

 public:
  explicit LaserCorrectionController(QObject* parent = nullptr);

 public slots:
  void setSettings(const crawling::LaserCorrectionSettings& settings);
  void setEnabled(bool enabled);
  void shutdown();
  void processCameraImage(const QImage& image);
  void processCameraFrame(const QImage& image, quint32 sourceFrameNumber,
                          qint64 receivedAtEpochMs);
  void processDriveTelemetry(const crawling::DriveTelemetry& telemetry);
  void trajectorySessionStarted(quint64 sessionId, const QString& directory);
  void trajectoryImageSaved(quint64 sessionId, const QString& path);
  void trajectorySaveFailed(quint64 sessionId, const QString& error);
  void rawFrameSaved(quint64 sessionId, quint64 frameSequence,
                     const QString& path, const QString& error);

 signals:
  void commandChanged(double linearMps, double angularRadps);
  void statusChanged(const crawling::LaserCorrectionStatus& status);
  void logMessage(const QString& message);
  // High-rate structured diagnostics are written to disk but intentionally
  // kept out of the operator console.
  void diagnosticLogMessage(const QString& message);
  void rawFrameReady(quint64 sessionId, quint64 frameSequence,
                      const QImage& image, const QString& metadataJson);
  void cameraObservationReady(const QImage& image,
                               const crawling::LaserGapDetection& detection);
  void trajectorySessionRequested(quint64 sessionId);
  void trajectorySegmentReady(
      quint64 sessionId,
      const QVector<crawling::LaserEdgeSample>& scanSamples,
      const crawling::LaserPathFit& fit, double segmentLengthM,
      double lateralSpanM, const QString& label);

 private slots:
  void watchdogTick();

 private:
  enum class Phase {
    Idle,
    AwaitingInputs,
    SurveyForward,
    PauseBeforeReturn,
    SurveyReturn,
    PauseBeforeTracking,
    TrackingForward,
  };

  bool telemetryUsable(const crawling::DriveTelemetry& telemetry) const;
  bool phaseNeedsLaser() const;
  bool phaseMoves() const;
  bool wheelsStopped() const;
  void tryStartSurvey();
  void beginInitialSurvey();
  void beginTrackingSegment();
  void resetGapTracker();
  void beginSeamReacquisition(const QString& reason);
  void updateCenterErrorTrend(qint64 now);
  void resetPhaseTravel();
  void integrateWheelMotion(qint64 now, qint64 previousTelemetryMs,
                            double previousLeftSpeedMps,
                            double previousRightSpeedMps);
  bool stopIfMotionStalled(qint64 now);
  bool stopIfImageTimedOut(qint64 now);
  void advanceFromTelemetry(qint64 now);
  void appendCurrentEdgeSample();
  void logRawFrameDiagnostic(const LaserRawFrameDiagnostic& diagnostic,
                             const LaserGapDetection& detection, qint64 now);
  void queueRawFrame(const QImage& image, const LaserRawFrameDiagnostic& diagnostic,
                     const LaserGapDetection& detection,
                     const LaserGapDetectorConfig& config, qint64 now);
  bool fitCollectedPath(QString* error);
  void beginTrajectorySession();
  void queueTrajectoryImage(const LaserPathFit& fit, const QString& label);
  void resetControllerError();
  void logDetectionDiagnostic(const LaserGapDetection& detection, qint64 now);
  void resetSession();
  void setPhase(Phase phase, const QString& reason, bool writeLog = true);
  QString phaseName(Phase phase) const;
  QString runningReason() const;
  void applyCommand(double targetLinearMps, double targetAngularRadps,
                    double deltaSeconds,
                    double curvatureLimitRadPerM = -1.0);
  void commandImmediateStop(const QString& reason);
  void stop(const QString& reason);
  void publishStatus();

  LaserCorrectionSettings settings_;
  LaserCorrectionStatus status_;
  Phase phase_ = Phase::Idle;
  QElapsedTimer clock_;
  QTimer watchdog_;
  qint64 enabledAtMs_ = -1;
  qint64 phaseStartedMs_ = -1;
  qint64 lastImageMs_ = -1;
  qint64 lastValidDetectionMs_ = -1;
  qint64 lastTelemetryMs_ = -1;
  qint64 previousControlMs_ = -1;
  qint64 lastCommandMs_ = -1;
  qint64 lastControlDiagnosticMs_ = -1;
  qint64 lastDetectionDiagnosticMs_ = -1;
  qint64 lastCenterTrendSampleMs_ = -1;
  qint64 lastRawFrameDiagnosticMs_ = -1;
  qint64 lastRawFrameSaveMs_ = -1;
  bool rawFrameSavePending_ = false;
  bool rawArchiveDisabled_ = false;
  quint64 rawFramePendingSessionId_ = 0;
  quint64 rawFramePendingSequence_ = 0;
  qint64 sourceReceivedAtEpochMs_ = -1;
  quint32 sourceFrameNumber_ = 0;
  bool sourceFrameNumberValid_ = false;
  bool processingSourceFrame_ = false;
  qint64 lastSourceRejectLogMs_ = -1;
  qint64 lastDetectorDurationMs_ = -1;
  qint64 lastFrameQueueAgeMs_ = -1;
  qint64 lastWatchdogTickMs_ = -1;
  qint64 lastGuidanceFitMs_ = -1;

  bool haveTelemetry_ = false;
  double yawRad_ = 0.0;
  double leftSpeedMps_ = 0.0;
  double rightSpeedMps_ = 0.0;
  double driveAppliedLinearMps_ = 0.0;
  double driveAppliedAngularRadps_ = 0.0;
  double leftTargetMps_ = 0.0;
  double rightTargetMps_ = 0.0;
  double phaseTravelM_ = 0.0;
  double surveyTravelM_ = 0.0;
  double segmentStartYawRad_ = 0.0;
  qint64 lastMotionProgressMs_ = -1;
  double lastSampleLongitudinalM_ = -1.0;
  double latestLeftEdgeM_ = 0.0;
  double latestRightEdgeM_ = 0.0;
  double measuredLeftEdgeM_ = 0.0;
  double measuredRightEdgeM_ = 0.0;
  quint64 lastCloudCameraSequence_ = 0;
  double filteredLeftEdgeM_ = 0.0;
  double filteredRightEdgeM_ = 0.0;
  bool filteredEdgeValid_ = false;
  bool gapTrackerValid_ = false;
  bool trackedGapHorizontal_ = true;
  double trackedGapCenterRatio_ = 0.5;
  double trackedGapWidthRatio_ = 0.0;
  double trackedGapAbsoluteCenterRatio_ = 0.5;
  double referenceGapAbsoluteCenterRatio_ = -1.0;
  double trackedLineStartRatio_ = -1.0;
  double trackedLineEndRatio_ = -1.0;
  double trackedGapConfidence_ = 0.0;
  qint64 lastTrackedGapMs_ = -1;
  qint64 lastTwoEdgeDetectionMs_ = -1;
  qint64 lastDetectionRejectLogMs_ = -1;
  bool detectionHeld_ = false;
  bool detectionEdgeBreakFallback_ = false;
  double desiredHeadingRad_ = 0.0;
  double filteredYawRateRadps_ = 0.0;
  // Camera-center feedback is short-filtered and measured relative to the
  // optical scan center. Path heading comes from a longer odometry-
  // frame cloud retained across tracking segments.
  double filteredGapLateralM_ = 0.0;
  bool filteredGapValid_ = false;
  qint64 lastGapFilterMs_ = -1;
  double filteredCenterAngularRadps_ = 0.0;
  double lastCenterTrendErrorM_ = 0.0;
  double centerErrorDeltaM_ = 0.0;
  double filteredCenterErrorRateMps_ = 0.0;
  double filteredSignedCenterRateMps_ = 0.0;
  bool centerErrorTrendValid_ = false;
  double trackingReferenceGapCenterRatio_ = -1.0;
  double trackingReferenceGapAbsoluteCenterRatio_ = -1.0;
  double odometryLongitudinalM_ = 0.0;
  double odometryLateralM_ = 0.0;
  double trackingTravelM_ = 0.0;
  int lastAcceptedFitDirection_ = 0;
  int pendingFitDirection_ = 0;
  int pendingFitDirectionCount_ = 0;
  double currentLinearMps_ = 0.0;
  double currentAngularRadps_ = 0.0;
  bool angularLimitLogged_ = false;
  int trackingFitFailureCount_ = 0;
  int controlDiagnosticSequence_ = 0;
  int rawFrameDiagnosticSequence_ = 0;
  quint64 cameraFrameSequence_ = 0;
  int invalidDetectionCount_ = 0;
  int reusedDetectionCount_ = 0;
  int angularDirectionChangeCount_ = 0;
  int lastAngularDirection_ = 0;
  int steeringMismatchCount_ = 0;
  // The detector must see the same dominant two-sided gap repeatedly before
  // the first survey is allowed to move. This prevents a startup reflection
  // or one noisy frame from becoming the tracked weld identity.
  int initialGapConfirmationCount_ = 0;
  bool initialGapConfirmationHorizontal_ = true;
  double pendingInitialGapAbsoluteCenterRatio_ = 0.5;
  double pendingInitialGapWidthRatio_ = 0.0;
  bool boundaryProtectionLogged_ = false;
  bool centerRecoveryWindowActive_ = false;
  bool centerRecoveryProtectionLogged_ = false;
  bool centerResponseUntrusted_ = false;
  bool detectionDegradedLogged_ = false;
  double centerRecoveryWindowStartTravelM_ = 0.0;
  double centerRecoveryWindowStartYawRad_ = 0.0;
  bool reacquisitionPending_ = false;
  double centerRecoveryWindowStartErrorM_ = 0.0;
  double centerRecoveryBestErrorM_ = 0.0;
  int latestImageAxisLengthPx_ = 0;
  int latestLineStartPx_ = -1;
  int latestLineEndPx_ = -1;
  QVector<LaserEdgeSample> samples_;
  QVector<LaserEdgeSample> visualSamples_;
  QVector<LaserEdgeSample> guidanceSamples_;
  QVector<double> guidanceTravelSamples_;
  double lastVisualLongitudinalM_ = -1.0;
  double lastGuidanceLongitudinalM_ = -1.0;
  quint64 trajectorySessionId_ = 0;
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserCorrectionSettings)
Q_DECLARE_METATYPE(crawling::LaserCorrectionStatus)
