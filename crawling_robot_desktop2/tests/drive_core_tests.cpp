#include <QtTest>

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>

#include "differential_mixer.h"
#include "drive_settings.h"
#include "laser_gap_detector.h"
#include "laser_correction_controller.h"
#include "laser_path_estimator.h"
#include "laser_seam_trajectory.h"
#include "laser_trajectory_renderer.h"
#include "mwd_rs485_protocol.h"
#include "servo_protocol.h"
#include "wheel_synchronizer.h"

namespace crawling {
namespace {

QImage gapImage(int centerX) {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  const int gapStart = centerX - 25;
  const int gapEnd = centerX + 25;
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x < gapStart; ++x) row[x] = 245;
    for (int x = gapEnd + 1; x <= 380; ++x) row[x] = 245;
  }
  return image;
}

QImage gapImageWithLine(int centerX, int lineStart, int lineEnd) {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  const int gapStart = centerX - 25;
  const int gapEnd = centerX + 25;
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = lineStart; x < gapStart; ++x) row[x] = 245;
    for (int x = gapEnd + 1; x <= lineEnd; ++x) row[x] = 245;
  }
  return image;
}

QImage displacedReflectionGapImage(bool vertical, double baselineSlope) {
  QImage image(vertical ? 120 : 400, vertical ? 400 : 120,
               QImage::Format_Grayscale8);
  image.fill(20);
  for (int axis = 20; axis <= 380; ++axis) {
    const int baseline = qRound(60.0 + baselineSlope * (axis - 200));
    const bool insideGap = axis >= 140 && axis <= 260;
    for (int thickness = -2; thickness <= 2; ++thickness) {
      const auto paint = [&](int cross, uchar intensity) {
        const int x = vertical ? cross : axis;
        const int y = vertical ? axis : cross;
        image.scanLine(y)[x] = intensity;
      };
      if (insideGap) {
        // Bright returns at other heights occupy every scan-axis column.
        // A whole-image maximum projection therefore has no dark gap at all.
        paint(baseline - 25 + thickness, 255);
        paint(baseline + 30 + thickness, 250);
      } else {
        paint(baseline + thickness, 180);
      }
    }
  }
  return image;
}

QImage shallowRaisedContourImage(bool vertical, bool keepParentStripe,
                                 bool addCompetingGap = false) {
  QImage image(vertical ? 120 : 400, vertical ? 400 : 120,
               QImage::Format_Grayscale8);
  image.fill(20);
  const auto paint = [&](int axis, int cross) {
    image.scanLine(vertical ? axis : cross)[vertical ? cross : axis] = 180;
  };
  for (int axis = 20; axis <= 380; ++axis) {
    const bool raised = axis >= 140 && axis <= 260;
    const bool competingGap = addCompetingGap && axis >= 65 && axis <= 95;
    if ((!raised || keepParentStripe) && !competingGap) {
      for (int thickness = -2; thickness <= 2; ++thickness) {
        paint(axis, 60 + thickness);
      }
    }
    if (raised) {
      // One bright sample still leaks into the baseline band. Its averaged
      // intensity is enough to look continuous, but the geometric ridge is
      // 6 px away and therefore is not on the parent stripe.
      for (int thickness = -2; thickness <= 2; ++thickness) {
        paint(axis, 66 + thickness);
      }
    }
  }
  return image;
}

DriveTelemetry enabledTelemetry(double positionM, double speedMps,
                                double wheelRadiusM) {
  DriveTelemetry telemetry;
  telemetry.state = DriveState::Enabled;
  telemetry.feedbackFresh = true;
  telemetry.left.valid = true;
  telemetry.right.valid = true;
  telemetry.left.wheelPositionRad = positionM / wheelRadiusM;
  telemetry.right.wheelPositionRad = positionM / wheelRadiusM;
  telemetry.left.wheelSpeedMps = speedMps;
  telemetry.right.wheelSpeedMps = speedMps;
  return telemetry;
}

bool feedUntilCorrectionPhase(LaserCorrectionController& controller,
                              const LaserCorrectionSettings& settings,
                              const LaserCorrectionStatus& latestStatus,
                              const QString& expectedPhase,
                              const QImage& image, double speedMps,
                              int timeoutMs = 3000) {
  QElapsedTimer deadline;
  deadline.start();
  while (latestStatus.active && latestStatus.phase != expectedPhase &&
         deadline.elapsed() < timeoutMs) {
    QTest::qWait(10);
    controller.processCameraImage(image);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, speedMps, settings.wheelRadiusM));
  }
  return latestStatus.active && latestStatus.phase == expectedPhase;
}

bool startTrackingWithMeasuredGap(LaserCorrectionController& controller,
                                  const LaserCorrectionSettings& settings,
                                  const LaserCorrectionStatus& latestStatus,
                                  const QImage& image) {
  controller.setSettings(settings);
  controller.setEnabled(true);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(image);
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  if (!feedUntilCorrectionPhase(controller, settings, latestStatus,
                                QStringLiteral("返回前停稳"), image,
                                settings.targetSpeedMps) ||
      !feedUntilCorrectionPhase(controller, settings, latestStatus,
                                QStringLiteral("原路返回"), image, 0.0) ||
      !feedUntilCorrectionPhase(controller, settings, latestStatus,
                                QStringLiteral("跟踪前停稳"), image,
                                -settings.targetSpeedMps) ||
      !feedUntilCorrectionPhase(controller, settings, latestStatus,
                                QStringLiteral("分段跟踪"), image, 0.0)) {
    return false;
  }
  controller.processCameraImage(image);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  return latestStatus.active && latestStatus.linearCommandMps > 0.0;
}

}  // namespace

class DriveCoreTests final : public QObject {
  Q_OBJECT

 private slots:
  void straightMotionCommandsBothWheelsEqually();
  void fourManualDirectionsProduceDistinctWheelSigns();
  void pivotTurnCommandsOppositeWheelDirections();
  void translatingTurnKeepsBothWheelsMovingForward();
  void translatingLeftTurnMirrorsWheelRatio();
  void speedLimitPreservesWheelRatio();
  void feedbackSynchronizerThrottlesTheFasterSide();
  void feedbackSynchronizerKeepsReverseDirection();
  void staleFeedbackDisablesSynchronization();
  void feedbackSynchronizerHoldsCorrectionBetweenSamples();
  void feedbackSynchronizerPreservesRequestedCurveRatio();
  void feedbackSynchronizerSurvivesMotorCommandCeiling();
  void feedbackSynchronizerNeverReversesWheelDirection();
  void driveSettingsMigratesLegacyGearRatioOnce();
  void imageGapDetectorFindsCenteredHorizontalGap();
  void imageGapDetectorExportsRawFrameDiagnostic();
  void imageGapDetectorFindsBaselineGapUnderDisplacedReflections();
  void imageGapDetectorPrefersSeparatedExteriorStripe();
  void imageGapDetectorHandlesTiltedAndVerticalBaseline();
  void imageGapDetectorHandlesDiagonalBaseline();
  void imageGapDetectorDiagnosticsDoNotChangeDetection();
  void imageGapDetectorUsesShallowRaisedContour();
  void imageGapDetectorRejectsParallelReflectionAsContour();
  void imageGapDetectorRecordsContourConflict();
  void imageGapDetectorContourKeepsStrictIdentityGate();
  void imageGapDetectorPrefersDominantRaisedWeldOverSmallDarkHole();
  void imageGapDetectorKeepsAbsoluteCenterWhenLineSpanChanges();
  void imageGapDetectorAllowsLocalCenterJumpWhenAbsoluteCenterStable();
  void imageGapDetectorFillsSmallHolesAndKeepsDominantGap();
  void imageGapDetectorReportsShiftedGapPosition();
  void imageGapDetectorFollowsExpectedGap();
  void imageGapDetectorKeepsExpectedAxis();
  void imageGapDetectorRejectsAbsoluteCenterJump();
  void imageGapDetectorInfersSingleVisibleEdge();
  void imageGapDetectorPreservesLegacyEdgeWidthAtSampleStep();
  void imageGapDetectorCapsSingleEdgeConfidence();
  void imageGapDetectorHandlesBoundaryGap();
  void imageGapDetectorRejectsWidthJumpWithFallback();
  void imageGapDetectorDoesNotInferContinuousLine();
  void imageGapDetectorRejectsContinuousLaserLine();
  void pathEstimatorFitsPositiveAndNegativeSlopes();
  void pathEstimatorAveragesParallelEdges();
  void pathEstimatorPreservesIndependentRotatedEdgeCoordinates();
  void pathEstimatorFallsBackForMissingEdgeCoordinates();
  void pathEstimatorRejectsIsolatedOutlier();
  void pathEstimatorRejectsInsufficientData();
  void pathEstimatorFitsShortSegment();
  void seamTrajectoryReprojectsWeldIntoCurrentCameraPose();
  void seamTrajectoryExpiresWithoutNewRealObservations();
  void seamTrajectoryRejectsStationaryAndLongExtrapolation();
  void trajectoryRendererDrawsLaserGapAndCenterLine();
  void correctionControllerAcceptsTwentyMillimeterSegment();
  void correctionControllerRejectsRepeatedAndDelayedSourceFrames();
  void correctionControllerArchivesReplayableSourceFrame();
  void correctionControllerReacquiresRejectedWiderGap();
  void correctionControllerBridgesTransientLaserDropout();
  void correctionControllerSurveysReturnsAndTracks();
  void correctionControllerAllowsAccumulatedSurveyCenterDrift();
  void correctionControllerPreservesSurveyDuringReturnReacquisition();
  void correctionControllerTracksAfterOnePoorSurveyWithMissingGap();
  void correctionControllerKeepsRequestedSurveyAndTrackingSpeed();
  void correctionControllerConfirmsRaisedContourWithoutInventingFitPoints();
  void correctionControllerProtectsFullGapEnvelope();
  void correctionControllerSupervisesDetectionDropoutSpeed_data();
  void correctionControllerSupervisesDetectionDropoutSpeed();
  void protocolEncodesAndDecodesSpeedFrames();
  void protocolAcceptsStopFeedbackFrames();
  void mwdSpeedCommandUsesLittleEndianHundredthDps();
  void mwdPositionHoldCommandsMatchProtocol();
  void mwdBrakeCommandsMatchProtocol();
  void mwdFramesValidateChecksumsAndLengths();
  void mwdStatusFeedbackParsesSignedValues();
  void protocolEncodesAndDecodesStatusFrames();
};

void DriveCoreTests::straightMotionCommandsBothWheelsEqually() {
  const auto wheel = DifferentialMixer::mix(0.15, 0.0, 0.30, 0.50);
  QCOMPARE(wheel.leftMps, 0.15);
  QCOMPARE(wheel.rightMps, 0.15);
}

void DriveCoreTests::fourManualDirectionsProduceDistinctWheelSigns() {
  const auto forward = DifferentialMixer::mix(0.15, 0.0, 0.30, 0.50);
  const auto reverse = DifferentialMixer::mix(-0.15, 0.0, 0.30, 0.50);
  const auto left = DifferentialMixer::mix(0.0, -0.8, 0.30, 0.50);
  const auto right = DifferentialMixer::mix(0.0, 0.8, 0.30, 0.50);

  QVERIFY(forward.leftMps > 0.0 && forward.rightMps > 0.0);
  QVERIFY(reverse.leftMps < 0.0 && reverse.rightMps < 0.0);
  QVERIFY(left.leftMps < 0.0 && left.rightMps > 0.0);
  QVERIFY(right.leftMps > 0.0 && right.rightMps < 0.0);
}

