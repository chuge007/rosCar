#include <QtTest>
#include <cmath>
#include "opencv_laser_contour.h"
#include "laser_correction_controller.h"
#include "profile_weld_detector.h"
#include <QDateTime>
#include <limits>

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
  void sdkProfilePreviewWhileIdleNeverCommandsMotion() {
    LaserCorrectionController controller;
    QVector<QVector3D> points;
    for (int i = 0; i < 800; ++i)
      points.append(QVector3D(i, 0, float(100 + .02*i +
          ((i >= 300 && i <= 480) ? 8 : 0) + .02*std::sin(i*.7))));
    int previews = 0;
    int commands = 0;
    QObject::connect(&controller, &LaserCorrectionController::commandChanged,
                     [&commands](double, double) { ++commands; });
    QObject::connect(&controller, &LaserCorrectionController::profileObservationReady,
        [&](const QVector<QVector3D>& observed, const LaserGapDetection& detection,
            quint32 frame, qint64 receivedAt) {
          ++previews;
          QCOMPARE(observed, points);
          QVERIFY(detection.valid && detection.profileContour);
          QCOMPARE(detection.gapStartPx, 300);
          QCOMPARE(detection.gapEndPx, 480);
          QCOMPARE(frame, quint32(previews));
          QVERIFY(receivedAt > 0);
        });
    controller.processProfileFrame(points, 1, QDateTime::currentMSecsSinceEpoch());
    controller.processProfileFrame(points, 2, QDateTime::currentMSecsSinceEpoch());
    QCOMPARE(previews, 2);
    QCOMPARE(commands, 0);
    controller.processProfileFrame(points, 3, QDateTime::currentMSecsSinceEpoch()-1000);
    QCOMPARE(previews, 2);
    QCOMPARE(commands, 0);
  }

  void sdkProfileBridgesDropoutsWithoutFlatParentMerging() {
    QVector<QVector3D> points;
    for (int i=0;i<2048;++i) {
      double z=100+.02*i+.02*std::sin(i*.7);
      if (i>=750 && i<1420) z+=5;
      if ((i>=1200 && i<1221) || (i>=1310 && i<1335)) z=10;
      points.append(QVector3D(i,0,float(z)));
    }
    const auto found=ProfileWeldDetector::detect(points);
    QVERIFY(found.valid);
    QCOMPARE(found.gapStartPx,750);
    QCOMPARE(found.gapEndPx,1419);
    // A sustained parent segment separates two objects; don't bridge it.
    for (int i=1050;i<1090;++i) points[i].setZ(float(100+.02*i));
    const auto split=ProfileWeldDetector::detect(points);
    QVERIFY(!split.valid || split.gapEndPx<1090 || split.gapStartPx>1050);
  }
  void sdkProfileHeightShapes() {
    for (int width : {10, 30, 120, 420}) {
      for (double height : {1., 5., 80.}) {
        for (int shape = 0; shape < 3; ++shape) {
          QVector<QVector3D> points;
          for (int i = 0; i < 800; ++i) {
            double z = 100 + .02*i + .02*std::sin(i*.7);
            if (i >= 190 && i < 190+width) {
              z += height*(shape == 1 ? std::sin(.1+(i-190)*2.94/(width-1)) : 1.);
              if (shape == 2 && width >= 30 && i >= 190+width/2 && i < 192+width/2)
                z = std::numeric_limits<double>::quiet_NaN();
            }
            points.append(QVector3D(i,0,float(z)));
          }
          const auto r = ProfileWeldDetector::detect(points);
          QVERIFY(r.valid && r.profileContour);
          QVERIFY(std::abs(r.absoluteCenterRatio-(190+(width-1)*.5)/799) < .015);
        }
      }
    }
  }
  void sdkProfileRejectsNoiseAndWrongIdentity() {
    QVector<QVector3D> points;
    for (int i = 0; i < 800; ++i)
      points.append(QVector3D(i,0,float(100+.02*i+.02*std::sin(i*.7))));
    QVERIFY(!ProfileWeldDetector::detect(points).valid);
    points[400].setZ(180);
    QVERIFY(!ProfileWeldDetector::detect(points).valid);
    for (int i = 300; i < 400; ++i) points[i].setZ(points[i].z()+5);
    QVERIFY(ProfileWeldDetector::detect(points).valid);
    LaserGapDetectorConfig config;
    config.expectedAbsoluteCenterRatio = .8;
    const auto rejected = ProfileWeldDetector::detect(points,config);
    QVERIFY(!rejected.valid && rejected.continuityRejected);
  }
  void sdkProfileFeedsSurveyWithoutRasterImage() {
    LaserCorrectionController controller;
    LaserCorrectionStatus latest;
    connect(&controller,&LaserCorrectionController::statusChanged,
            [&latest](const LaserCorrectionStatus& s) { latest=s; });
    controller.setEnabled(true);
    DriveTelemetry telemetry;
    telemetry.state=DriveState::Enabled;
    telemetry.feedbackFresh=telemetry.left.valid=telemetry.right.valid=true;
    controller.processDriveTelemetry(telemetry);
    QVector<QVector3D> points;
    for (int i=0;i<800;++i)
      points.append(QVector3D(i,0,float(100+.02*i+((i>=350 && i<450)?5:0))));
    for (int i=1;i<=3;++i)
      controller.processProfileFrame(points,quint32(i),QDateTime::currentMSecsSinceEpoch());
    controller.processDriveTelemetry(telemetry);
    QVERIFY(latest.active && latest.gapValid);
    QCOMPARE(latest.phase,QString::fromUtf8("首段采集"));
    controller.setEnabled(false);
  }
  void raisedWeldWinsOverUnrelatedDarkSpeckle() {
    QImage image = weld();
    for (int y = 156; y <= 164; ++y)
      for (int x = 90; x <= 120; ++x) image.scanLine(y)[x] = 12;
    const auto result = LaserGapDetector::detect(image);
    QVERIFY(result.valid && result.opencvContour);
    QVERIFY(result.gapStartPx >= 418 && result.gapEndPx >= 758);
  }
  void reacquisitionCorridorRejectsDistantSpeckle() {
    QImage image(800, 240, QImage::Format_Grayscale8);
    image.fill(12);
    for (int y = 158; y <= 162; ++y)
      for (int x = 20; x <= 780; ++x)
        if (x < 90 || x > 120) image.scanLine(y)[x] = 210;
    QVERIFY(LaserGapDetector::detect(image).valid);
    LaserGapDetectorConfig config;
    config.referenceAbsoluteCenterRatio = .63;
    config.maximumReferenceCenterDriftRatio = .12;
    QVERIFY(!LaserGapDetector::detect(image, config).valid);
  }
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
