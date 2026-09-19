#include "device_controller.h"
#include "app_logger.h"
#include "hardware_discovery.h"
#include "utf8_compat.h"

#include "mv3dlp_laser_profile/driver.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QMutexLocker>
#include <QSerialPortInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace crawling {
namespace {
constexpr qint64 kPointCloudPublishIntervalMs = 33;
constexpr qint64 kCorrectionImagePublishIntervalMs = 50;
constexpr std::size_t kMaxPointCloudSamples = 1000;
}  // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent), imuPort_(this) {
  connect(&imuPort_, &QSerialPort::readyRead, this, &DeviceController::readImu);
  connect(&imuPort_, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred), this,
          [this](QSerialPort::SerialPortError error) {
            if (error == QSerialPort::NoError || !imuPort_.isOpen()) return;
            const QString message = imuPort_.errorString();
            AppLogger::error(QStringLiteral("IMU.COMM"),
                             QStringLiteral("event=serial_error port=%1 code=%2 error=%3")
                                 .arg(imuPort_.portName()).arg(static_cast<int>(error)).arg(message));
            emit imuConnectionChanged(false, message);
          });
  imuWatchdogTimer_ = new QTimer(this);
  imuWatchdogTimer_->setInterval(500);
  connect(imuWatchdogTimer_, &QTimer::timeout, this, &DeviceController::checkImuHealth);
  cameraTimer_ = new QTimer(this);
  // Keep the acquisition pump close to the camera frame period. The driver
  // returns the newest queued frame, so a slow conversion/UI pass cannot
  // make the preview trail behind the sensor.
  cameraTimer_->setTimerType(Qt::PreciseTimer);
  // Poll more frequently than the UI refresh period so a freshly-arrived
  // profile is removed from the SDK queue with minimal added latency.
  cameraTimer_->setInterval(5);
  connect(cameraTimer_, &QTimer::timeout, this, &DeviceController::captureCameraFrame);
}
void DeviceController::connectImu(const QString& portName, int baudRate, int divider) {
  disconnectImu();
  AppLogger::write(QStringLiteral("IMU.COMM"),
                   QStringLiteral("event=connect_start port=%1 baud=%2 divider=%3")
                       .arg(portName).arg(baudRate).arg(divider));
  imuParser_ = Rim302FrameParser{};
  imuSampleCount_ = 0;
  imuNoDataActive_ = false;
  lastImuSampleMs_ = QDateTime::currentMSecsSinceEpoch();
  imuBaudRate_ = baudRate;
  imuPort_.setPortName(portName); imuPort_.setBaudRate(baudRate); imuPort_.setDataBits(QSerialPort::Data8);
  imuPort_.setParity(QSerialPort::NoParity); imuPort_.setStopBits(QSerialPort::OneStop); imuPort_.setFlowControl(QSerialPort::NoFlowControl);
  if (!imuPort_.open(QIODevice::ReadWrite)) {
    const QString message = imuPort_.errorString();
    AppLogger::error(QStringLiteral("IMU.COMM"),
                     QStringLiteral("event=connect_failed port=%1 baud=%2 error=%3")
                         .arg(portName).arg(baudRate).arg(message));
    emit imuConnectionChanged(false, message);
    return;
  }
  const auto command = Rim302FrameParser::continuousModeCommand(static_cast<std::uint8_t>(divider));
  const qint64 written = imuPort_.write(reinterpret_cast<const char*>(command.data()),
                                        static_cast<qint64>(command.size()));
  const bool commandWritten = written == static_cast<qint64>(command.size());
  if (!commandWritten) {
    AppLogger::error(QStringLiteral("IMU.COMM"),
                     QStringLiteral("event=continuous_mode_write_failed port=%1 expected=%2 written=%3 error=%4")
                         .arg(portName).arg(command.size()).arg(written).arg(imuPort_.errorString()));
  }
  const QString message = CRAWLING_TEXT("RIM302 \xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x9A""%1\xEF\xBC\x8C""%2 bps").arg(portName).arg(baudRate);
  emit imuConnectionChanged(true, message);
  imuWatchdogTimer_->start();
  AppLogger::write(QStringLiteral("IMU.COMM"),
                   QStringLiteral("event=connect_complete result=%1 port=%2 baud=%3 divider=%4")
                       .arg(commandWritten ? QStringLiteral("OK") : QStringLiteral("DEGRADED"))
                       .arg(portName).arg(baudRate).arg(divider));
}
void DeviceController::disconnectImu() {
  const bool wasOpen = imuPort_.isOpen();
  const QString portName = imuPort_.portName();
  imuWatchdogTimer_->stop();
  if (wasOpen) imuPort_.close();
  emit imuConnectionChanged(false, CRAWLING_TEXT("RIM302 \xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80"""));
  if (wasOpen) {
    AppLogger::write(QStringLiteral("IMU.COMM"),
                     QStringLiteral("event=disconnect_complete port=%1 samples=%2")
                         .arg(portName).arg(imuSampleCount_));
  }
}
void DeviceController::autoDetectDevices(const QString& preferredImuPort,
                                          const QString& preferredLaserSerial) {
  emit deviceDetectionChanged(true, CRAWLING_TEXT("\xE6\xAD\xA3""\xE5\x9C\xA8""\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B"" IMU \xE4\xB8\x8E""\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""..."));
  AppLogger::write(QStringLiteral("DEVICE.DISCOVERY"),
                   QStringLiteral("event=auto_detect_start preferred_imu=%1 preferred_camera=%2")
                       .arg(preferredImuPort, preferredLaserSerial));

  QString imuPortName;
  int imuBaudRate = 115200;
  if (imuPort_.isOpen()) {
    imuPortName = imuPort_.portName();
    imuBaudRate = imuBaudRate_;
  } else {
    const QVector<int> baudRates{115200, 256000, 9600, 19200, 38400, 57600};
    QVector<QString> candidates;
    if (!preferredImuPort.trimmed().isEmpty()) {
      candidates.append(preferredImuPort.trimmed());
    }
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
      if (!candidates.contains(info.portName())) {
        candidates.append(info.portName());
      }
    }
    for (const QString& candidate : candidates) {
      for (const int baudRate : baudRates) {
        if (!HardwareDiscovery::probeImuPort(candidate, baudRate, 550)) {
          continue;
        }
        imuPortName = candidate;
        imuBaudRate = baudRate;
        break;
      }
      if (!imuPortName.isEmpty()) {
        break;
      }
    }
    if (!imuPortName.isEmpty()) {
      connectImu(imuPortName, imuBaudRate, 10);
    }
  }

  QString laserSerial;
  QString laserMessage;
  try {
    scanCamera();
    if (camera_ && !camera_->isConnected()) {
      const QString preferred = preferredLaserSerial.trimmed();
      if (!preferred.isEmpty()) {
        for (const auto& device : camera_->enumerateDevices()) {
          if (QString::fromStdString(device.serial_number) == preferred) {
            laserSerial = preferred;
            break;
          }
        }
      }
      if (laserSerial.isEmpty()) {
        const auto devices = camera_->enumerateDevices();
        if (!devices.empty()) {
          laserSerial = QString::fromStdString(devices.front().serial_number);
        }
      }
      if (!laserSerial.isEmpty()) {
        connectCamera(laserSerial);
        laserMessage = CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE8\x87\xAA""\xE5\x8A\xA8""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x9A""%1").arg(laserSerial);
      }
    } else if (camera_ && camera_->isConnected()) {
      laserMessage = CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE4\xBF\x9D""\xE6\x8C\x81""\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""");
    }
  } catch (const std::exception& e) {
    laserMessage = QString::fromLocal8Bit(e.what());
    emit cameraConnectionChanged(false, laserMessage);
  }

  emit sensorSettingsDetected(imuPortName, imuBaudRate, laserSerial);
  QStringList found;
  found << (imuPortName.isEmpty() ? CRAWLING_TEXT("IMU \xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0""")
                                  : CRAWLING_TEXT("IMU=%1 @ %2").arg(imuPortName).arg(imuBaudRate));
  found << (laserSerial.isEmpty() ? CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0""")
                                  : CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""=%1").arg(laserSerial));
  if (!laserMessage.isEmpty()) {
    found << laserMessage;
  }
  const QString message = found.join(CRAWLING_TEXT("\xEF\xBC\x9B"""));
  emit deviceDetectionChanged(false, message);
  emit logMessage(message);
  AppLogger::write(QStringLiteral("DEVICE.DISCOVERY"),
                   QStringLiteral("event=auto_detect_complete imu_port=%1 imu_baud=%2 camera_serial=%3 result=%4")
                       .arg(imuPortName).arg(imuBaudRate).arg(laserSerial).arg(message));
}
void DeviceController::readImu() {
  const QByteArray raw = imuPort_.readAll();
  for (const auto& sample : imuParser_.consume(
           reinterpret_cast<const std::uint8_t*>(raw.constData()), size_t(raw.size()))) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (imuNoDataActive_) {
      imuNoDataActive_ = false;
      AppLogger::write(QStringLiteral("IMU.DATA"),
                       QStringLiteral("event=sample_stream_recovered port=%1 no_valid_sample_ms=%2")
                           .arg(imuPort_.portName()).arg(now - lastImuSampleMs_));
    }
    lastImuSampleMs_ = now;
    ++imuSampleCount_;
    if (imuSampleCount_ == 1) {
      AppLogger::write(QStringLiteral("IMU.DATA"),
                       QStringLiteral("event=first_valid_sample port=%1 baud=%2")
                           .arg(imuPort_.portName()).arg(imuBaudRate_));
    }
    emit imuSampleChanged(sample);
  }
}
void DeviceController::checkImuHealth() {
  if (!imuPort_.isOpen()) return;
  const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - lastImuSampleMs_;
  if (elapsed >= 2000 && !imuNoDataActive_) {
    imuNoDataActive_ = true;
    AppLogger::warning(QStringLiteral("IMU.DATA"),
                       QStringLiteral("event=sample_stream_stalled port=%1 baud=%2 no_valid_sample_ms=%3 possible_causes=baud_or_protocol_or_wiring")
                           .arg(imuPort_.portName()).arg(imuBaudRate_).arg(elapsed));
  }
}
void DeviceController::scanCamera() {
  AppLogger::write(QStringLiteral("CAMERA.DISCOVERY"), QStringLiteral("event=scan_start"));
  try {
    if (!camera_) {
      mv3dlp::DriverOptions options;
      options.library_search_paths = {
          QDir(QCoreApplication::applicationDirPath()).filePath("mv3dlp_sdk").toStdString(),
          "D:/dev/deskCrawlingRobot/modules/mv3dlp_laser_profile/windows_x64/bin"};
      camera_ = std::make_unique<mv3dlp::Driver>(options);
      AppLogger::write(QStringLiteral("CAMERA.SDK"),
                       QStringLiteral("event=driver_initialize result=OK"));
    }
    QStringList devices;
    for (const auto& device : camera_->enumerateDevices()) {
      devices << QString::fromStdString(device.serial_number + " | " + device.current_ip +
                                        " | " + device.model_name);
      AppLogger::write(QStringLiteral("CAMERA.DISCOVERY"),
                       QStringLiteral("event=device_found serial=%1 model=%2 ip=%3 host=%4")
                           .arg(QString::fromStdString(device.serial_number))
                           .arg(QString::fromStdString(device.model_name))
                           .arg(QString::fromStdString(device.current_ip))
                           .arg(QString::fromStdString(device.host_ip)));
    }
    const QString message =
        CRAWLING_TEXT("\xE7\xBA\xBF\xE6\xBF\x80\xE5\x85\x89"
                      "\xE6\x89\xAB\xE6\x8F\x8F\xE5\x88\xB0 %1 "
                      "\xE4\xB8\xAA\xE8\xAE\xBE\xE5\xA4\x87")
            .arg(devices.size());
    emit cameraDevicesChanged(devices);
    emit logMessage(message);
    AppLogger::write(QStringLiteral("CAMERA.DISCOVERY"),
                     QStringLiteral("event=scan_complete result=OK device_count=%1")
                         .arg(devices.size()));
  } catch (const std::exception& e) {
    const QString message = QStringLiteral("camera scan failed: %1")
                                .arg(QString::fromLocal8Bit(e.what()));
    emit cameraConnectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("CAMERA.DISCOVERY"),
                     QStringLiteral("event=scan_complete result=FAILED error=%1").arg(message));
  }
}
void DeviceController::connectCamera(const QString& serialNumber) {
  const QString serial = serialNumber.section(" | ", 0, 0);
  AppLogger::write(QStringLiteral("CAMERA.CONNECTION"),
                   QStringLiteral("event=connect_start serial=%1").arg(serial));
  try {
    scanCamera();
    if (!camera_) return;
    if (serial.isEmpty()) {
      const QString message =
          CRAWLING_TEXT("\xE8\xAF\xB7\xE5\x85\x88\xE9\x80\x89\xE6\x8B\xA9"
                        "\xE7\x9B\xB8\xE6\x9C\xBA");
      emit cameraConnectionChanged(false, message);
      AppLogger::warning(QStringLiteral("CAMERA.CONNECTION"),
                         QStringLiteral("event=connect_rejected result=FAILED reason=no_serial_selected"));
      return;
    }
    if (camera_->isConnected()) {
      // Auto-detect may already have opened this camera. Reuse that handle
      // instead of issuing a second OpenDeviceBySN call.
      if (!camera_->isAcquiring()) {
        camera_->startAcquisition();
        resetCameraDiagnostics();
      }
      cameraTimer_->start();
      emit cameraConnectionChanged(true, QStringLiteral("Camera already connected; reusing existing handle"));
      AppLogger::write(QStringLiteral("CAMERA.CONNECTION"),
                       QStringLiteral("event=connect_complete result=REUSED serial=%1 acquiring=%2 timer_ms=%3")
                           .arg(serial)
                           .arg(camera_->isAcquiring()).arg(cameraTimer_->interval()));
      return;
    }
    camera_->connectBySerial(serial.toStdString());
    // This HFR camera reports ImageMode=7 as its point-cloud/profile mode
    // (see the SDK's IsHFRProfileMode path).  Keep that mode instead of
    // forcing ImageMode=4, which is the non-HFR point-cloud mode on other
    // cameras.  The driver consumes the per-profile callback, so we do not
    // wait for the 2000-row range image assembled by the image callback.
    QString model;
    for (const auto& device : camera_->enumerateDevices()) {
      if (QString::fromStdString(device.serial_number) == serial) {
        model = QString::fromStdString(device.model_name);
        break;
      }
    }
    // Request the camera's original image stream. The application can convert
    // depth frames for its lightweight preview, but the device is no longer
    // asked to generate a point-cloud/range-image output first.
    camera_->setAcquisitionMode(mv3dlp::AcquisitionMode::original_image);
    AppLogger::write(QStringLiteral("CAMERA.CONFIG"),
                     QStringLiteral("event=set_acquisition_mode result=OK serial=%1 model=%2 mode=original_image")
                         .arg(serial).arg(model));
    // These parameters are optional across the two camera firmware families.
    // Apply them when supported, but never fail an otherwise valid connection.
    try {
      camera_->setEnumParam(mv3dlp::param_keys::kTriggerMode, 0u);
      AppLogger::write(QStringLiteral("CAMERA.CONFIG"),
                       QStringLiteral("event=set_trigger_mode result=OK mode=free_run"));
    } catch (const std::exception&) {
      AppLogger::warning(QStringLiteral("CAMERA.CONFIG"),
                         QStringLiteral("event=set_trigger_mode result=UNSUPPORTED fallback=camera_default"));
    }
    try {
      camera_->setFloatParam(mv3dlp::param_keys::kAcquisitionFrameRate, 30.0F);
      AppLogger::write(QStringLiteral("CAMERA.CONFIG"),
                       QStringLiteral("event=set_frame_rate result=OK fps=30"));
    } catch (const std::exception&) {
      AppLogger::warning(QStringLiteral("CAMERA.CONFIG"),
                         QStringLiteral("event=set_frame_rate result=UNSUPPORTED fallback=camera_default"));
    }
    // Disable frame triggering so the camera free-runs continuously. The
    // range-image callback is assembled by the camera from its profile lines;
    // setting the acquisition rate prevents an old one-shot/external-trigger
    // configuration from stretching the interval between completed images.
    camera_->startAcquisition();
    resetCameraDiagnostics();
    cameraTimer_->start();
    const QString message = CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x9A""%1").arg(serial);
    emit cameraConnectionChanged(true, message);
    AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                     QStringLiteral("event=acquisition_start result=OK serial=%1 timer_ms=%2")
                         .arg(serial).arg(cameraTimer_->interval()));
  } catch (const std::exception& e) {
    const QString message = QString::fromLocal8Bit(e.what());
    emit cameraConnectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("CAMERA.CONNECTION"),
                     QStringLiteral("event=connect_complete result=FAILED serial=%1 error=%2")
                         .arg(serial, message));
  }
}
void DeviceController::disconnectCamera() {
  const bool wasConnected = camera_ && camera_->isConnected();
  const bool wasAcquiring = camera_ && camera_->isAcquiring();
  bool disconnectSucceeded = true;
  cameraTimer_->stop();
  try {
    if (wasAcquiring) camera_->stopAcquisition();
    if (wasConnected) camera_->disconnect();
  } catch (const std::exception& e) {
    disconnectSucceeded = false;
    AppLogger::error(QStringLiteral("CAMERA.CONNECTION"),
                     QStringLiteral("event=disconnect result=FAILED error=%1")
                         .arg(QString::fromLocal8Bit(e.what())));
  } catch (...) {
    disconnectSucceeded = false;
    AppLogger::error(QStringLiteral("CAMERA.CONNECTION"),
                     QStringLiteral("event=disconnect result=FAILED error=unknown_exception"));
  }
  emit cameraConnectionChanged(false, CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80"""));
  if (wasConnected || wasAcquiring) {
    AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                     QStringLiteral("event=acquisition_stop result=%1 duration_ms=%2 polls=%3 frames=%4 timeouts=%5 invalid=%6 empty=%7 last_frame=%8 preview_points=%9")
                         .arg(disconnectSucceeded ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                         .arg(QDateTime::currentMSecsSinceEpoch() - cameraAcquisitionStartedMs_)
                         .arg(cameraPollCount_).arg(cameraFrameCount_).arg(cameraTimeoutCount_)
                         .arg(cameraInvalidFrameCount_).arg(cameraEmptyDataCount_)
                         .arg(lastCameraFrameNumber_).arg(cameraPreviewPointCount_));
  }
}
void DeviceController::shutdown() { disconnectImu(); disconnectCamera(); }
void DeviceController::resetCameraDiagnostics() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  cameraAcquisitionStartedMs_ = now;
  lastCameraFrameMs_ = now;
  cameraPollCount_ = 0;
  cameraTimeoutCount_ = 0;
  cameraInvalidFrameCount_ = 0;
  cameraEmptyDataCount_ = 0;
  cameraFrameCount_ = 0;
  cameraPreviewPointCount_ = 0;
  lastCameraFrameNumber_ = 0;
  cameraFirstFrameLogged_ = false;
  cameraPipelineReadyLogged_ = false;
  cameraStalled_ = false;
  cameraInvalidFrameActive_ = false;
  cameraEmptyFrameActive_ = false;
  cameraDecodeFailureActive_ = false;
}
QVector<QVector3D> DeviceController::takeLatestPointCloudProfile() {
  QMutexLocker locker(&pointCloudMutex_);
  QVector<QVector3D> latest = std::move(latestPointCloudProfile_);
  latestPointCloudProfile_.clear();
  pointCloudSignalPending_ = false;
  return latest;
}

QImage DeviceController::takeLatestCameraImage() {
  QMutexLocker locker(&pointCloudMutex_);
  QImage image = std::move(latestCameraImage_);
  latestCameraImage_ = QImage();
  cameraImageSignalPending_ = false;
  return image;
}
void DeviceController::captureCameraFrame() {
  try {
    ++cameraPollCount_;
    if (!camera_ || !camera_->isAcquiring()) {
      return;
    }

    // Keep this slot bounded and always consume the freshest SDK frame.
    const auto frame = camera_->tryFetchFrame(std::chrono::milliseconds(5));
    if (!frame) {
      ++cameraTimeoutCount_;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (!cameraStalled_ && now - lastCameraFrameMs_ >= 2000) {
        cameraStalled_ = true;
        AppLogger::warning(QStringLiteral("CAMERA.ACQUISITION"),
                           QStringLiteral("event=frame_stream_stalled no_frame_ms=%1 polls=%2 frames=%3 timeouts=%4 last_frame=%5")
                               .arg(now - lastCameraFrameMs_).arg(cameraPollCount_)
                               .arg(cameraFrameCount_).arg(cameraTimeoutCount_)
                               .arg(lastCameraFrameNumber_));
      }
      return;
    }
    const qint64 frameReceivedMs = QDateTime::currentMSecsSinceEpoch();
    if (cameraStalled_) {
      AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                       QStringLiteral("event=frame_stream_recovered frame=%1 stalled_ms=%2")
                           .arg(frame->frame_number).arg(frameReceivedMs - lastCameraFrameMs_));
      cameraStalled_ = false;
    }
    lastCameraFrameMs_ = frameReceivedMs;
    ++cameraFrameCount_;
    lastCameraFrameNumber_ = frame->frame_number;
    if (!frame->valid) {
      ++cameraInvalidFrameCount_;
      if (!cameraInvalidFrameActive_) {
        cameraInvalidFrameActive_ = true;
        AppLogger::warning(QStringLiteral("CAMERA.ACQUISITION"),
                           QStringLiteral("event=invalid_frame frame=%1 type=%2 size=%3x%4 data_bytes=%5")
                               .arg(frame->frame_number).arg(static_cast<quint32>(frame->type))
                               .arg(frame->width).arg(frame->height).arg(frame->data.size()));
      }
      return;
    }
    if (cameraInvalidFrameActive_) {
      cameraInvalidFrameActive_ = false;
      AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                       QStringLiteral("event=valid_frame_recovered frame=%1 invalid_total=%2")
                           .arg(frame->frame_number).arg(cameraInvalidFrameCount_));
    }
    if (!cameraFirstFrameLogged_) {
      cameraFirstFrameLogged_ = true;
      AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                       QStringLiteral("event=first_valid_frame frame=%1 type=%2 size=%3x%4 data_bytes=%5")
                           .arg(frame->frame_number).arg(static_cast<quint32>(frame->type))
                           .arg(frame->width).arg(frame->height).arg(frame->data.size()));
    }
    const bool emptyFrame = frame->data.empty() && frame->intensity_data.empty();
    if (emptyFrame) {
      ++cameraEmptyDataCount_;
      if (!cameraEmptyFrameActive_) {
        cameraEmptyFrameActive_ = true;
        AppLogger::warning(QStringLiteral("CAMERA.ACQUISITION"),
                           QStringLiteral("event=empty_frame frame=%1 type=%2")
                               .arg(frame->frame_number).arg(static_cast<quint32>(frame->type)));
      }
    } else if (cameraEmptyFrameActive_) {
      cameraEmptyFrameActive_ = false;
      AppLogger::write(QStringLiteral("CAMERA.ACQUISITION"),
                       QStringLiteral("event=frame_data_recovered frame=%1 empty_total=%2")
                           .arg(frame->frame_number).arg(cameraEmptyDataCount_));
    }

    quint64 pointCount = 0;
    QVector<QVector3D> profile;
    QImage cameraImage;
    if (frame->type == mv3dlp::FrameType::depth ||
        frame->type == mv3dlp::FrameType::point_cloud ||
        frame->type == mv3dlp::FrameType::profile_abc32) {
      const auto cloud = camera_->convertDepthToPointCloud(*frame);
      pointCount = cloud.points.size();
      // Keep acquisition at the camera rate, but limit the amount of data
      // copied into the GUI event queue. The control heartbeat must not be
      // delayed by repeated multi-thousand-point queued signal arguments.
      // Sample a 2D grid rather than a linear stride: for a 2048-wide image,
      // a stride of 4096 would always select the same column and collapse the
      // Y-Z projection to one visible point.
      const std::size_t width = cloud.width;
      const std::size_t height = cloud.height;
      if (width > 0 && height > 0 && width * height <= cloud.points.size()) {
        const std::size_t gridSide = static_cast<std::size_t>(
            std::sqrt(static_cast<double>(kMaxPointCloudSamples)));
        const std::size_t rowStep = std::max<std::size_t>(1, (height + gridSide - 1) / gridSide);
        const std::size_t columnStep = std::max<std::size_t>(1, (width + gridSide - 1) / gridSide);
        profile.reserve(static_cast<int>(std::min<std::size_t>(kMaxPointCloudSamples,
                                                               ((height + rowStep - 1) / rowStep) *
                                                                   ((width + columnStep - 1) / columnStep))));
        for (std::size_t row = 0; row < height; row += rowStep) {
          for (std::size_t column = 0; column < width; column += columnStep) {
            const auto& point = cloud.points[row * width + column];
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
              profile.append(QVector3D(point.x, point.y, point.z));
            }
          }
        }
      } else {
        const std::size_t stride = std::max<std::size_t>(1, cloud.points.size() / kMaxPointCloudSamples);
        profile.reserve(static_cast<int>(cloud.points.size() / stride));
        for (std::size_t i = 0; i < cloud.points.size(); i += stride) {
          const auto& point = cloud.points[i];
          if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
            profile.append(QVector3D(point.x, point.y, point.z));
          }
        }
      }
    } else if ((frame->type == mv3dlp::FrameType::mono8 ||
                frame->type == mv3dlp::FrameType::unknown) &&
               frame->width > 0 && frame->height > 0 && !frame->data.empty()) {
      // original_image may be delivered as a raw 8-bit image instead of a
      // typed depth frame. Convert a small 2D sample to display coordinates so
      // the preview does not remain in the empty/waiting state.
      const std::size_t width = frame->width;
      const std::size_t height = frame->height;
      const std::size_t pixelCount = width * height;
      const std::size_t bytesPerPixel = frame->data.size() >= pixelCount * 2u ? 2u : 1u;
      if (pixelCount > 0 && frame->data.size() >= pixelCount * bytesPerPixel) {
        const std::size_t gridSide = static_cast<std::size_t>(
            std::sqrt(static_cast<double>(kMaxPointCloudSamples)));
        const std::size_t rowStep = std::max<std::size_t>(1, (height + gridSide - 1) / gridSide);
        const std::size_t columnStep = std::max<std::size_t>(1, (width + gridSide - 1) / gridSide);
        profile.reserve(static_cast<int>(kMaxPointCloudSamples));
        for (std::size_t row = 0; row < height; row += rowStep) {
          for (std::size_t column = 0; column < width; column += columnStep) {
            const std::size_t index = row * width + column;
            double value = 0.0;
            if (bytesPerPixel == 2u) {
              std::uint16_t raw = 0;
              std::memcpy(&raw, frame->data.data() + index * 2u, sizeof(raw));
              value = static_cast<double>(raw) * frame->z_scale + frame->z_offset;
            } else {
              value = static_cast<double>(frame->data[index]);
            }
            if (std::isfinite(value)) {
              profile.append(QVector3D(static_cast<float>(column),
                                       static_cast<float>(row),
                                       static_cast<float>(value)));
            }
          }
        }
        pointCount = pixelCount;
        if (bytesPerPixel == 1u) {
          cameraImage = QImage(frame->data.data(), static_cast<int>(width), static_cast<int>(height),
                               static_cast<int>(width), QImage::Format_Grayscale8).copy();
        }
      }
    } else if (frame->type == mv3dlp::FrameType::jpeg && !frame->data.empty()) {
      // original_image is delivered by this camera as JPEG (type=5).
      const int encodedSize = static_cast<int>(std::min<std::size_t>(
          frame->data.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
      cameraImage = QImage::fromData(frame->data.data(), encodedSize, "JPEG");
      if (!cameraImage.isNull()) {
        pointCount = static_cast<quint64>(cameraImage.width()) * cameraImage.height();
      } else if (!cameraDecodeFailureActive_) {
        cameraDecodeFailureActive_ = true;
        AppLogger::error(QStringLiteral("CAMERA.DECODE"),
                         QStringLiteral("event=jpeg_decode_failed frame=%1 data_bytes=%2")
                             .arg(frame->frame_number).arg(frame->data.size()));
      }
    } else if (frame->type == mv3dlp::FrameType::rgb24 && !frame->data.empty() &&
               frame->width > 0 && frame->height > 0) {
      const std::size_t pixelCount = static_cast<std::size_t>(frame->width) * frame->height;
      if (frame->data.size() >= pixelCount * 3u) {
        cameraImage = QImage(frame->data.data(), static_cast<int>(frame->width),
                             static_cast<int>(frame->height), static_cast<int>(frame->width * 3u),
                             QImage::Format_RGB888).copy();
        pointCount = pixelCount;
      }
    }

    const int previewPointCount = profile.size();
    if (!cameraImage.isNull() && cameraDecodeFailureActive_) {
      cameraDecodeFailureActive_ = false;
      AppLogger::write(QStringLiteral("CAMERA.DECODE"),
                       QStringLiteral("event=jpeg_decode_recovered frame=%1")
                           .arg(frame->frame_number));
    }
    const bool imagePreviewAvailable = !cameraImage.isNull();
    if (imagePreviewAvailable) pointCount = 0;
    emit cameraFrameChanged(frame->frame_number, frame->width, frame->height, pointCount);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cameraImage.isNull() && now - lastPointCloudEmitMs_ >= kPointCloudPublishIntervalMs) {
      lastPointCloudEmitMs_ = now;
      // Queued QVector delivery is implicitly shared, so this does not copy
      // the sampled points before the latest-frame buffer takes ownership.
      emit pointCloudProfileChanged(profile);
    }
    if (imagePreviewAvailable &&
        now - lastCorrectionImageEmitMs_ >= kCorrectionImagePublishIntervalMs) {
      lastCorrectionImageEmitMs_ = now;
      // QImage is implicitly shared. A queued receiver keeps the decoded frame
      // alive while the GUI preview buffer takes ownership below.
      emit cameraImageFrameChanged(cameraImage);
      emit correctionCameraFrameReady(cameraImage, frame->frame_number,
                                      frameReceivedMs);
    }
    bool notifyPreview = false;
    if (!imagePreviewAvailable) {
      QMutexLocker locker(&pointCloudMutex_);
      latestPointCloudProfile_ = std::move(profile);
      cameraPreviewPointCount_ += static_cast<quint64>(latestPointCloudProfile_.size());
      if (!pointCloudSignalPending_) {
        pointCloudSignalPending_ = true;
        notifyPreview = true;
      }
    } else {
      QMutexLocker locker(&pointCloudMutex_);
      latestCameraImage_ = std::move(cameraImage);
    }
    if (notifyPreview) {
      emit pointCloudProfileReady();
    }
    bool notifyImage = false;
    {
      QMutexLocker locker(&pointCloudMutex_);
      if (!latestCameraImage_.isNull() && !cameraImageSignalPending_) {
        cameraImageSignalPending_ = true;
        notifyImage = true;
      }
    }
    if (notifyImage) emit cameraImageReady();

    if (!cameraPipelineReadyLogged_ && (imagePreviewAvailable || previewPointCount > 0)) {
      cameraPipelineReadyLogged_ = true;
      AppLogger::write(QStringLiteral("CAMERA.PIPELINE"),
                       QStringLiteral("event=preview_ready source=DeviceController target=%1 frame=%2 type=%3 preview_points=%4")
                           .arg(imagePreviewAvailable ? QStringLiteral("CameraView+LaserCorrection")
                                                      : QStringLiteral("PointCloudView"))
                           .arg(frame->frame_number).arg(static_cast<quint32>(frame->type))
                           .arg(previewPointCount));
    }
  } catch (const std::exception& e) {
    const QString message = QString::fromLocal8Bit(e.what());
    AppLogger::error(QStringLiteral("CAMERA.ACQUISITION"),
                     QStringLiteral("event=capture_exception result=FAILED frame=%1 error=%2")
                         .arg(lastCameraFrameNumber_).arg(message));
    disconnectCamera();
    emit logMessage(message);
  }
}
}  // namespace crawling