void DriveCoreTests::driveSettingsMigratesLegacyGearRatioOnce() {
  QCOMPARE(DriveSettings{}.motorOutputToWheelRatio, 36.0);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("drive_settings.ini"));
  QSettings persistent(path, QSettings::IniFormat);
  persistent.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 1.0);
  persistent.sync();

  DriveSettings migrated = DriveSettings::load(persistent);
  QCOMPARE(migrated.motorOutputToWheelRatio, 36.0);
  migrated.save(persistent);
  persistent.sync();
  QCOMPARE(persistent.value(QStringLiteral("drive/settingsSchemaVersion")).toInt(),
           DriveSettings::kCurrentSettingsSchemaVersion);

  DriveSettings explicitCurrent = DriveSettings::load(persistent);
  explicitCurrent.motorOutputToWheelRatio = 1.0;
  explicitCurrent.save(persistent);
  persistent.sync();
  QCOMPARE(DriveSettings::load(persistent).motorOutputToWheelRatio, 1.0);

  QSettings custom(directory.filePath(QStringLiteral("custom.ini")),
                   QSettings::IniFormat);
  custom.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 73.5);
  custom.sync();
  QCOMPARE(DriveSettings::load(custom).motorOutputToWheelRatio, 73.5);
  DriveSettings::load(custom).save(custom);
  QCOMPARE(DriveSettings::load(custom).motorOutputToWheelRatio, 73.5);

  // Every profile predating the confirmed 36:1 installation must migrate
  // both historical placeholders. Saving makes that migration one-time;
  // explicit ratios in the current schema are user choices.
  for (int schema = 0; schema < 4; ++schema) {
    for (double legacyRatio : {1.0, 100.0}) {
      QSettings legacy(directory.filePath(
                           QStringLiteral("schema%1_ratio%2.ini")
                               .arg(schema).arg(legacyRatio)),
                       QSettings::IniFormat);
      legacy.setValue(QStringLiteral("drive/settingsSchemaVersion"), schema);
      legacy.setValue(QStringLiteral("drive/motorOutputToWheelRatio"),
                      legacyRatio);
      const DriveSettings upgraded = DriveSettings::load(legacy);
      QCOMPARE(upgraded.motorOutputToWheelRatio, 36.0);
      upgraded.save(legacy);
      QCOMPARE(DriveSettings::load(legacy).motorOutputToWheelRatio, 36.0);

      legacy.setValue(QStringLiteral("drive/motorOutputToWheelRatio"),
                      legacyRatio);
      QCOMPARE(DriveSettings::load(legacy).motorOutputToWheelRatio, legacyRatio);
    }
    QSettings legacyCustom(directory.filePath(
                               QStringLiteral("schema%1_custom.ini").arg(schema)),
                           QSettings::IniFormat);
    legacyCustom.setValue(QStringLiteral("drive/settingsSchemaVersion"), schema);
    legacyCustom.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 73.5);
    QCOMPARE(DriveSettings::load(legacyCustom).motorOutputToWheelRatio, 73.5);
  }

  QSettings schema2(directory.filePath(QStringLiteral("schema2.ini")),
                    QSettings::IniFormat);
  schema2.setValue(QStringLiteral("drive/settingsSchemaVersion"), 2);
  schema2.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 1.0);
  schema2.setValue(QStringLiteral("drive/synchronizerMaxCorrection"), 0.030);
  schema2.sync();
  const DriveSettings adaptive = DriveSettings::load(schema2);
  QCOMPARE(adaptive.motorOutputToWheelRatio, 36.0);
  QCOMPARE(adaptive.synchronizer.maximumCorrectionMps, 0.300);
}

void DriveCoreTests::pivotTurnCommandsOppositeWheelDirections() {
  const auto wheel = DifferentialMixer::mix(0.0, 0.8, 0.30, 0.50);
  QCOMPARE(wheel.leftMps, 0.12);
  QCOMPARE(wheel.rightMps, -0.12);
  QCOMPARE(wheel.angularRadps, 0.8);
}

void DriveCoreTests::translatingTurnKeepsBothWheelsMovingForward() {
  // Positive angular speed is a right turn: left wheel is the outer wheel.
  const auto wheel = DifferentialMixer::mix(0.025, 0.30, 0.30, 0.50);
  QVERIFY(wheel.leftMps > 0.0);
  QVERIFY(wheel.rightMps > 0.0);
  QVERIFY(wheel.leftMps > wheel.rightMps);
  QCOMPARE(wheel.rightMps / wheel.leftMps, 0.50);
}

void DriveCoreTests::translatingLeftTurnMirrorsWheelRatio() {
  // Negative angular speed is a left turn: right wheel is the outer wheel.
  const auto wheel = DifferentialMixer::mix(0.025, -0.30, 0.30, 0.50);
  QVERIFY(wheel.leftMps > 0.0);
  QVERIFY(wheel.rightMps > 0.0);
  QVERIFY(wheel.rightMps > wheel.leftMps);
  QCOMPARE(wheel.leftMps / wheel.rightMps, 0.50);
}

void DriveCoreTests::speedLimitPreservesWheelRatio() {
  WheelTargets target;
  target.leftMps = 0.10;
  target.rightMps = 0.30;
  target.linearMps = 0.20;
  target.angularRadps = -1.0;
  const auto limited = DifferentialMixer::limitUniformly(target, 0.12);
  QCOMPARE(limited.leftMps, 0.04);
  QCOMPARE(limited.rightMps, 0.12);
  QCOMPARE(limited.leftMps / limited.rightMps, 1.0 / 3.0);
}

void DriveCoreTests::feedbackSynchronizerThrottlesTheFasterSide() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(0.27, 0.27, 0.03, 0.09,
                                          0.27, 0.27, true, 0.334,
                                          SynchronizerConfig{});
  QVERIFY(result.active);
  QVERIFY(result.normalizedError > 0.0);
  QVERIFY(result.correctionMps > 0.0);
  QCOMPARE(result.leftMps, 0.27);
  QVERIFY(result.rightMps < 0.10);
  QVERIFY(std::abs(result.leftMps * result.leftResponseFactor -
                   result.rightMps * result.rightResponseFactor) < 1e-12);
}

void DriveCoreTests::feedbackSynchronizerKeepsReverseDirection() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(-0.27, -0.27, -0.03, -0.09,
                                          -0.27, -0.27, true, 0.334,
                                          SynchronizerConfig{});
  QVERIFY(result.active);
  QCOMPARE(result.leftMps, -0.27);
  QVERIFY(result.rightMps > -0.10);
  QVERIFY(result.leftMps <= 0.0);
  QVERIFY(result.rightMps <= 0.0);
}

void DriveCoreTests::staleFeedbackDisablesSynchronization() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(0.10, 0.10, 0.0, 0.10,
                                          0.10, 0.10, false, 0.334,
                                          SynchronizerConfig{});
  QVERIFY(!result.active);
  QCOMPARE(result.leftMps, 0.10);
  QCOMPARE(result.rightMps, 0.10);
}

void DriveCoreTests::feedbackSynchronizerHoldsCorrectionBetweenSamples() {
  WheelSynchronizer synchronizer;
  const auto updated = synchronizer.update(0.10, 0.10, 0.03, 0.09,
                                           0.10, 0.10, true, 0.334,
                                           SynchronizerConfig{});
  const auto held = synchronizer.update(0.10, 0.10, 0.09, 0.03,
                                        0.10, 0.10, false, 0.334,
                                        SynchronizerConfig{});
  QVERIFY(updated.active);
  QVERIFY(held.active);
  QCOMPARE(held.normalizedError, updated.normalizedError);
  QCOMPARE(held.correctionMps, updated.correctionMps);
  QCOMPARE(held.leftMps, updated.leftMps);
  QCOMPARE(held.rightMps, updated.rightMps);
}

void DriveCoreTests::feedbackSynchronizerPreservesRequestedCurveRatio() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(0.05, 0.10, 0.015, 0.09,
                                          0.05, 0.10, true, 0.334,
                                          SynchronizerConfig{});
  QVERIFY(result.active);
  const double predictedLeft = result.leftMps * result.leftResponseFactor;
  const double predictedRight = result.rightMps * result.rightResponseFactor;
  QVERIFY(std::abs(predictedLeft / predictedRight - 0.5) < 1e-12);
}

void DriveCoreTests::feedbackSynchronizerSurvivesMotorCommandCeiling() {
  constexpr double pi = 3.14159265358979323846;
  const double motorCommandCeilingMps = 4300.0 * 0.040 * pi / 180.0 / 100.0;
  WheelTargets requested;
  requested.leftMps = 0.60;
  requested.rightMps = 0.60;
  const WheelTargets bounded =
      DifferentialMixer::limitUniformly(requested, motorCommandCeilingMps);

  WheelSynchronizer synchronizer;
  const auto synchronized = synchronizer.update(
      bounded.leftMps, bounded.rightMps,
      bounded.leftMps * 0.98, bounded.rightMps,
      bounded.leftMps, bounded.rightMps, true, 0.334,
      SynchronizerConfig{});
  WheelTargets corrected;
  corrected.leftMps = synchronized.leftMps;
  corrected.rightMps = synchronized.rightMps;
  const WheelTargets limitedAgain =
      DifferentialMixer::limitUniformly(corrected, motorCommandCeilingMps);

  QCOMPARE(limitedAgain.leftMps, corrected.leftMps);
  QCOMPARE(limitedAgain.rightMps, corrected.rightMps);
  QVERIFY(limitedAgain.rightMps < limitedAgain.leftMps);
  QVERIFY(limitedAgain.rightMps > limitedAgain.leftMps * 0.95);
}

void DriveCoreTests::feedbackSynchronizerNeverReversesWheelDirection() {
  WheelSynchronizer synchronizer;
  const auto forward = synchronizer.update(0.05, 0.06, 0.0, 0.06,
                                           0.05, 0.06, true, 0.334,
                                           SynchronizerConfig{});
  QVERIFY(forward.leftMps >= 0.0);
  QVERIFY(forward.rightMps >= 0.0);

  synchronizer.reset();
  const auto reverse = synchronizer.update(-0.05, -0.06, 0.0, -0.06,
                                           -0.05, -0.06, true, 0.334,
                                           SynchronizerConfig{});
  QVERIFY(reverse.leftMps <= 0.0);
  QVERIFY(reverse.rightMps <= 0.0);
}

void DriveCoreTests::imageGapDetectorFindsCenteredHorizontalGap() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 170; ++x) row[x] = 245;
    for (int x = 230; x <= 380; ++x) row[x] = 245;
  }

  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(result.valid);
  QVERIFY(result.horizontal);
  QVERIFY(std::abs(result.normalizedCenter - 0.5) < 0.03);
  QVERIFY(result.gapStartPx >= 168);
  QVERIFY(result.gapEndPx <= 232);
}

void DriveCoreTests::imageGapDetectorExportsRawFrameDiagnostic() {
  LaserRawFrameDiagnostic diagnostic;
  const LaserGapDetection result =
      LaserGapDetector::detect(gapImage(200), {}, &diagnostic);

  QVERIFY(result.valid);
  QVERIFY(diagnostic.available);
  QVERIFY(diagnostic.horizontal);
  QCOMPARE(diagnostic.imageWidth, 400);
  QCOMPARE(diagnostic.imageHeight, 120);
  QCOMPARE(diagnostic.sampleStepPx, 2);
  QCOMPARE(diagnostic.rawProfile.size(), 200);
  QCOMPARE(diagnostic.filteredProfile.size(), 200);
  QCOMPARE(diagnostic.presentProfile.size(), 200);
  QVERIFY(diagnostic.lineLevel > diagnostic.backgroundLevel);
  QVERIFY(diagnostic.threshold > diagnostic.backgroundLevel);
  QVERIFY(diagnostic.presentProfile.at(50));
  QVERIFY(!diagnostic.presentProfile.at(100));
  QVERIFY(diagnostic.presentProfile.at(150));
}

