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
  // Internal camera calibration. It is deliberately not exposed as an
  // operator setting: the detected line span and this device calibration are
  // used together to convert image ratios to the vehicle frame.
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
  // Geometry-based steering limit. At 0.35 rad/m the chassis changes heading
  // by at most about two degrees per 100 mm, independent of travel speed.
  double maximumCurvatureRadPerM = 0.35;
  // Keep the weld near the middle of the currently visible laser span. The
  // lookahead converts lateral image error into a gentle heading correction.
  double laserCenterFeedbackGain = 0.60;
  double laserCenterLookaheadM = 0.25;
  // Keep the inner wheel moving during correction. The previous 0.45 rad/s
  // cap was too aggressive at low speed and could turn almost in place.
  double minimumInnerWheelRatio = 0.45;
  double maxAngularRadps = 0.25;
  double maxAngularAccelerationRadps2 = 0.12;
  double maxLinearAccelerationMps2 = 0.05;

  double wheelRadiusM = 0.040;
  double trackWidthM = 0.300;
  int imageTimeoutMs = 180;
  int telemetryTimeoutMs = 600;
  int detectionTimeoutMs = 500;
  int transientDetectionHoldMs = 500;
  int motionStallTimeoutMs = 5000;
  LaserGapDetectorConfig detector;
};

struct LaserCorrectionStatus {
  bool active = false;
  bool gapValid = false;
  bool horizontalLaser = true;
  double gapCenterRatio = 0.5;
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
  void processDriveTelemetry(const crawling::DriveTelemetry& telemetry);
  void trajectorySessionStarted(quint64 sessionId, const QString& directory);
  void trajectoryImageSaved(quint64 sessionId, const QString& path);
  void trajectorySaveFailed(quint64 sessionId, const QString& error);

 signals:
  void commandChanged(double linearMps, double angularRadps);
  void statusChanged(const crawling::LaserCorrectionStatus& status);
  void logMessage(const QString& message);
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
    PauseBeforeSurveyRetry,
    PauseBeforeTracking,
    TrackingForward,
  };

  bool telemetryUsable(const crawling::DriveTelemetry& telemetry) const;
  bool phaseNeedsLaser() const;
  bool phaseMoves() const;
  bool wheelsStopped() const;
  void tryStartSurvey();
  void beginInitialSurvey();
  void beginInitialSurveyRetry();
  void beginTrackingSegment();
  void resetGapTracker();
  void resetPhaseTravel();
  void integrateWheelMotion(qint64 now, qint64 previousTelemetryMs,
                            double previousLeftSpeedMps,
                            double previousRightSpeedMps);
  bool stopIfMotionStalled(qint64 now);
  void advanceFromTelemetry(qint64 now);
  void appendCurrentEdgeSample();
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
                    double deltaSeconds);
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
  bool detectionHeld_ = false;
  double desiredHeadingRad_ = 0.0;
  double filteredYawRateRadps_ = 0.0;
  double currentLinearMps_ = 0.0;
  double currentAngularRadps_ = 0.0;
  bool angularLimitLogged_ = false;
  int initialSurveyRetryCount_ = 0;
  int trackingFitFailureCount_ = 0;
  int controlDiagnosticSequence_ = 0;
  int invalidDetectionCount_ = 0;
  int reusedDetectionCount_ = 0;
  int angularDirectionChangeCount_ = 0;
  int lastAngularDirection_ = 0;
  int steeringMismatchCount_ = 0;
  QVector<LaserEdgeSample> samples_;
  QVector<LaserEdgeSample> visualSamples_;
  double lastVisualLongitudinalM_ = -1.0;
  quint64 trajectorySessionId_ = 0;
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserCorrectionSettings)
Q_DECLARE_METATYPE(crawling::LaserCorrectionStatus)
