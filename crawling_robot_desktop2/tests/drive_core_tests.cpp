#include <QtTest>

#include <QColor>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

#include "differential_mixer.h"
#include "drive_settings.h"
#include "laser_gap_detector.h"
#include "laser_correction_controller.h"
#include "laser_path_estimator.h"
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
  void imageGapDetectorReportsShiftedGapPosition();
  void imageGapDetectorFollowsExpectedGap();
  void imageGapDetectorKeepsExpectedAxis();
  void imageGapDetectorInfersSingleVisibleEdge();
  void imageGapDetectorDoesNotInferContinuousLine();
  void imageGapDetectorRejectsContinuousLaserLine();
  void pathEstimatorFitsPositiveAndNegativeSlopes();
  void pathEstimatorAveragesParallelEdges();
  void pathEstimatorRejectsIsolatedOutlier();
  void pathEstimatorRejectsInsufficientData();
  void pathEstimatorFitsShortSegment();
  void trajectoryRendererDrawsLaserGapAndCenterLine();
  void correctionControllerAcceptsTwentyMillimeterSegment();
  void correctionControllerSurveysReturnsAndTracks();
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
  QCOMPARE(DriveSettings{}.motorOutputToWheelRatio, 100.0);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("drive_settings.ini"));
  QSettings persistent(path, QSettings::IniFormat);
  persistent.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 1.0);
  persistent.sync();

  DriveSettings migrated = DriveSettings::load(persistent);
  QCOMPARE(migrated.motorOutputToWheelRatio, 100.0);
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

  QSettings schema2(directory.filePath(QStringLiteral("schema2.ini")),
                    QSettings::IniFormat);
  schema2.setValue(QStringLiteral("drive/settingsSchemaVersion"), 2);
  schema2.setValue(QStringLiteral("drive/motorOutputToWheelRatio"), 1.0);
  schema2.setValue(QStringLiteral("drive/synchronizerMaxCorrection"), 0.030);
  schema2.sync();
  const DriveSettings adaptive = DriveSettings::load(schema2);
  QCOMPARE(adaptive.motorOutputToWheelRatio, 1.0);
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
  controller.setEnabled(false);
}

void DriveCoreTests::correctionControllerSurveysReturnsAndTracks() {
  LaserCorrectionController controller;
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
  controller.processCameraImage(gapImage(200));
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