void DriveCoreTests::imageGapDetectorFindsBaselineGapUnderDisplacedReflections() {
  const QImage image = displacedReflectionGapImage(false, 0.0);
  // Establish that a bright pixel exists even in the middle of the weld;
  // detection must measure interruption of the original stripe instead.
  for (int x = 140; x <= 260; ++x) {
    int brightest = 0;
    for (int y = 0; y < image.height(); ++y) {
      brightest = std::max(brightest, static_cast<int>(image.constScanLine(y)[x]));
    }
    QVERIFY(brightest >= 250);
  }
  LaserRawFrameDiagnostic diagnostic;
  const LaserGapDetection result = LaserGapDetector::detect(image, {}, &diagnostic);
  QVERIFY(result.valid);
  QVERIFY(result.horizontal);
  QVERIFY(result.baselineSupported);
  QVERIFY(!result.edgeBreakFallback);
  QVERIFY(std::abs(result.gapStartPx - 140) <= 4);
  QVERIFY(std::abs(result.gapEndPx - 260) <= 4);
  QVERIFY(std::abs(result.absoluteCenterRatio - 200.0 / 399.0) < 0.01);
  QVERIFY(std::abs(result.baselineOffsetPx - 60.0) <= 3.0);
  QVERIFY(diagnostic.baselineUsed);
  const int centerSample = 200 / diagnostic.sampleStepPx;
  QVERIFY(diagnostic.fullProjectionProfile.at(centerSample) > diagnostic.threshold);
  QVERIFY(diagnostic.rawProfile.at(centerSample) < diagnostic.threshold);
  QVERIFY(!diagnostic.presentProfile.at(centerSample));
}

void DriveCoreTests::imageGapDetectorPrefersSeparatedExteriorStripe() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int x = 0; x < image.width(); ++x) {
    for (int thickness = -2; thickness <= 2; ++thickness) {
      if (x < 80 || x >= 320) {
        image.scanLine(60 + thickness)[x] = 180;
      } else {
        image.scanLine(30 + thickness)[x] = 255;
        image.scanLine(90 + thickness)[x] = 250;
      }
    }
  }
  // These remote bright pixels must not make the long central reflection
  // appear to have the broad support of both exterior parent-stripe pieces.
  // The stripe occupies only 40% of the scan, the brighter reflections 60%.
  for (int thickness = -2; thickness <= 2; ++thickness) {
    image.scanLine(30 + thickness)[0] = 255;
    image.scanLine(30 + thickness)[398] = 255;
  }
  LaserRawFrameDiagnostic diagnostic;
  const LaserGapDetection result = LaserGapDetector::detect(image, {}, &diagnostic);
  QVERIFY(result.valid);
  QVERIFY(result.horizontal);
  QVERIFY(result.baselineSupported);
  QVERIFY(!result.edgeBreakFallback);
  QVERIFY(std::abs(result.baselineOffsetPx - 60.0) <= 3.0);
  QVERIFY(std::abs(result.gapStartPx - 80) <= 4);
  QVERIFY(std::abs(result.gapEndPx - 319) <= 4);
  QVERIFY(std::abs(result.absoluteCenterRatio - 0.5) < 0.01);
  QVERIFY(diagnostic.baselineSupportRatio >= 0.38);
  QVERIFY(diagnostic.baselineSupportRatio <= 0.42);
}

void DriveCoreTests::imageGapDetectorHandlesTiltedAndVerticalBaseline() {
  for (bool vertical : {false, true}) {
    for (double slope : {-0.10, 0.10}) {
      const LaserGapDetection result =
          LaserGapDetector::detect(displacedReflectionGapImage(vertical, slope));
      QVERIFY(result.valid);
      QCOMPARE(result.horizontal, !vertical);
      QVERIFY(result.baselineSupported);
      QVERIFY(!result.edgeBreakFallback);
      QVERIFY(std::abs(result.gapStartPx - 140) <= 4);
      QVERIFY(std::abs(result.gapEndPx - 260) <= 4);
      QVERIFY(std::abs(result.absoluteCenterRatio - 200.0 / 399.0) < 0.01);
      QVERIFY(std::abs(result.baselineSlope - slope) < 0.025);
      QVERIFY(std::abs(result.baselineOffsetPx - (60.0 - 200.0 * slope)) < 3.0);
    }
  }
}

void DriveCoreTests::imageGapDetectorHandlesDiagonalBaseline() {
  QImage image(400, 400, QImage::Format_Grayscale8);
  image.fill(20);
  for (int x = 20; x <= 380; ++x) {
    if (x >= 140 && x <= 260) continue;
    for (int offset = -2; offset <= 2; ++offset) {
      image.scanLine(x + offset)[x] = 245;
    }
  }
  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(result.valid);
  QVERIFY(result.baselineSupported);
  QVERIFY(std::abs(result.baselineSlope - 1.0) < 0.025);
  QVERIFY(std::abs(result.gapStartPx - 140) <= 4);
  QVERIFY(std::abs(result.gapEndPx - 260) <= 4);
  QVERIFY(std::abs(result.absoluteCenterRatio - 200.0 / 399.0) < 0.01);
}

void DriveCoreTests::imageGapDetectorDiagnosticsDoNotChangeDetection() {
  for (bool vertical : {false, true}) {
    const QImage image = displacedReflectionGapImage(vertical, 0.10);
    const LaserGapDetection withoutDiagnostic = LaserGapDetector::detect(image);
    LaserRawFrameDiagnostic diagnostic;
    const LaserGapDetection withDiagnostic =
        LaserGapDetector::detect(image, {}, &diagnostic);
    QCOMPARE(withDiagnostic.valid, withoutDiagnostic.valid);
    QCOMPARE(withDiagnostic.horizontal, withoutDiagnostic.horizontal);
    QCOMPARE(withDiagnostic.baselineSupported, withoutDiagnostic.baselineSupported);
    QCOMPARE(withDiagnostic.gapStartPx, withoutDiagnostic.gapStartPx);
    QCOMPARE(withDiagnostic.gapEndPx, withoutDiagnostic.gapEndPx);
    QCOMPARE(withDiagnostic.lineStartPx, withoutDiagnostic.lineStartPx);
    QCOMPARE(withDiagnostic.lineEndPx, withoutDiagnostic.lineEndPx);
    QCOMPARE(withDiagnostic.absoluteCenterRatio, withoutDiagnostic.absoluteCenterRatio);
    QCOMPARE(withDiagnostic.confidence, withoutDiagnostic.confidence);
    QCOMPARE(withDiagnostic.baselineOffsetPx, withoutDiagnostic.baselineOffsetPx);
    QCOMPARE(withDiagnostic.baselineSlope, withoutDiagnostic.baselineSlope);
    QCOMPARE(withDiagnostic.edgeBreakFallback, withoutDiagnostic.edgeBreakFallback);
    QVERIFY(diagnostic.available);
  }
}

void DriveCoreTests::imageGapDetectorKeepsAbsoluteCenterWhenLineSpanChanges() {
  const LaserGapDetection fullLine =
      LaserGapDetector::detect(gapImageWithLine(200, 20, 380));
  const LaserGapDetection clippedLine =
      LaserGapDetector::detect(gapImageWithLine(200, 0, 300));

  QVERIFY(fullLine.valid);
  QVERIFY(clippedLine.valid);
  // The physical gap stays at the same raw-image pixels even though the
  // visible laser-line endpoints change. This is the coordinate used for
  // vehicle feedback and must remain stable.
  QVERIFY(std::abs(fullLine.absoluteCenterRatio -
                   clippedLine.absoluteCenterRatio) < 0.01);
  QVERIFY(std::abs(fullLine.normalizedCenter - clippedLine.normalizedCenter) >
          0.10);
}

void DriveCoreTests::imageGapDetectorUsesShallowRaisedContour() {
  for (bool vertical : {false, true}) {
    LaserRawFrameDiagnostic diagnostic;
    const LaserGapDetection result = LaserGapDetector::detect(
        shallowRaisedContourImage(vertical, false), {}, &diagnostic);
    QVERIFY(result.valid);
    QCOMPARE(result.horizontal, !vertical);
    QVERIFY(result.contourSupported);
    QVERIFY(result.contourFallback);
    QVERIFY(!result.edgeBreakFallback);
    QVERIFY(std::abs(result.gapStartPx - 140) <= 4);
    QVERIFY(std::abs(result.gapEndPx - 260) <= 4);
    QVERIFY(std::abs(result.absoluteCenterRatio - 200.0 / 399.0) < 0.01);
    QVERIFY(result.confidence > 0.05);
    QVERIFY(result.confidence <= 0.075 + 1e-9);
    QCOMPARE(diagnostic.contourProfile.size(), diagnostic.rawProfile.size());
    QVERIFY(diagnostic.contourProfile.at(100));
    QVERIFY(diagnostic.presentProfile.at(100));
    QVERIFY(diagnostic.contourDisplacementThresholdPx >= 5);
  }
}

void DriveCoreTests::imageGapDetectorRejectsParallelReflectionAsContour() {
  const LaserGapDetection result = LaserGapDetector::detect(
      shallowRaisedContourImage(false, true));
  // A second stripe beside an intact parent stripe has no measured raised
  // plate region; treating every off-band return as a weld would be unsafe.
  QVERIFY(!result.valid);
  QVERIFY(!result.contourSupported);
  QVERIFY(!result.contourFallback);
}

void DriveCoreTests::imageGapDetectorRecordsContourConflict() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int x = 20; x <= 380; ++x) {
    const bool darkGap = x >= 100 && x <= 160;
    const bool raised = x >= 230 && x <= 270;
    for (int y = 58; y <= 62; ++y) {
      if (!darkGap && !raised) image.scanLine(y)[x] = 180;
    }
    if (raised) {
      for (int y = 64; y <= 68; ++y) image.scanLine(y)[x] = 180;
    }
  }
  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(result.valid);
  QVERIFY(result.contourSupported);
  QVERIFY(result.contourConflict);
  QVERIFY(!result.contourAgreesWithGap);
  // Without a tracked weld identity there is no evidence to arbitrarily
  // replace one measured feature with another. Expose the conflict instead.
  QVERIFY(!result.contourFallback);
  QVERIFY(std::abs(result.gapStartPx - 100) <= 4);
  QVERIFY(std::abs(result.gapEndPx - 160) <= 4);
  QVERIFY(std::abs(result.contourStartPx - 230) <= 4);
  QVERIFY(std::abs(result.contourEndPx - 270) <= 4);
}

void DriveCoreTests::imageGapDetectorPrefersDominantRaisedWeldOverSmallDarkHole() {
  const auto result = LaserGapDetector::detect(
      shallowRaisedContourImage(false, false, true));
  QVERIFY(result.valid && result.contourFallback);
  QVERIFY(result.contourDominant && result.contourConflict);
  QVERIFY(std::abs(result.gapStartPx - 140) <= 4);
  QVERIFY(std::abs(result.gapEndPx - 260) <= 4);
  QVERIFY(result.confidence <= 0.075 + 1e-9);
}

void DriveCoreTests::imageGapDetectorContourKeepsStrictIdentityGate() {
  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.2;
  config.expectedAbsoluteCenterRatio = 0.2;
  config.expectedGapWidthRatio = 0.34;
  config.expectedLineStartRatio = 20.0 / 399.0;
  config.expectedLineEndRatio = 380.0 / 399.0;
  const LaserGapDetection result = LaserGapDetector::detect(
      shallowRaisedContourImage(false, false), config);
  QVERIFY(!result.valid);
  QVERIFY(!result.contourSupported);
  QVERIFY(!result.contourFallback);
}

