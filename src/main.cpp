#include "drive_settings.h"
#include "drive_types.h"
#include "main_window.h"
#include "synchronized_drive_controller.h"
#include "device_controller.h"
#include "app_logger.h"
#include "laser_correction_controller.h"

#include <QApplication>
#include <QThread>

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  application.setOrganizationName(CRAWLING_TEXT("CrawlingRobot"));
  application.setApplicationName(CRAWLING_TEXT("DriveConsole"));
  crawling::AppLogger::initialize();
  crawling::AppLogger::write(CRAWLING_TEXT("\xE7\xB3\xBB""\xE7\xBB\x9F"""), CRAWLING_TEXT("\xE7\xA8\x8B""\xE5\xBA\x8F""\xE5\x90\xAF""\xE5\x8A\xA8"""));
  qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
  qRegisterMetaType<crawling::DriveState>("crawling::DriveState");
  qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
  qRegisterMetaType<crawling::ImuSample>("crawling::ImuSample");
  qRegisterMetaType<crawling::HardwareDetectionResult>("crawling::HardwareDetectionResult");
  qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
  qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");
  qRegisterMetaType<crawling::LaserCorrectionSettings>("crawling::LaserCorrectionSettings");
  qRegisterMetaType<crawling::LaserCorrectionStatus>("crawling::LaserCorrectionStatus");

  QThread controlThread;
  auto* controller = new crawling::SynchronizedDriveController();
  controller->moveToThread(&controlThread);
  QObject::connect(&controlThread, &QThread::started, controller,
                   &crawling::SynchronizedDriveController::startControlLoop);
  QObject::connect(&controlThread, &QThread::finished, controller, &QObject::deleteLater);
  controlThread.start();

  QThread deviceThread;
  auto* devices = new crawling::DeviceController();
  devices->moveToThread(&deviceThread);
  QObject::connect(&deviceThread, &QThread::finished, devices, &QObject::deleteLater);
  deviceThread.start();

  auto* correction = new crawling::LaserCorrectionController(&application);

  QObject::connect(controller, &crawling::SynchronizedDriveController::logMessage, &application,
                   [](const QString& message) { crawling::AppLogger::write(CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98"""), message); });
  QObject::connect(devices, &crawling::DeviceController::logMessage, &application,
                   [](const QString& message) { crawling::AppLogger::write(CRAWLING_TEXT("\xE8\xAE\xBE""\xE5\xA4\x87"""), message); });

  crawling::MainWindow window(controller, devices, correction);
  window.show();
  const int result = application.exec();

  window.shutdownControl();
  controlThread.quit();
  controlThread.wait();
  QMetaObject::invokeMethod(devices, "shutdown", Qt::BlockingQueuedConnection);
  deviceThread.quit();
  deviceThread.wait();
  crawling::AppLogger::write(CRAWLING_TEXT("\xE7\xB3\xBB""\xE7\xBB\x9F"""), CRAWLING_TEXT("\xE7\xA8\x8B""\xE5\xBA\x8F""\xE9\x80\x80""\xE5\x87\xBA"""));
  return result;
}
