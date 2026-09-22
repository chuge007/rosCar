#include "drive_settings.h"
#include "drive_types.h"
#include "main_window.h"
#include "synchronized_drive_controller.h"
#include "device_controller.h"
#include "usb_camera_controller.h"
#include "app_logger.h"
#include "laser_correction_controller.h"
#include "laser_trajectory_renderer.h"

#include <QApplication>
#include <QImage>
#include <QThread>

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  application.setOrganizationName(CRAWLING_TEXT("CrawlingRobot"));
  application.setApplicationName(CRAWLING_TEXT("DriveConsole"));
  crawling::AppLogger::initialize();
  crawling::AppLogger::write(QStringLiteral("SYSTEM"),
                             QStringLiteral("event=application_start result=OK"));
  qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
  qRegisterMetaType<crawling::DriveState>("crawling::DriveState");
  qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
  qRegisterMetaType<crawling::ImuSample>("crawling::ImuSample");
  qRegisterMetaType<crawling::HardwareDetectionResult>("crawling::HardwareDetectionResult");
  qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
  qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");
  qRegisterMetaType<QImage>("QImage");
  qRegisterMetaType<crawling::LaserCorrectionSettings>("crawling::LaserCorrectionSettings");
  qRegisterMetaType<crawling::LaserCorrectionStatus>("crawling::LaserCorrectionStatus");
  qRegisterMetaType<crawling::LaserGapDetection>("crawling::LaserGapDetection");
  qRegisterMetaType<crawling::LaserEdgeSample>("crawling::LaserEdgeSample");
  qRegisterMetaType<crawling::LaserPathFit>("crawling::LaserPathFit");
  qRegisterMetaType<QVector<crawling::LaserEdgeSample>>(
      "QVector<crawling::LaserEdgeSample>");

  QThread controlThread;
  auto* controller = new crawling::SynchronizedDriveController();
  controller->moveToThread(&controlThread);
  QObject::connect(&controlThread, &QThread::started, controller,
                   &crawling::SynchronizedDriveController::startControlLoop);
  QObject::connect(&controlThread, &QThread::finished, controller, &QObject::deleteLater);
  controlThread.start();
  crawling::AppLogger::write(QStringLiteral("SYSTEM.THREAD"),
                             QStringLiteral("event=thread_start module=DRIVE.CONTROL result=OK"));

  QThread deviceThread;
  auto* devices = new crawling::DeviceController();
  devices->moveToThread(&deviceThread);
  QObject::connect(&deviceThread, &QThread::finished, devices, &QObject::deleteLater);
  deviceThread.start();
  crawling::AppLogger::write(QStringLiteral("SYSTEM.THREAD"),
                             QStringLiteral("event=thread_start module=DEVICE.CONTROL result=OK"));

  QThread usbCameraThread;
  auto* usbCamera = new crawling::UsbCameraController();
  usbCamera->moveToThread(&usbCameraThread);
  QObject::connect(&usbCameraThread, &QThread::finished, usbCamera,
                   &QObject::deleteLater);
  usbCameraThread.start();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_start module=USB_CAMERA result=OK"));

  // Perception and feedback must not wait for UI painting or synchronous
  // diagnostic file writes. All inputs and UI publications cross queues.
  QThread correctionThread;
  auto* correction = new crawling::LaserCorrectionController();
  correction->moveToThread(&correctionThread);
  QObject::connect(&correctionThread, &QThread::finished,
                   correction, &QObject::deleteLater);
  correctionThread.start();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_start module=CORRECTION.CONTROL result=OK"));

  QThread trajectoryWriterThread;
  auto* trajectoryWriter = new crawling::LaserTrajectoryWriter();
  trajectoryWriter->moveToThread(&trajectoryWriterThread);
  QObject::connect(&trajectoryWriterThread, &QThread::finished,
                   trajectoryWriter, &QObject::deleteLater);
  QObject::connect(
      correction,
      &crawling::LaserCorrectionController::trajectorySessionRequested,
      trajectoryWriter, &crawling::LaserTrajectoryWriter::beginSession,
      Qt::QueuedConnection);
  QObject::connect(
      correction, &crawling::LaserCorrectionController::trajectorySegmentReady,
      trajectoryWriter, &crawling::LaserTrajectoryWriter::saveSegment,
      Qt::QueuedConnection);
  QObject::connect(
      trajectoryWriter, &crawling::LaserTrajectoryWriter::sessionStarted,
      correction, &crawling::LaserCorrectionController::trajectorySessionStarted,
      Qt::QueuedConnection);
  QObject::connect(
      trajectoryWriter, &crawling::LaserTrajectoryWriter::imageSaved,
      correction, &crawling::LaserCorrectionController::trajectoryImageSaved,
      Qt::QueuedConnection);
  QObject::connect(
      trajectoryWriter, &crawling::LaserTrajectoryWriter::saveFailed,
      correction, &crawling::LaserCorrectionController::trajectorySaveFailed,
      Qt::QueuedConnection);
  QObject::connect(correction, &crawling::LaserCorrectionController::rawFrameReady,
                   trajectoryWriter, &crawling::LaserTrajectoryWriter::saveRawFrame,
                   Qt::QueuedConnection);
  QObject::connect(trajectoryWriter, &crawling::LaserTrajectoryWriter::rawFrameSaved,
                   correction, &crawling::LaserCorrectionController::rawFrameSaved,
                   Qt::QueuedConnection);
  trajectoryWriterThread.start();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_start module=CORRECTION.IMAGE_WRITER result=OK"));

  QObject::connect(controller, &crawling::SynchronizedDriveController::logMessage, &application,
                   [](const QString& message) {
                     const bool failed = message.contains(QStringLiteral("FAILED"), Qt::CaseInsensitive) ||
                                         message.contains(QStringLiteral("fault"), Qt::CaseInsensitive) ||
                                         message.contains(QStringLiteral("timeout"), Qt::CaseInsensitive) ||
                                         message.contains(CRAWLING_TEXT("故障")) ||
                                         message.contains(CRAWLING_TEXT("失败")) ||
                                         message.contains(CRAWLING_TEXT("超时")) ||
                                         message.contains(CRAWLING_TEXT("急停"));
                     if (failed) crawling::AppLogger::error(QStringLiteral("DRIVE.CONTROL"), message);
                     else crawling::AppLogger::write(QStringLiteral("DRIVE.CONTROL"), message);
                   });
  QObject::connect(correction, &crawling::LaserCorrectionController::logMessage, &application,
                   [](const QString& message) {
                     const bool warning = message.contains(CRAWLING_TEXT("失败")) ||
                                          message.contains(CRAWLING_TEXT("超时")) ||
                                          message.contains(CRAWLING_TEXT("未检测到")) ||
                                          message.contains(CRAWLING_TEXT("拟合未通过")) ||
                                          message.contains(CRAWLING_TEXT("连续不一致"));
                     if (warning) crawling::AppLogger::warning(QStringLiteral("CORRECTION.CONTROL"), message);
                      else crawling::AppLogger::write(QStringLiteral("CORRECTION.CONTROL"), message);
                   });
  QObject::connect(
      correction,
      &crawling::LaserCorrectionController::diagnosticLogMessage,
      &application, [](const QString& message) {
        crawling::AppLogger::write(QStringLiteral("CORRECTION.RAW_IMAGE"),
                                   message);
      });

  crawling::MainWindow window(controller, devices, usbCamera, correction);
  window.show();
  const int result = application.exec();

  window.shutdownControl();
  correctionThread.quit();
  correctionThread.wait();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_stop module=CORRECTION.CONTROL result=OK"));
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.SHUTDOWN"),
      QStringLiteral("event=module_shutdown_complete module=DRIVE.CONTROL result=OK"));
  QMetaObject::invokeMethod(trajectoryWriter, "shutdown", Qt::BlockingQueuedConnection);
  trajectoryWriterThread.quit();
  trajectoryWriterThread.wait();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_stop module=CORRECTION.IMAGE_WRITER result=OK interfaces=released"));
  controlThread.quit();
  controlThread.wait();
  crawling::AppLogger::write(QStringLiteral("SYSTEM.THREAD"),
                             QStringLiteral("event=thread_stop module=DRIVE.CONTROL result=OK interfaces=released"));
  QMetaObject::invokeMethod(devices, "shutdown", Qt::BlockingQueuedConnection);
  deviceThread.quit();
  deviceThread.wait();
  crawling::AppLogger::write(QStringLiteral("SYSTEM.THREAD"),
                             QStringLiteral("event=thread_stop module=DEVICE.CONTROL result=OK interfaces=released"));
  QMetaObject::invokeMethod(usbCamera, "shutdown", Qt::BlockingQueuedConnection);
  usbCameraThread.quit();
  usbCameraThread.wait();
  crawling::AppLogger::write(
      QStringLiteral("SYSTEM.THREAD"),
      QStringLiteral("event=thread_stop module=USB_CAMERA result=OK interfaces=released"));
  crawling::AppLogger::write(QStringLiteral("SYSTEM"),
                             QStringLiteral("event=application_exit result=OK code=%1").arg(result));
  return result;
}