void DriveCoreTests::imageGapDetectorAllowsLocalCenterJumpWhenAbsoluteCenterStable() {
  const LaserGapDetection previous =
      LaserGapDetector::detect(gapImageWithLine(200, 20, 380));
  QVERIFY(previous.valid);

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = previous.normalizedCenter;
  config.expectedGapWidthRatio =
      static_cast<double>(previous.gapEndPx - previous.gapStartPx + 1) /
      std::max(1, previous.lineEndPx - previous.lineStartPx + 1);
  config.expectedAbsoluteCenterRatio = previous.absoluteCenterRatio;
  config.expectedLineStartRatio = previous.lineStartPx / 399.0;
  config.expectedLineEndRatio = previous.lineEndPx / 399.0;

  // The visible laser span changes from [20, 380] to [0, 300], so the local
  // normalized center moves substantially. The physical raw-image center is
  // unchanged and must remain acceptable to the tracker.
  const LaserGapDetection clipped =
      LaserGapDetector::detect(gapImageWithLine(200, 0, 300), config);
  QVERIFY(clipped.valid);
  QVERIFY(std::abs(clipped.absoluteCenterRatio -
                   previous.absoluteCenterRatio) < 0.01);
}

void DriveCoreTests::imageGapDetectorFillsSmallHolesAndKeepsDominantGap() {
  QImage image(800, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 780; ++x) row[x] = 245;
    for (int x = 70; x <= 170; ++x) row[x] = 20;
    for (int x = 250; x <= 257; ++x) row[x] = 20;
    for (int x = 420; x <= 470; ++x) row[x] = 20;
    for (int x = 625; x <= 632; ++x) row[x] = 20;
  }

  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(result.valid);
  QVERIFY(result.horizontal);
  QVERIFY(std::abs(result.absoluteCenterRatio - 0.15) < 0.02);
  QVERIFY(result.gapStartPx >= 66);
  QVERIFY(result.gapEndPx <= 174);
}

void DriveCoreTests::imageGapDetectorReportsShiftedGapPosition() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(15);
  for (int y = 50; y <= 54; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 80; ++x) row[x] = 250;
    for (int x = 120; x <= 380; ++x) row[x] = 250;
  }

  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(result.valid);
  QVERIFY(result.normalizedCenter > 0.18);
  QVERIFY(result.normalizedCenter < 0.28);
}

void DriveCoreTests::imageGapDetectorFollowsExpectedGap() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 100; ++x) row[x] = 245;
    for (int x = 135; x <= 185; ++x) row[x] = 245;
    for (int x = 220; x <= 380; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedCenterRatio = 0.30;
  config.maximumTrackingCenterJumpRatio = 0.10;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);
  QVERIFY(result.valid);
  QVERIFY(result.normalizedCenter > 0.20);
  QVERIFY(result.normalizedCenter < 0.40);
}

void DriveCoreTests::imageGapDetectorKeepsExpectedAxis() {
  const QImage image = gapImage(200);
  LaserGapDetectorConfig config;
  config.expectedAxis = 2;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);
  QVERIFY(!result.valid);
}

void DriveCoreTests::imageGapDetectorRejectsAbsoluteCenterJump() {
  const QImage image = gapImage(100);
  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.22;
  config.expectedAbsoluteCenterRatio = 0.50;
  config.referenceAbsoluteCenterRatio = 0.50;
  config.maximumAbsoluteCenterJumpRatio = 0.06;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(!result.valid);
  QVERIFY(result.continuityRejected);
}

void DriveCoreTests::imageGapDetectorInfersSingleVisibleEdge() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 170; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.50;
  config.expectedGapWidthRatio = 0.17;
  config.expectedLineStartRatio = 0.05;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(result.valid);
  QVERIFY(result.edgeBreakFallback);
  QVERIFY(result.horizontal);
  QVERIFY(std::abs(result.normalizedCenter - 0.50) < 0.04);
  QVERIFY(result.confidence > 0.0);
}

void DriveCoreTests::imageGapDetectorPreservesLegacyEdgeWidthAtSampleStep() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  // Only the left shoulder remains visible. The tracked local width is 17%
  // of the configured line span, which must remain a physical-width estimate
  // after the detector's two-pixel sampling.
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 170; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.50;
  config.expectedGapWidthRatio = 0.17;
  config.expectedLineStartRatio = 0.05;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(result.valid);
  QVERIFY(result.edgeBreakFallback);
  const int inferredWidth = result.gapEndPx - result.gapStartPx + 1;
  // 0.17 * (380 - 20) ~= 61 px. A legacy-unit regression produced roughly
  // half this width because expectedLineSpan was divided by sampleStep twice.
  QVERIFY(inferredWidth >= 50);
  QVERIFY(inferredWidth <= 75);
}

void DriveCoreTests::imageGapDetectorCapsSingleEdgeConfidence() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 170; ++x) row[x] = 245;
  }
  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.50;
  config.expectedGapWidthRatio = 0.17;
  config.expectedLineStartRatio = 0.05;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);
  QVERIFY(result.valid);
  QVERIFY(result.edgeBreakFallback);
  QVERIFY(result.confidence <= 0.045 + 1e-9);
}

void DriveCoreTests::imageGapDetectorHandlesBoundaryGap() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 50; x <= 380; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.08;
  config.expectedGapWidthRatio = 0.12;
  config.expectedLineStartRatio = 0.0;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(result.valid);
  QVERIFY(result.edgeBreakFallback);
  QVERIFY(result.gapStartPx <= 12);
  QVERIFY(result.gapEndPx >= 40);
  QVERIFY(result.normalizedCenter < 0.14);
}

void DriveCoreTests::imageGapDetectorRejectsWidthJumpWithFallback() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 120; ++x) row[x] = 245;
    for (int x = 180; x <= 380; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.36;
  config.expectedGapWidthRatio = 0.08;
  config.expectedLineStartRatio = 0.05;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(result.valid);
  QVERIFY(result.edgeBreakFallback);
  QVERIFY(result.widthRejected);
  QVERIFY(std::abs(result.normalizedCenter - 0.36) < 0.08);
}

void DriveCoreTests::imageGapDetectorDoesNotInferContinuousLine() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 380; ++x) row[x] = 245;
  }

  LaserGapDetectorConfig config;
  config.expectedAxis = 1;
  config.expectedCenterRatio = 0.50;
  config.expectedGapWidthRatio = 0.17;
  config.expectedLineStartRatio = 0.05;
  config.expectedLineEndRatio = 0.95;
  const LaserGapDetection result = LaserGapDetector::detect(image, config);

  QVERIFY(!result.valid);
  QVERIFY(!result.edgeBreakFallback);
}

void DriveCoreTests::imageGapDetectorRejectsContinuousLaserLine() {
  QImage image(400, 120, QImage::Format_Grayscale8);
  image.fill(25);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = image.scanLine(y);
    for (int x = 20; x <= 380; ++x) row[x] = 240;
  }

  const LaserGapDetection result = LaserGapDetector::detect(image);
  QVERIFY(!result.valid);
}

void DriveCoreTests::pathEstimatorFitsShortSegment() {
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i < 4; ++i) {
    const double x = i * 0.006;
    samples.append({x, -0.030 + 0.10 * x, 0.030 + 0.10 * x});
  }

  const LaserPathFit fit = LaserPathEstimator::fit(samples, 4, 0.005, 0.002);
  QVERIFY(fit.valid);
  QCOMPARE(fit.sampleCount, 4);
  QVERIFY(std::abs(fit.spanM - 0.018) < 1e-9);
  QVERIFY(std::abs(fit.centerSlope - 0.10) < 1e-9);
}

void DriveCoreTests::pathEstimatorFitsPositiveAndNegativeSlopes() {
  for (const double expectedSlope : {0.18, -0.14}) {
    QVector<LaserEdgeSample> samples;
    for (int i = 0; i <= 40; ++i) {
      const double x = i * 0.005;
      samples.append({x, -0.045 + expectedSlope * x,
                      0.035 + expectedSlope * x});
    }
    const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.002);
    QVERIFY(fit.valid);
    QVERIFY(std::abs(fit.centerSlope - expectedSlope) < 1e-9);
    QVERIFY(std::abs(fit.leftInterceptM + 0.045) < 1e-9);
    QVERIFY(std::abs(fit.rightInterceptM - 0.035) < 1e-9);
    QVERIFY(std::abs(fit.centerInterceptM + 0.005) < 1e-9);
    QVERIFY(std::abs(fit.angleRad - std::atan(expectedSlope)) < 1e-9);
  }
}

void DriveCoreTests::pathEstimatorAveragesParallelEdges() {
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i <= 40; ++i) {
    const double x = i * 0.005;
    samples.append({x, -0.055 + 0.21 * x, 0.045 + 0.21 * x});
  }
  const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.002);
  QVERIFY(fit.valid);
  QVERIFY(std::abs(fit.leftSlope - 0.21) < 1e-9);
  QVERIFY(std::abs(fit.rightSlope - 0.21) < 1e-9);
  QVERIFY(std::abs(fit.centerSlope - 0.21) < 1e-9);
  QCOMPARE(fit.sampleCount, samples.size());
}

void DriveCoreTests::pathEstimatorPreservesIndependentRotatedEdgeCoordinates() {
  const double rotation = 0.60;
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  const double originalSlope = 0.18;
  const double expectedSlope =
      (sine + cosine * originalSlope) / (cosine - sine * originalSlope);
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i <= 40; ++i) {
    // A changing scan orientation intersects the parallel edges at different
    // positions along the seam. Rotate each actual point independently.
    const double leftX = i * 0.005;
    const double rightX = leftX * 1.6 + 0.020;
    const double leftY = -0.035 + originalSlope * leftX;
    const double rightY = 0.035 + originalSlope * rightX;
    LaserEdgeSample sample;
    sample.leftLongitudinalM = cosine * leftX - sine * leftY;
    sample.rightLongitudinalM = cosine * rightX - sine * rightY;
    sample.longitudinalM =
        (sample.leftLongitudinalM + sample.rightLongitudinalM) * 0.5;
    sample.leftLateralM = sine * leftX + cosine * leftY;
    sample.rightLateralM = sine * rightX + cosine * rightY;
    samples.append(sample);
  }
  const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.002);
  QVERIFY(fit.valid);
  QVERIFY(std::abs(fit.leftSlope - expectedSlope) < 1e-9);
  QVERIFY(std::abs(fit.rightSlope - expectedSlope) < 1e-9);
  QVERIFY(std::abs(fit.angleRad - std::atan(expectedSlope)) < 1e-9);
  QVERIFY(fit.rmsErrorM < 1e-9);

  samples[20].leftLateralM += 0.08;
  samples[20].rightLateralM += 0.08;
  const LaserPathFit robustFit =
      LaserPathEstimator::fit(samples, 12, 0.13, 0.003);
  QVERIFY(robustFit.valid);
  QCOMPARE(robustFit.sampleCount, samples.size() - 1);
  QVERIFY(std::abs(robustFit.leftSlope - expectedSlope) < 1e-9);
  QVERIFY(std::abs(robustFit.rightSlope - expectedSlope) < 1e-9);
}

void DriveCoreTests::pathEstimatorFallsBackForMissingEdgeCoordinates() {
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i <= 40; ++i) {
    const double x = i * 0.005;
    LaserEdgeSample sample{x, -0.040 + 0.12 * x, 0.040 + 0.12 * x};
    QVERIFY(std::isnan(sample.leftLongitudinalM));
    QVERIFY(std::isnan(sample.rightLongitudinalM));
    if (i % 2 == 0) {
      sample.leftLongitudinalM = x;
    } else {
      sample.rightLongitudinalM = std::numeric_limits<double>::infinity();
    }
    samples.append(sample);
  }
  const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.002);
  QVERIFY(fit.valid);
  QCOMPARE(fit.sampleCount, samples.size());
  QVERIFY(std::abs(fit.leftSlope - 0.12) < 1e-9);
  QVERIFY(std::abs(fit.rightSlope - 0.12) < 1e-9);
}

