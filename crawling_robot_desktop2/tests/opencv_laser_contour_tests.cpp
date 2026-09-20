#include <QtTest>
#include <cmath>
#include "opencv_laser_contour.h"
#include "laser_correction_controller.h"

using namespace crawling;
namespace {
QImage weld(bool parent = false, bool downward = false, int rightEdge = 760) {
  QImage image(800, 240, QImage::Format_Grayscale8);
  image.fill(12);
  for (int x = 20; x <= 780; ++x) {
    const bool inside = x >= 420 && x <= rightEdge;
    if (!inside || parent)
      for (int y = 158; y <= 162; ++y) image.scanLine(y)[x] = 210;
    // Discontinuous plateau, as opposed to a dark defect or reflection
    // coexisting with a complete parent stripe.
    if (inside && !(x >= 480 && x <= 540)) {
      const int top = downward ? 205 : 95;
      for (int y = top - 2; y <= top + 2; ++y) image.scanLine(y)[x] = 235;
    }
  }
  return image;
}
}
class OpenCvContourTests : public QObject {
  Q_OBJECT
 private slots:
  void fragmentedPlateauWithShortShoulder() {
    const auto image = weld();
    const auto detection = OpenCvLaserContour::detect(image, 160, 0, 6, {});
    QVERIFY(detection.valid && detection.opencvContour && detection.contourFallback);
    QVERIFY(std::abs(detection.gapStartPx - 420) <= 2);
    QVERIFY(std::abs(detection.gapEndPx - 760) <= 2);
    QVERIFY(!detection.edgeBreakFallback);
    const auto routed = LaserGapDetector::detect(image);
    QVERIFY(routed.valid && routed.opencvContour);
    QVERIFY(std::abs(routed.absoluteCenterRatio - detection.absoluteCenterRatio) < .01);
  }
  void rejectsBlankReflectionAndDownwardStripe() {
    QImage blank(800, 240, QImage::Format_Grayscale8);
    blank.fill(12);
    for (const QImage& image : {blank, weld(true), weld(false, true)})
      QVERIFY(!OpenCvLaserContour::detect(image, 160, 0, 6, {}).valid);
  }
  void respectsIdentityAndNeedsBothShoulders() {
    LaserGapDetectorConfig config;
    config.expectedAbsoluteCenterRatio = .2;
    QVERIFY(!OpenCvLaserContour::detect(weld(), 160, 0, 6, config).valid);
    config.expectedAbsoluteCenterRatio = -1;
    config.expectedAbsoluteGapWidthRatio = .1;
    QVERIFY(!OpenCvLaserContour::detect(weld(), 160, 0, 6, config).valid);
    QVERIFY(!OpenCvLaserContour::detect(weld(false, false, 780), 160, 0, 6, {}).valid);
  }
  void confirmationBeforeSurvey() {
    LaserCorrectionController controller;
    LaserCorrectionStatus latest;
    connect(&controller, &LaserCorrectionController::statusChanged,
            [&latest](const LaserCorrectionStatus& value) { latest = value; });
    controller.setEnabled(true);
    DriveTelemetry telemetry;
    telemetry.state = DriveState::Enabled;
    telemetry.feedbackFresh = telemetry.left.valid = telemetry.right.valid = true;
    controller.processDriveTelemetry(telemetry);
    for (int i = 0; i < 4; ++i) {
      controller.processCameraImage(weld());
      QCOMPARE(latest.linearCommandMps, 0.0);
    }
    controller.processCameraImage(weld());
    controller.processDriveTelemetry(telemetry);
    QVERIFY(latest.active && latest.gapValid && latest.contourFallback);
    QVERIFY(!latest.detectionHeld);
    QCOMPARE(latest.phase, QString::fromUtf8("首段采集"));
    controller.setEnabled(false);
  }
};
QTEST_GUILESS_MAIN(OpenCvContourTests)
#include "opencv_laser_contour_tests.moc"