void DriveCoreTests::pathEstimatorRejectsIsolatedOutlier() {
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i <= 40; ++i) {
    const double x = i * 0.005;
    samples.append({x, -0.04 + 0.12 * x, 0.04 + 0.12 * x});
  }
  samples[20].leftLateralM += 0.08;
  samples[20].rightLateralM += 0.08;
  const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.003);
  QVERIFY(fit.valid);
  QVERIFY(std::abs(fit.centerSlope - 0.12) < 0.002);
  QCOMPARE(fit.sampleCount, samples.size() - 1);
}

void DriveCoreTests::pathEstimatorRejectsInsufficientData() {
  QVector<LaserEdgeSample> tooFew;
  for (int i = 0; i < 8; ++i) {
    const double x = i * 0.02;
    tooFew.append({x, -0.04 + 0.1 * x, 0.04 + 0.1 * x});
  }
  QVERIFY(!LaserPathEstimator::fit(tooFew, 12, 0.10, 0.003).valid);

  QVector<LaserEdgeSample> tooShort;
  for (int i = 0; i < 20; ++i) {
    const double x = i * 0.002;
    tooShort.append({x, -0.04 + 0.1 * x, 0.04 + 0.1 * x});
  }
  QVERIFY(!LaserPathEstimator::fit(tooShort, 12, 0.10, 0.003).valid);
}

void DriveCoreTests::seamTrajectoryReprojectsWeldIntoCurrentCameraPose() {
  LaserSeamTrajectory trajectory;
  for (int i = 0; i < 9; ++i) {
    trajectory.observe({i * 0.005, 0.0, 0.0}, i * 50,
                       0.010, 0.030, 0.25, 0.8);
  }
  const LaserSeamPose pose{0.041, 0.005, 0.03};
  const LaserSeamPrediction prediction =
      trajectory.predict(pose, 425, 0.25, 0.20, 1.0);
  QVERIFY(prediction.valid);
  // World seam center is y=20 mm. Rotating the complete 250 mm laser
  // lookahead moves its image coordinate even if the body translates little.
  const double expectedCenterM =
      (0.020 - pose.lateralM - std::sin(pose.yawRad) * 0.25) /
      std::cos(pose.yawRad);
  QVERIFY(std::abs(prediction.absoluteCenterRatio -
                   (0.5 + expectedCenterM / 0.20)) < 1e-8);
  QVERIFY(std::abs(prediction.absoluteWidthRatio -
                   0.020 / (std::cos(pose.yawRad) * 0.20)) < 1e-8);
  QVERIFY(prediction.sampleCount >= 5);
}

void DriveCoreTests::seamTrajectoryExpiresWithoutNewRealObservations() {
  LaserSeamTrajectory trajectory;
  for (int i = 0; i < 9; ++i) {
    trajectory.observe({i * 0.005, 0.0, 0.0}, i * 50,
                       -0.010, 0.010, 0.25, 0.8);
  }
  QVERIFY(trajectory.predict({0.040, 0.0, 0.0}, 450,
                             0.25, 0.20, 1.0).valid);
  // Calling predict is not a real observation and must not renew its age.
  trajectory.predict({0.040, 0.0, 0.0}, 2000, 0.25, 0.20, 1.0);
  const auto expired = trajectory.predict({0.040, 0.0, 0.0}, 3000,
                                           0.25, 0.20, 1.0);
  QVERIFY(!expired.valid);
  QCOMPARE(expired.observationAgeMs, qint64(2600));
  trajectory.clear();
  QVERIFY(!trajectory.predict({0.040, 0.0, 0.0}, 3000,
                              0.25, 0.20, 1.0).valid);
}

void DriveCoreTests::seamTrajectoryRejectsStationaryAndLongExtrapolation() {
  LaserSeamTrajectory stationary;
  for (int i = 0; i < 30; ++i) {
    stationary.observe({0.0, 0.0, 0.0}, i * 10,
                        -0.010, 0.010, 0.25, 0.8);
  }
  QVERIFY(!stationary.predict({0.0, 0.0, 0.0}, 300,
                              0.25, 0.20, 1.0).valid);
  LaserSeamTrajectory trajectory;
  for (int i = 0; i < 9; ++i) {
    trajectory.observe({i * 0.005, 0.0, 0.0}, i * 50,
                       -0.010, 0.010, 0.25, 0.8);
  }
  const auto far = trajectory.predict({0.12, 0.0, 0.0}, 425,
                                       0.25, 0.20, 1.0);
  QVERIFY(!far.valid);
  QVERIFY(far.extrapolationM > 0.06);
}

void DriveCoreTests::trajectoryRendererDrawsLaserGapAndCenterLine() {
  QVector<LaserEdgeSample> samples;
  for (int i = 0; i <= 40; ++i) {
    const double x = i * 0.005;
    samples.append({x, -0.04 + 0.10 * x, 0.04 + 0.10 * x});
  }
  const LaserPathFit fit = LaserPathEstimator::fit(samples, 12, 0.13, 0.002);
  QVERIFY(fit.valid);
  LaserTrajectorySegment segment;
  segment.scanSamples = samples;
  segment.fit = fit;
  segment.segmentLengthM = 0.20;
  const QImage image = LaserTrajectoryRenderer::render({segment}, 0.20);
  QVERIFY(!image.isNull());
  QVERIFY(image.width() >= 700);
  QVERIFY(image.height() >= 700);

  int redPixels = 0;
  int greenPixels = 0;
  for (int y = 0; y < image.height(); ++y) {
    const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QColor color = QColor::fromRgba(row[x]);
      if (color.red() > color.green() + 35 &&
          color.red() > color.blue() + 20) {
        ++redPixels;
      }
      if (color.green() > color.red() + 35 &&
          color.green() > color.blue() + 20) {
        ++greenPixels;
      }
    }
  }
  QVERIFY(redPixels > 500);
  QVERIFY(greenPixels > 20);
}

void DriveCoreTests::correctionControllerAcceptsTwentyMillimeterSegment() {
  LaserCorrectionController controller;
  QSignalSpy logSpy(&controller, &LaserCorrectionController::logMessage);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.02;
  controller.setSettings(settings);
  controller.setEnabled(true);

  QString combinedLog;
  for (const QList<QVariant>& arguments : logSpy) {
    if (!arguments.isEmpty()) combinedLog += arguments.first().toString();
  }
  QVERIFY(combinedLog.contains(QStringLiteral("分段 20.0 mm")));
  QVERIFY(combinedLog.contains(QStringLiteral("采样间距 1.5 mm")));
  QVERIFY(combinedLog.contains(QStringLiteral("最少样本 4")));
  QVERIFY(combinedLog.contains(QStringLiteral("最大拟合RMS 3.0 mm")));
  QVERIFY(combinedLog.contains(QStringLiteral("滚动窗口 80 mm")));
  QVERIFY(combinedLog.contains(QStringLiteral("滚动拟合最小跨度 35 mm")));
  QVERIFY(combinedLog.contains(QStringLiteral("单段航向最多更新 1.55")));
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerRejectsRepeatedAndDelayedSourceFrames() {
  LaserCorrectionController controller;
  LaserCorrectionSettings settings;
  controller.setSettings(settings);
  LaserCorrectionStatus latestStatus;
  int publishedCount = 0;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus, &publishedCount](const LaserCorrectionStatus& status) {
            latestStatus = status;
            ++publishedCount;
          });
  controller.setEnabled(true);
  for (quint32 frame : {98u, 99u}) {
    controller.processCameraFrame(gapImage(200), frame,
                                  QDateTime::currentMSecsSinceEpoch());
  }
  controller.processCameraFrame(gapImage(200), 100,
                                QDateTime::currentMSecsSinceEpoch());
  QVERIFY(latestStatus.gapValid);
  const double firstCenter = latestStatus.gapAbsoluteCenterRatio;
  const int initialPublishedCount = publishedCount;

  // A repeated SDK identity must not be counted as a fresh observation,
  // even if the queued payload differs from the previously decoded image.
  controller.processCameraFrame(gapImage(220), 100,
                                QDateTime::currentMSecsSinceEpoch());
  QCOMPARE(publishedCount, initialPublishedCount);
  QCOMPARE(latestStatus.gapAbsoluteCenterRatio, firstCenter);

  controller.processCameraFrame(
      gapImage(220), 101,
      QDateTime::currentMSecsSinceEpoch() - settings.imageTimeoutMs - 1000);
  QCOMPARE(publishedCount, initialPublishedCount);
  QCOMPARE(latestStatus.gapAbsoluteCenterRatio, firstCenter);

  // A rejected old payload must not consume its identity: the corresponding
  // timely source frame can still be accepted and update the measurement.
  controller.processCameraFrame(gapImage(220), 101,
                                QDateTime::currentMSecsSinceEpoch());
  QVERIFY(publishedCount > initialPublishedCount);
  QVERIFY(latestStatus.gapValid);
  QVERIFY(latestStatus.gapAbsoluteCenterRatio > firstCenter + 0.035);
  QVERIFY(std::abs(latestStatus.gapAbsoluteCenterRatio - 220.0 / 399.0) < 0.01);
}

void DriveCoreTests::correctionControllerArchivesReplayableSourceFrame() {
  LaserCorrectionController controller;
  QSignalSpy snapshotSpy(&controller, &LaserCorrectionController::rawFrameReady);
  controller.setEnabled(true);
  QImage source = displacedReflectionGapImage(false, 0.0);
  const QImage expectedImage = source.copy();
  const qint64 receivedAt = QDateTime::currentMSecsSinceEpoch();
  controller.processCameraFrame(source, 701, receivedAt);
  QCOMPARE(snapshotSpy.size(), 1);
  const QList<QVariant> snapshot = snapshotSpy.first();
  QCOMPARE(snapshot.size(), 4);
  // Queued archival must retain the complete exact source pixels while the
  // producer is free to reuse or release its own image buffer.
  source.fill(0);
  const QImage archivedImage = qvariant_cast<QImage>(snapshot.at(2));
  QVERIFY(archivedImage == expectedImage);
  const QJsonDocument document =
      QJsonDocument::fromJson(snapshot.at(3).toString().toUtf8());
  QVERIFY(document.isObject());
  const QJsonObject metadata = document.object();
  QCOMPARE(metadata.value(QStringLiteral("source_frame")).toString(),
           QStringLiteral("701"));
  QCOMPARE(metadata.value(QStringLiteral("source_received_epoch_ms")).toString(),
           QString::number(receivedAt));
  QCOMPARE(metadata.value(QStringLiteral("camera_seq")).toString().toULongLong(),
           snapshot.at(1).toULongLong());
  QVERIFY(metadata.value(QStringLiteral("baseline_supported")).toBool());
  const LaserGapDetection replay = LaserGapDetector::detect(archivedImage);
  QVERIFY(replay.valid);
  QCOMPARE(metadata.value(QStringLiteral("gap_start_px")).toInt(), replay.gapStartPx);
  QCOMPARE(metadata.value(QStringLiteral("gap_end_px")).toInt(), replay.gapEndPx);
  QVERIFY(std::abs(metadata.value(QStringLiteral("center_ratio")).toDouble() -
                   replay.absoluteCenterRatio) < 1e-12);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerReacquiresRejectedWiderGap() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  QSignalSpy diagnosticSpy(&controller,
                           &LaserCorrectionController::diagnosticLogMessage);
  QSignalSpy commandSpy(&controller,
                        &LaserCorrectionController::commandChanged);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.10;
  settings.targetSpeedMps = 0.01;
  settings.maxLinearAccelerationMps2 = 100.0;
  settings.detectionTimeoutMs = 200;
  settings.transientDetectionHoldMs = 100;
  settings.detectionRecoveryTimeoutMs = 1000;
  settings.motionStallTimeoutMs = 5000;
  settings.detector.allowEdgeBreakFallback = false;
  controller.setSettings(settings);
  controller.setEnabled(true);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(gapImage(200));
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(latestStatus.gapValid);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  const int oldStart = latestStatus.gapStartPx;
  const int oldEnd = latestStatus.gapEndPx;
  const double oldCenter = latestStatus.gapAbsoluteCenterRatio;

  QImage widerGap = gapImage(250);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = widerGap.scanLine(y);
    for (int x = 190; x <= 310; ++x) row[x] = 20;
  }
  const LaserGapDetection unconstrained = LaserGapDetector::detect(widerGap);
  QVERIFY(unconstrained.valid);
  QVERIFY(!unconstrained.edgeBreakFallback);
  QVERIFY(unconstrained.gapEndPx - unconstrained.gapStartPx > 110);

  // The wider, shifted real gap fails the old identity's width/position gates.
  // Keep delivering fresh images and telemetry until recovery releases those
  // gates; an old center must never masquerade as a measurement of this gap.
  bool reacquiring = false;
  for (int frame = 0; frame < 30 && !reacquiring; ++frame) {
    QTest::qWait(50);
    controller.processCameraImage(widerGap);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
    QVERIFY(latestStatus.active);
    QVERIFY(latestStatus.detectionHeld);
    QCOMPARE(latestStatus.gapStartPx, oldStart);
    QCOMPARE(latestStatus.gapEndPx, oldEnd);
    QCOMPARE(latestStatus.gapAbsoluteCenterRatio, oldCenter);
    QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
    for (const QList<QVariant>& arguments : diagnosticSpy) {
      if (arguments.first().toString().contains(
              QStringLiteral("event=seam_reacquisition "))) {
        reacquiring = true;
        break;
      }
    }
  }
  QVERIFY(reacquiring);

  // Interrupt confirmation with a failed frame. The controller must demand
  // three consecutive real pairs again, while continuing forward throughout.
  QImage blank(400, 120, QImage::Format_Grayscale8);
  blank.fill(20);
  controller.processCameraImage(blank);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(latestStatus.detectionHeld);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  for (int frame = 0; frame < 2; ++frame) {
    controller.processCameraImage(widerGap);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
    QVERIFY(latestStatus.detectionHeld);
    QCOMPARE(latestStatus.gapStartPx, oldStart);
    QCOMPARE(latestStatus.gapEndPx, oldEnd);
    QCOMPARE(latestStatus.gapAbsoluteCenterRatio, oldCenter);
    QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  }
  controller.processCameraImage(widerGap);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(latestStatus.active);
  QVERIFY(latestStatus.gapValid);
  QVERIFY(!latestStatus.detectionHeld);
  QVERIFY(!latestStatus.edgeBreakFallback);
  QCOMPARE(latestStatus.gapStartPx, unconstrained.gapStartPx);
  QCOMPARE(latestStatus.gapEndPx, unconstrained.gapEndPx);
  QVERIFY(std::abs(latestStatus.gapAbsoluteCenterRatio -
                   unconstrained.absoluteCenterRatio) < 1e-9);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerBridgesTransientLaserDropout() {
  LaserCorrectionController controller;
  QSignalSpy logSpy(&controller, &LaserCorrectionController::logMessage);
  QSignalSpy rawDiagnosticSpy(
      &controller, &LaserCorrectionController::diagnosticLogMessage);
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.10;
  settings.targetSpeedMps = 0.01;
  settings.maxLinearAccelerationMps2 = 100.0;
  settings.maxAngularAccelerationRadps2 = 100.0;
  settings.transientDetectionHoldMs = 500;
  controller.setSettings(settings);
  QSignalSpy commandSpy(&controller,
                        &LaserCorrectionController::commandChanged);

  controller.setEnabled(true);
  controller.processCameraImage(gapImage(200));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);
  controller.processCameraImage(gapImage(200));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);
  controller.processCameraImage(gapImage(200));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);

  QImage continuousLine(400, 120, QImage::Format_Grayscale8);
  continuousLine.fill(20);
  for (int y = 58; y <= 62; ++y) {
    uchar* row = continuousLine.scanLine(y);
    for (int x = 20; x <= 380; ++x) row[x] = 245;
  }
  controller.processCameraImage(continuousLine);

  QVERIFY(latestStatus.gapValid);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  // Keep fresh raw images arriving beyond the ordinary hold interval while
  // every frame lacks a detectable gap. Tracking/survey motion must degrade
  // to the previous center and stitched path instead of pulsing to zero.
  for (int i = 0; i < 14; ++i) {
    QTest::qWait(50);
    controller.processCameraImage(continuousLine);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  }
  QVERIFY(latestStatus.active);
  QVERIFY(latestStatus.gapValid);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  QString combinedLog;
  for (const QList<QVariant>& arguments : logSpy) {
    if (!arguments.isEmpty()) combinedLog += arguments.first().toString();
  }
  QVERIFY(combinedLog.contains(QStringLiteral("连续运行降级")));
  QString rawDiagnosticLog;
  for (const QList<QVariant>& arguments : rawDiagnosticSpy) {
    if (!arguments.isEmpty()) {
      rawDiagnosticLog += arguments.first().toString();
    }
  }
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("event=laser_raw_log_format")));
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("event=laser_raw_frame")));
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("mode=no_supported_gap")));
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("present_runs=")));
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("raw_q4=")));
  QVERIFY(rawDiagnosticLog.contains(QStringLiteral("smooth_q4=")));
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerSurveysReturnsAndTracks() {
  LaserCorrectionController controller;
  QSignalSpy logSpy(&controller, &LaserCorrectionController::logMessage);
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.10;
  settings.sampleSpacingM = 0.001;
  settings.minimumFitSamples = 8;
  settings.minimumFitSpanRatio = 0.50;
  settings.settleTimeMs = 100;
  settings.targetSpeedMps = 0.5;
  settings.maxLinearAccelerationMps2 = 100.0;
  settings.maxAngularAccelerationRadps2 = 100.0;
  settings.imageTimeoutMs = 180;
  settings.telemetryTimeoutMs = 1000;
  settings.detectionTimeoutMs = 1000;
  controller.setSettings(settings);

  QSignalSpy commandSpy(&controller,
                        &LaserCorrectionController::commandChanged);
  controller.setEnabled(true);
  for (int i = 0; i < 3; ++i) {
    controller.processCameraImage(gapImage(200));
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);

  // Absolute wheel positions deliberately remain frozen. Segment progress
  // must come from fresh wheel-speed integration so encoder scale/sign errors
  // cannot leave the survey moving forward forever.
  for (int i = 1; i <= 110; ++i) {
    QTest::qWait(2);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, settings.targetSpeedMps,
                         settings.wheelRadiusM));
    controller.processCameraImage(gapImage(200 + i / 6));
  }
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);

  // Paused survey/return phases do not need a laser frame. This wait exceeds
  // the moving-phase image timeout and must not end the correction session.
  QTest::qWait(210);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(commandSpy.last().at(0).toDouble() < 0.0);

  for (int i = 0; i < 110; ++i) {
    QTest::qWait(2);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, -settings.targetSpeedMps,
                         settings.wheelRadiusM));
  }
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);

  QTest::qWait(210);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);

  controller.processCameraImage(gapImage(205));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  // Tracking now targets the full raw-image center instead of preserving the
  // survey endpoint. A 205 px gap in a 400 px image therefore has a small,
  // intentional return-to-center error.
  QVERIFY(std::abs(latestStatus.laserCenterErrorM) < 0.004);
  // The installed camera/drive calibration maps a gap right of the raw image
  // center to the positive right-turn command that brings it back toward the
  // optical center. The sign is shared by the live loop and fitted cloud.
  QVERIFY(latestStatus.laserCenterErrorM > 0.0);
  QVERIFY(commandSpy.last().at(1).toDouble() > 0.0);
  const double trackingAbsoluteCenter = latestStatus.gapAbsoluteCenterRatio;
  // Changing only the visible laser-line endpoints must not create a vehicle
  // lateral error when the gap remains at the same raw-image pixels.
  controller.processCameraImage(gapImageWithLine(205, 0, 300));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(std::abs(latestStatus.gapAbsoluteCenterRatio -
                   trackingAbsoluteCenter) < 0.01);
  QVERIFY(std::abs(latestStatus.laserCenterErrorM) < 0.003);
  QString combinedLog;
  for (const QList<QVariant>& arguments : logSpy) {
    if (!arguments.isEmpty()) combinedLog += arguments.first().toString();
  }
  QVERIFY(combinedLog.contains(QStringLiteral("跟踪横向目标已设为激光全幅中心")));
  QVERIFY(std::abs(commandSpy.last().at(1).toDouble()) > 0.001);
  const double commandedLinear = commandSpy.last().at(0).toDouble();
  const double commandedAngular = commandSpy.last().at(1).toDouble();
  const double commandedLeft =
      commandedLinear + commandedAngular * settings.trackWidthM * 0.5;
  const double commandedRight =
      commandedLinear - commandedAngular * settings.trackWidthM * 0.5;
  const double innerWheelRatio =
      std::min(std::abs(commandedLeft), std::abs(commandedRight)) /
      std::max(std::abs(commandedLeft), std::abs(commandedRight));
  QVERIFY(innerWheelRatio >= 0.45 - 1e-6);

  const double firstFitAngleRad = latestStatus.fittedAngleRad;
  QVERIFY(std::abs(firstFitAngleRad) > 0.01);
  QVERIFY(std::abs(commandSpy.last().at(1).toDouble()) <=
          settings.maxAngularRadps + 0.001);
  QCOMPARE(latestStatus.cycleCount, 1);
  for (int i = 1; i <= 110; ++i) {
    QTest::qWait(2);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, settings.targetSpeedMps,
                         settings.wheelRadiusM));
    controller.processCameraImage(gapImage(205 + i / 3));
  }

  QCOMPARE(latestStatus.cycleCount, 2);
  QVERIFY(latestStatus.active);
  QVERIFY(std::abs(latestStatus.fittedAngleRad - firstFitAngleRad) > 0.01);
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
}

void DriveCoreTests::correctionControllerAllowsAccumulatedSurveyCenterDrift() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.10;
  settings.targetSpeedMps = 0.20;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  settings.detector.allowEdgeBreakFallback = false;
  controller.setSettings(settings);
  controller.setEnabled(true);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(gapImage(200));
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QCOMPARE(latestStatus.phase, QStringLiteral("首段采集"));

  // Each displacement is small, but the accumulated 80 px exceeds the old
  // fixed startup-reference gate of 10% of the 400 px image. Every accepted
  // center must be the current measurement, never the held startup gap.
  for (int center = 204; center <= 280; center += 4) {
    controller.processCameraImage(gapImage(center));
    QVERIFY(latestStatus.gapValid);
    QVERIFY(!latestStatus.detectionHeld);
    QVERIFY(!latestStatus.edgeBreakFallback);
    QVERIFY(std::abs(latestStatus.gapAbsoluteCenterRatio - center / 399.0) <
            0.005);
  }
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("返回前停稳"), gapImage(280),
                                  settings.targetSpeedMps));
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("原路返回"), gapImage(280),
                                  0.0));

  // The same continuity rule must hold on return, including movement past
  // the startup center in the opposite direction.
  for (int center = 276; center >= 120; center -= 4) {
    controller.processCameraImage(gapImage(center));
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
    QCOMPARE(latestStatus.phase, QStringLiteral("原路返回"));
    QVERIFY(latestStatus.gapValid);
    QVERIFY(!latestStatus.detectionHeld);
    QVERIFY(!latestStatus.edgeBreakFallback);
    QVERIFY(std::abs(latestStatus.gapAbsoluteCenterRatio - center / 399.0) <
            0.005);
  }
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerPreservesSurveyDuringReturnReacquisition() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  QSignalSpy diagnosticSpy(&controller,
                           &LaserCorrectionController::diagnosticLogMessage);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.04;
  settings.sampleSpacingM = 0.001;
  settings.targetSpeedMps = 0.10;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  settings.detectionTimeoutMs = 200;
  settings.transientDetectionHoldMs = 100;
  settings.detectionRecoveryTimeoutMs = 1000;
  settings.detector.allowEdgeBreakFallback = false;
  controller.setSettings(settings);
  controller.setEnabled(true);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(gapImage(200));
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("返回前停稳"), gapImage(200),
                                  settings.targetSpeedMps));
  const int collectedBeforeReturn = latestStatus.collectedSamples;
  QVERIFY(collectedBeforeReturn >= 6);
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("原路返回"), gapImage(200),
                                  0.0));

  QImage widerGap = gapImage(250);
  for (int y = 58; y <= 62; ++y) {
    for (int x = 190; x <= 310; ++x) widerGap.scanLine(y)[x] = 20;
  }
  const LaserGapDetection replacement = LaserGapDetector::detect(widerGap);
  QVERIFY(replacement.valid && !replacement.edgeBreakFallback);
  bool reacquiring = false;
  QElapsedTimer recoveryDeadline;
  recoveryDeadline.start();
  while (!reacquiring && recoveryDeadline.elapsed() < 2000) {
    QTest::qWait(25);
    controller.processCameraImage(widerGap);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
    QVERIFY(latestStatus.active);
    QCOMPARE(latestStatus.phase, QStringLiteral("原路返回"));
    QCOMPARE(latestStatus.collectedSamples, collectedBeforeReturn);
    for (const QList<QVariant>& arguments : diagnosticSpy) {
      const QString message = arguments.first().toString();
      if (message.contains(QStringLiteral("event=seam_reacquisition "))) {
        QVERIFY(message.contains(QStringLiteral("survey_preserved=1")));
        reacquiring = true;
      }
    }
  }
  QVERIFY(reacquiring);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(widerGap);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
    QCOMPARE(latestStatus.collectedSamples, collectedBeforeReturn);
  }
  QVERIFY(latestStatus.gapValid);
  QVERIFY(!latestStatus.detectionHeld);
  QCOMPARE(latestStatus.gapStartPx, replacement.gapStartPx);
  QCOMPARE(latestStatus.gapEndPx, replacement.gapEndPx);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerTracksAfterOnePoorSurveyWithMissingGap() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  QStringList phases;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus, &phases](const LaserCorrectionStatus& status) {
            latestStatus = status;
            if (phases.isEmpty() || phases.last() != status.phase) {
              phases.append(status.phase);
            }
          });
  QSignalSpy diagnosticSpy(&controller,
                           &LaserCorrectionController::diagnosticLogMessage);
  QSignalSpy commandSpy(&controller, &LaserCorrectionController::commandChanged);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.04;
  settings.targetSpeedMps = 0.10;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  settings.transientDetectionHoldMs = 100;
  settings.detector.allowEdgeBreakFallback = false;
  controller.setSettings(settings);
  controller.setEnabled(true);
  for (int frame = 0; frame < 3; ++frame) {
    controller.processCameraImage(gapImage(200));
  }
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QImage blank(400, 120, QImage::Format_Grayscale8);
  blank.fill(20);
  QVERIFY(!LaserGapDetector::detect(blank).valid);

  // Fresh images keep arriving, but none supplies a usable edge pair. The
  // survey must still finish at the requested distance and return only once.
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("返回前停稳"), blank,
                                  settings.targetSpeedMps));
  QCOMPARE(commandSpy.last().at(0).toDouble(), 0.0);
  QVERIFY(latestStatus.collectedSamples < 4);
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("原路返回"), blank, 0.0));
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("跟踪前停稳"), gapImage(220),
                                  -settings.targetSpeedMps));
  bool poorSurveyReported = false;
  for (const QList<QVariant>& arguments : diagnosticSpy) {
    const QString message = arguments.first().toString();
    if (message.contains(QStringLiteral("event=initial_survey_complete "))) {
      QVERIFY(message.contains(QStringLiteral("fit_valid=0")));
      QVERIFY(message.contains(QStringLiteral("retry=0")));
      poorSurveyReported = true;
    }
  }
  QVERIFY(poorSurveyReported);
  QVERIFY(feedUntilCorrectionPhase(controller, settings, latestStatus,
                                  QStringLiteral("分段跟踪"), gapImage(220),
                                  0.0));
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, 0.0, settings.wheelRadiusM));
  QVERIFY(commandSpy.last().at(0).toDouble() > 0.0);
  // An unavailable fitted heading must not disable the independent live
  // centering loop: a right-side gap still requests a gentle positive turn.
  QVERIFY(commandSpy.last().at(1).toDouble() > 0.0);
  QCOMPARE(phases.count(QStringLiteral("首段采集")), 1);
  QCOMPARE(phases.count(QStringLiteral("原路返回")), 1);
  QVERIFY(latestStatus.active);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerKeepsRequestedSurveyAndTrackingSpeed() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  QSignalSpy commandSpy(&controller, &LaserCorrectionController::commandChanged);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.02;
  settings.targetSpeedMps = 0.030;
  settings.maxLinearAccelerationMps2 = 100.0;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  settings.detector.allowEdgeBreakFallback = false;
  const QImage image = gapImage(220);
  QVERIFY(startTrackingWithMeasuredGap(controller, settings, latestStatus, image));

  // The survey and return may stop to change direction, but moving commands
  // at this off-center, fully observed weld must retain the requested speed.
  bool sawForwardSurvey = false;
  bool sawReturn = false;
  for (const QList<QVariant>& command : commandSpy) {
    const double linear = command.at(0).toDouble();
    if (std::abs(linear) <= 1e-9) continue;
    QVERIFY(std::abs(std::abs(linear) - settings.targetSpeedMps) < 1e-8);
    sawForwardSurvey = sawForwardSurvey || linear > 0.0;
    sawReturn = sawReturn || linear < 0.0;
  }
  QVERIFY(sawForwardSurvey);
  QVERIFY(sawReturn);

  // Test the actual speed-setting route again during live steering. A valid
  // lateral offset changes differential wheel speed, not the mean target.
  settings.targetSpeedMps = 0.045;
  controller.setSettings(settings);
  commandSpy.clear();
  controller.processCameraImage(image);
  controller.processDriveTelemetry(
      enabledTelemetry(0.0, settings.targetSpeedMps, settings.wheelRadiusM));
  QElapsedTimer tracking;
  tracking.start();
  while (tracking.elapsed() < 400) {
    QTest::qWait(20);
    controller.processCameraImage(image);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, settings.targetSpeedMps, settings.wheelRadiusM));
    QVERIFY(latestStatus.active);
    QCOMPARE(latestStatus.phase, QStringLiteral("分段跟踪"));
    QVERIFY(!latestStatus.detectionHeld);
  }
  QVERIFY(!commandSpy.isEmpty());
  bool sawSteering = false;
  for (const QList<QVariant>& command : commandSpy) {
    QVERIFY(std::abs(command.at(0).toDouble() - settings.targetSpeedMps) < 1e-8);
    sawSteering = sawSteering || std::abs(command.at(1).toDouble()) > 1e-6;
  }
  QVERIFY(sawSteering);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerConfirmsRaisedContourWithoutInventingFitPoints() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latest;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latest](const LaserCorrectionStatus& status) { latest = status; });
  QSignalSpy diagnosticSpy(&controller, &LaserCorrectionController::diagnosticLogMessage);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.02;
  settings.targetSpeedMps = 0.030;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  const QImage contour = shallowRaisedContourImage(false, false);
  QVERIFY(LaserGapDetector::detect(contour).contourFallback);
  controller.setSettings(settings);
  controller.setEnabled(true);
  controller.processDriveTelemetry(enabledTelemetry(0, 0, settings.wheelRadiusM));
  for (int frame = 0; frame < 4; ++frame) {
    controller.processCameraImage(contour);
    QCOMPARE(latest.phase, QStringLiteral("等待输入"));
    QCOMPARE(latest.linearCommandMps, 0.0);
  }
  controller.processCameraImage(contour);
  QCOMPARE(latest.phase, QStringLiteral("首段采集"));
  QVERIFY(latest.contourFallback);
  QCOMPARE(latest.collectedSamples, 0);
  controller.setEnabled(false);

  // First establish a real two-edge identity of the same physical width.
  // A sustained raised contour must stay usable after the old 1.8 s timer,
  // while all geometric fit points remain from the real observations only.
  QVERIFY(startTrackingWithMeasuredGap(controller, settings, latest,
                                      displacedReflectionGapImage(false, 0.0)));
  const int realSamples = latest.collectedSamples;
  diagnosticSpy.clear();
  QElapsedTimer interval;
  interval.start();
  while (interval.elapsed() < 2100) {
    QTest::qWait(25);
    controller.processCameraImage(contour);
    controller.processDriveTelemetry(enabledTelemetry(0, 0, settings.wheelRadiusM));
    QVERIFY(latest.active && latest.gapValid);
    QVERIFY(latest.contourFallback && !latest.detectionHeld);
    QVERIFY(latest.linearCommandMps > 0.0);
    QCOMPARE(latest.collectedSamples, realSamples);
  }
  for (const QList<QVariant>& entry : diagnosticSpy) {
    QVERIFY(!entry.first().toString().contains(QStringLiteral("event=seam_reacquisition ")));
  }
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerProtectsFullGapEnvelope() {
  LaserCorrectionController controller;
  LaserCorrectionStatus latest;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latest](const LaserCorrectionStatus& status) { latest = status; });
  QSignalSpy diagnostics(&controller, &LaserCorrectionController::diagnosticLogMessage);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.02;
  settings.targetSpeedMps = 0.030;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  const QImage image = gapImageWithLine(340, 0, 399);
  const auto detection = LaserGapDetector::detect(image);
  QVERIFY(detection.valid && !detection.edgeBreakFallback);
  QVERIFY(1.0 - detection.absoluteCenterRatio > 0.10);
  QVERIFY((399.0 - detection.gapEndPx) / 399.0 < 0.10);
  QVERIFY(startTrackingWithMeasuredGap(controller, settings, latest, image));
  QElapsedTimer interval;
  interval.start();
  while (interval.elapsed() < 600) {
    QTest::qWait(20);
    controller.processCameraImage(image);
    controller.processDriveTelemetry(enabledTelemetry(0, 0, settings.wheelRadiusM));
  }
  QVERIFY(latest.active);
  QVERIFY(latest.linearCommandMps < settings.targetSpeedMps * 0.95);
  QVERIFY(latest.linearCommandMps >= settings.targetSpeedMps * 0.45 - 1e-8);
  QVERIFY(latest.angularCommandRadps > 0.0);
  bool sawEnvelope = false;
  const QString prefix = QStringLiteral("event=laser_control_output json=");
  for (const QList<QVariant>& entry : diagnostics) {
    const QString text = entry.first().toString();
    if (!text.startsWith(prefix)) continue;
    const auto record = QJsonDocument::fromJson(text.mid(prefix.size()).toUtf8()).object();
    QVERIFY(record.value(QStringLiteral("boundary_envelope_margin_ratio")).toDouble() < 0.10);
    sawEnvelope = true;
  }
  QVERIFY(sawEnvelope);
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerSupervisesDetectionDropoutSpeed_data() {
  QTest::addColumn<int>("dropoutMs");
  QTest::addColumn<double>("minimumSpeedRatio");
  QTest::addColumn<double>("finalMaximumSpeedRatio");
  QTest::newRow("short-held-frame-keeps-requested-speed") << 250 << 1.0 << 1.0;
  QTest::newRow("long-dropout-has-one-nonzero-floor") << 3400 << 0.45 << 0.46;
}

void DriveCoreTests::correctionControllerSupervisesDetectionDropoutSpeed() {
  QFETCH(int, dropoutMs);
  QFETCH(double, minimumSpeedRatio);
  QFETCH(double, finalMaximumSpeedRatio);
  LaserCorrectionController controller;
  LaserCorrectionStatus latestStatus;
  connect(&controller, &LaserCorrectionController::statusChanged,
          [&latestStatus](const LaserCorrectionStatus& status) {
            latestStatus = status;
          });
  QSignalSpy commandSpy(&controller, &LaserCorrectionController::commandChanged);
  QSignalSpy diagnosticSpy(&controller,
                           &LaserCorrectionController::diagnosticLogMessage);
  LaserCorrectionSettings settings;
  settings.segmentLengthM = 0.02;
  settings.targetSpeedMps = 0.030;
  settings.maxLinearAccelerationMps2 = 100.0;
  settings.settleTimeMs = 100;
  settings.imageTimeoutMs = 1000;
  settings.telemetryTimeoutMs = 1000;
  settings.transientDetectionHoldMs = 500;
  settings.detector.allowEdgeBreakFallback = false;
  QVERIFY(startTrackingWithMeasuredGap(controller, settings, latestStatus,
                                      gapImage(200)));
  QVERIFY(std::abs(commandSpy.last().at(0).toDouble() -
                   settings.targetSpeedMps) < 1e-8);

  QImage blank(400, 120, QImage::Format_Grayscale8);
  blank.fill(20);
  QVERIFY(!LaserGapDetector::detect(blank).valid);
  commandSpy.clear();
  diagnosticSpy.clear();
  // Fresh blank frames isolate detector failure from camera disconnection.
  // The centered prior excludes independent scan-edge speed protection.
  QElapsedTimer dropout;
  dropout.start();
  bool sawHeldDetection = false;
  double feedbackSpeedMps = settings.targetSpeedMps;
  while (dropout.elapsed() < dropoutMs) {
    controller.processCameraImage(blank);
    controller.processDriveTelemetry(
        enabledTelemetry(0.0, feedbackSpeedMps, settings.wheelRadiusM));
    QVERIFY(latestStatus.active);
    QCOMPARE(latestStatus.phase, QStringLiteral("分段跟踪"));
    sawHeldDetection = sawHeldDetection || latestStatus.detectionHeld;
    QVERIFY(!commandSpy.isEmpty());
    feedbackSpeedMps = commandSpy.last().at(0).toDouble();
    QTest::qWait(20);
  }
  QVERIFY(sawHeldDetection);
  for (const QList<QVariant>& command : commandSpy) {
    const double linear = command.at(0).toDouble();
    QVERIFY(linear >= settings.targetSpeedMps * minimumSpeedRatio - 1e-8);
    QVERIFY(linear <= settings.targetSpeedMps + 1e-8);
  }
  QVERIFY(commandSpy.last().at(0).toDouble() <=
          settings.targetSpeedMps * finalMaximumSpeedRatio + 1e-8);

  bool sawSupervisorRecord = false;
  const QString prefix = QStringLiteral("event=laser_control_output json=");
  for (const QList<QVariant>& diagnostic : diagnosticSpy) {
    const QString message = diagnostic.first().toString();
    if (!message.startsWith(prefix)) continue;
    const QJsonObject record = QJsonDocument::fromJson(
        message.mid(prefix.size()).toUtf8()).object();
    QVERIFY(!record.isEmpty());
    QCOMPARE(record.value(QStringLiteral("requested_speed_mps")).toDouble(),
             settings.targetSpeedMps);
    QCOMPARE(record.value(QStringLiteral("speed_supervisor_boundary_scale")).toDouble(),
             1.0);
    QVERIFY(record.value(QStringLiteral("speed_supervisor_target_mps")).toDouble() >=
            settings.targetSpeedMps * minimumSpeedRatio - 1e-8);
    sawSupervisorRecord = true;
  }
  if (dropoutMs > 500) QVERIFY(sawSupervisorRecord);
  controller.setEnabled(false);
}

void DriveCoreTests::protocolEncodesAndDecodesSpeedFrames() {
  const CanFrame command = ServoProtocol::speedCommand(1, 100.0);
  QCOMPARE(command.id, 0x141U);
  QCOMPARE(command.data[0], 0xA2U);
  QCOMPARE(command.data[4], 0x10U);
  QCOMPARE(command.data[5], 0x27U);

  CanFrame response;
  response.id = 0x241;
  response.data = {0xA2, 50, 100, 0, 244, 1, 45, 0};
  const auto feedback = ServoProtocol::parseFeedback(response);
  QVERIFY(feedback.has_value());
  QCOMPARE(feedback->motorId, static_cast<std::uint8_t>(1));
  QCOMPARE(feedback->temperatureC, 50);
  QCOMPARE(feedback->outputSpeedDps, 500.0);
}

void DriveCoreTests::protocolEncodesAndDecodesStatusFrames() {
  const CanFrame query = ServoProtocol::statusQuery(7);
  QCOMPARE(query.id, 0x147U);
  QCOMPARE(query.data[0], 0x9AU);

  CanFrame response;
  response.id = 0x247;
  response.data = {0x9A, 42, 0, 0, 0, 0, 0x34, 0x12};
  const auto status = ServoProtocol::parseStatus(response);
  QVERIFY(status.has_value());
  QCOMPARE(status->motorId, static_cast<std::uint8_t>(7));
  QCOMPARE(status->temperatureC, 42);
  QCOMPARE(status->errorState, static_cast<std::uint16_t>(0x1234));
}

void DriveCoreTests::protocolAcceptsStopFeedbackFrames() {
  CanFrame response;
  response.id = 0x242;
  response.data = {0x81, 0, 0, 0, 0, 0, 0, 0};
  const auto feedback = ServoProtocol::parseFeedback(response);
  QVERIFY(feedback.has_value());
  QCOMPARE(feedback->motorId, static_cast<std::uint8_t>(2));
}

void DriveCoreTests::mwdSpeedCommandUsesLittleEndianHundredthDps() {
  const QByteArray frame = MwdRs485Protocol::speedCommand(2, -12.34);
  QCOMPARE(frame.size(), 10);
  QCOMPARE(static_cast<std::uint8_t>(frame.at(0)), static_cast<std::uint8_t>(0x3E));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(1)), static_cast<std::uint8_t>(0xA2));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(2)), static_cast<std::uint8_t>(2));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(3)), static_cast<std::uint8_t>(4));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(4)), static_cast<std::uint8_t>(0xE6));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(5)), static_cast<std::uint8_t>(0x2E));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(6)), static_cast<std::uint8_t>(0xFB));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(7)), static_cast<std::uint8_t>(0xFF));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(8)), static_cast<std::uint8_t>(0xFF));
  QCOMPARE(static_cast<std::uint8_t>(frame.at(9)), static_cast<std::uint8_t>(0x27));
}

void DriveCoreTests::mwdPositionHoldCommandsMatchProtocol() {
  const QByteArray query = MwdRs485Protocol::multiTurnAngleQuery(2);
  QCOMPARE(query.toHex(' ').toUpper(), QByteArray("3E 92 02 00 D2"));

  const QByteArray hold = MwdRs485Protocol::multiTurnPositionCommand(
      1, 36000, 360.0);
  QCOMPARE(hold.toHex(' ').toUpper(),
           QByteArray("3E A4 01 0C EF A0 8C 00 00 00 00 00 00 "
                      "A0 8C 00 00 58"));

  QByteArray responseBuffer = MwdRs485Protocol::command(
      MwdRs485Protocol::kReadMultiTurnAngle, 2,
      QByteArray::fromHex("60 73 FF FF FF FF FF FF"));
  MwdRs485Frame response;
  QVERIFY(MwdRs485Protocol::takeFrame(&responseBuffer, &response));
  const auto angle = MwdRs485Protocol::parseMultiTurnAngle(response);
  QVERIFY(angle.has_value());
  QCOMPARE(angle.value(), static_cast<std::int64_t>(-36000));

  MwdRs485Frame holdResponse;
  holdResponse.command = MwdRs485Protocol::kMultiTurnPositionClosedLoop;
  holdResponse.motorId = 1;
  holdResponse.data = QByteArray::fromHex("1E 34 12 00 00 78 56");
  const auto holdFeedback = MwdRs485Protocol::parseMotorFeedback(holdResponse);
  QVERIFY(holdFeedback.has_value());
  QCOMPARE(holdFeedback->motorId, static_cast<std::uint8_t>(1));
}

void DriveCoreTests::mwdBrakeCommandsMatchProtocol() {
  const QByteArray stopped = MwdRs485Protocol::command(
      MwdRs485Protocol::kMotorStop, 1);
  QCOMPARE(stopped.toHex(' ').toUpper(), QByteArray("3E 81 01 00 C0"));

  const QByteArray applied = MwdRs485Protocol::brakeCommand(1, true);
  QCOMPARE(applied.toHex(' ').toUpper(), QByteArray("3E 8C 01 01 CC 00 00"));

  const QByteArray released = MwdRs485Protocol::brakeCommand(2, false);
  QCOMPARE(released.toHex(' ').toUpper(), QByteArray("3E 8C 02 01 CD 01 01"));

  const QByteArray query = MwdRs485Protocol::brakeStatusQuery(2);
  QCOMPARE(query.toHex(' ').toUpper(), QByteArray("3E 8C 02 01 CD 10 10"));

  QByteArray responseBuffer = applied;
  MwdRs485Frame response;
  QVERIFY(MwdRs485Protocol::takeFrame(&responseBuffer, &response));
  const auto brakeApplied = MwdRs485Protocol::parseBrakeApplied(response);
  QVERIFY(brakeApplied.has_value());
  QVERIFY(brakeApplied.value());
}

void DriveCoreTests::mwdFramesValidateChecksumsAndLengths() {
  const QByteArray first = MwdRs485Protocol::command(
      MwdRs485Protocol::kMotorStop, 1);
  const QByteArray second = MwdRs485Protocol::command(
      MwdRs485Protocol::kMotorRun, 2);
  QByteArray buffer = first + second;
  MwdRs485Frame frame;
  QVERIFY(MwdRs485Protocol::takeFrame(&buffer, &frame));
  QCOMPARE(frame.command, MwdRs485Protocol::kMotorStop);
  QCOMPARE(frame.motorId, static_cast<std::uint8_t>(1));
  QVERIFY(MwdRs485Protocol::takeFrame(&buffer, &frame));
  QCOMPARE(frame.command, MwdRs485Protocol::kMotorRun);
  QCOMPARE(frame.motorId, static_cast<std::uint8_t>(2));
  QVERIFY(buffer.isEmpty());

  QByteArray corrupt = first;
  corrupt[4] = static_cast<char>(static_cast<std::uint8_t>(corrupt.at(4)) + 1);
  QVERIFY(!MwdRs485Protocol::takeFrame(&corrupt, &frame));
  QVERIFY(corrupt.isEmpty());

  QByteArray corruptData = MwdRs485Protocol::speedCommand(1, 1.0);
  corruptData[9] = static_cast<char>(static_cast<std::uint8_t>(corruptData.at(9)) + 1);
  QVERIFY(!MwdRs485Protocol::takeFrame(&corruptData, &frame));
  QVERIFY(corruptData.isEmpty());
}

void DriveCoreTests::mwdStatusFeedbackParsesSignedValues() {
  MwdRs485Frame frame;
  frame.command = MwdRs485Protocol::kReadStatus2;
  frame.motorId = 7;
  frame.data = QByteArray::fromHex("D6 34 12 9C FF 78 56");
  const auto feedback = MwdRs485Protocol::parseMotorFeedback(frame);
  QVERIFY(feedback.has_value());
  QCOMPARE(feedback->temperatureC, -42);
  QCOMPARE(feedback->controlValue, static_cast<std::int16_t>(0x1234));
  QCOMPARE(feedback->speedDps, -100.0);
  QCOMPARE(feedback->encoder, static_cast<std::uint16_t>(0x5678));
}

}  // namespace crawling

QTEST_APPLESS_MAIN(crawling::DriveCoreTests)

#include "drive_core_tests.moc"
