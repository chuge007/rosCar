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
constexpr std::size_t kMaxPointCloudSamples = 1000;
}  // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent), imuPort_(this) {
  connect(&imuPort_, &QSerialPort::readyRead, this, &DeviceController::readImu);
  connect(&imuPort_, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred), this,
          [this](QSerialPort::SerialPortError error) { if (error != QSerialPort::NoError && imuPort_.isOpen()) { emit imuConnectionChanged(false, imuPort_.errorString()); } });
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
  imuParser_ = Rim302FrameParser{};
  imuBaudRate_ = baudRate;
  imuPort_.setPortName(portName); imuPort_.setBaudRate(baudRate); imuPort_.setDataBits(QSerialPort::Data8);
  imuPort_.setParity(QSerialPort::NoParity); imuPort_.setStopBits(QSerialPort::OneStop); imuPort_.setFlowControl(QSerialPort::NoFlowControl);
  if (!imuPort_.open(QIODevice::ReadWrite)) { emit imuConnectionChanged(false, imuPort_.errorString()); return; }
  const auto command = Rim302FrameParser::continuousModeCommand(static_cast<std::uint8_t>(divider));
  imuPort_.write(reinterpret_cast<const char*>(command.data()), static_cast<qint64>(command.size()));
  const QString message = CRAWLING_TEXT("RIM302 \xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x9A""%1\xEF\xBC\x8C""%2 bps").arg(portName).arg(baudRate); emit imuConnectionChanged(true, message); AppLogger::write(CRAWLING_TEXT("IMU"), message);
}
void DeviceController::disconnectImu() { if (imuPort_.isOpen()) imuPort_.close(); emit imuConnectionChanged(false, CRAWLING_TEXT("RIM302 \xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80""")); AppLogger::write(CRAWLING_TEXT("IMU"), CRAWLING_TEXT("RIM302 \xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80""")); }
void DeviceController::autoDetectDevices(const QString& preferredImuPort,
                                          const QString& preferredLaserSerial) {
  emit deviceDetectionChanged(true, CRAWLING_TEXT("\xE6\xAD\xA3""\xE5\x9C\xA8""\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B"" IMU \xE4\xB8\x8E""\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""..."));
  AppLogger::write(CRAWLING_TEXT("\xE8\xAE\xBE""\xE5\xA4\x87"""), CRAWLING_TEXT("\xE5\xBC\x80""\xE5\xA7\x8B""\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B"" IMU \xE4\xB8\x8E""\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA"""));

  QString imuPortName;
  int imuBaudRate = 115200;
  if (imuPort_.isOpen()) {
    imuPortName = imuPort_.portName();
    imuBaudRate = imuBaudRate_;
  } else {
    const QVector<int> baudRates{115200, 230400, 460800, 921600};
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
  AppLogger::write(CRAWLING_TEXT("\xE8\xAE\xBE""\xE5\xA4\x87"""), message);
}
void DeviceController::readImu() { const QByteArray raw = imuPort_.readAll(); for (const auto& sample : imuParser_.consume(reinterpret_cast<const std::uint8_t*>(raw.constData()), size_t(raw.size()))) { emit imuSampleChanged(sample); const qint64 now = QDateTime::currentMSecsSinceEpoch(); if (now - lastImuLogMs_ >= 100) { lastImuLogMs_ = now; AppLogger::write(CRAWLING_TEXT("IMU"), CRAWLING_TEXT("R=%1 P=%2 Y=%3 G=[%4,%5,%6] A=[%7,%8,%9]").arg(sample.rollRad,0,'f',4).arg(sample.pitchRad,0,'f',4).arg(sample.yawRad,0,'f',4).arg(sample.gyroXRadps,0,'f',4).arg(sample.gyroYRadps,0,'f',4).arg(sample.gyroZRadps,0,'f',4).arg(sample.accelerationXMps2,0,'f',4).arg(sample.accelerationYMps2,0,'f',4).arg(sample.accelerationZMps2,0,'f',4)); } } }
void DeviceController::scanCamera() {
  try { if (!camera_) { mv3dlp::DriverOptions o; o.library_search_paths = {QDir(QCoreApplication::applicationDirPath()).filePath("mv3dlp_sdk").toStdString(), "D:/dev/deskCrawlingRobot/modules/mv3dlp_laser_profile/windows_x64/bin"}; camera_ = std::make_unique<mv3dlp::Driver>(o); AppLogger::write(QStringLiteral("Camera"), QStringLiteral("SDK driver initialized")); }
    QStringList devices; for (const auto& d : camera_->enumerateDevices()) { devices << QString::fromStdString(d.serial_number + " | " + d.current_ip + " | " + d.model_name); AppLogger::write(QStringLiteral("Camera"), QStringLiteral("device serial=%1 model=%2 ip=%3 host=%4").arg(QString::fromStdString(d.serial_number)).arg(QString::fromStdString(d.model_name)).arg(QString::fromStdString(d.current_ip)).arg(QString::fromStdString(d.host_ip))); }
    emit cameraDevicesChanged(devices); emit logMessage(CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE6\x89\xAB""\xE6\x8F\x8F""\xE5\x88\xB0"" %1 \xE4\xB8\xAA""\xE8\xAE\xBE""\xE5\xA4\x87""").arg(devices.size())); AppLogger::write(CRAWLING_TEXT("\xE7\x9B\xB8""\xE6\x9C\xBA"""), CRAWLING_TEXT("\xE6\x89\xAB""\xE6\x8F\x8F""\xE5\x88\xB0"" %1 \xE4\xB8\xAA""\xE8\xAE\xBE""\xE5\xA4\x87""").arg(devices.size()));
  } catch (const std::exception& e) { const QString message = QStringLiteral("camera scan failed: %1").arg(QString::fromLocal8Bit(e.what())); emit cameraConnectionChanged(false, message); emit logMessage(message); AppLogger::write(QStringLiteral("CameraError"), message); }
}
void DeviceController::connectCamera(const QString& serialNumber) {
  try { scanCamera(); if (!camera_) return; const QString serial = serialNumber.section(" | ", 0, 0); if (serial.isEmpty()) { emit cameraConnectionChanged(false, CRAWLING_TEXT("\xE8\xAF\xB7""\xE5\x85\x88""\xE9\x80\x89""\xE6\x8B\xA9""\xE7\x9B\xB8""\xE6\x9C\xBA""")); return; }
    if (camera_->isConnected()) {
      // Auto-detect may already have opened this camera. Reuse that handle
      // instead of issuing a second OpenDeviceBySN call.
      if (!camera_->isAcquiring()) {
        camera_->startAcquisition();
      }
      cameraTimer_->start();
      emit cameraConnectionChanged(true, QStringLiteral("Camera already connected; reusing existing handle"));
      AppLogger::write(QStringLiteral("Camera"),
                       QStringLiteral("reused camera handle acquiring=%1 timer=%2ms")
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
    AppLogger::write(QStringLiteral("Camera"),
                     QStringLiteral("connect serial=%1 model=%2 mode=original_image")
                         .arg(serial).arg(model));
    // These parameters are optional across the two camera firmware families.
    // Apply them when supported, but never fail an otherwise valid connection.
    try {
      camera_->setEnumParam(mv3dlp::param_keys::kTriggerMode, 0u);
      AppLogger::write(QStringLiteral("Camera"), QStringLiteral("TriggerMode=free-run applied"));
    } catch (const std::exception&) {
      AppLogger::write(QStringLiteral("Camera"), QStringLiteral("TriggerMode unsupported; using camera default"));
    }
    try {
      camera_->setFloatParam(mv3dlp::param_keys::kAcquisitionFrameRate, 30.0F);
      AppLogger::write(QStringLiteral("Camera"), QStringLiteral("AcquisitionFrameRate=30 applied"));
    } catch (const std::exception&) {
      AppLogger::write(QStringLiteral("Camera"), QStringLiteral("AcquisitionFrameRate unsupported; using camera default"));
    }
    // Disable frame triggering so the camera free-runs continuously. The
    // range-image callback is assembled by the camera from its profile lines;
    // setting the acquisition rate prevents an old one-shot/external-trigger
    // configuration from stretching the interval between completed images.
    camera_->startAcquisition(); cameraTimer_->start(); const QString message = CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x9A""%1").arg(serial); emit cameraConnectionChanged(true, message); AppLogger::write(CRAWLING_TEXT("\xE7\x9B\xB8""\xE6\x9C\xBA"""), message);
  } catch (const std::exception& e) { emit cameraConnectionChanged(false, QString::fromLocal8Bit(e.what())); }
}
void DeviceController::disconnectCamera() { cameraTimer_->stop(); try { if (camera_ && camera_->isAcquiring()) camera_->stopAcquisition(); if (camera_ && camera_->isConnected()) camera_->disconnect(); } catch (...) {} emit cameraConnectionChanged(false, CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80""")); AppLogger::write(CRAWLING_TEXT("\xE7\x9B\xB8""\xE6\x9C\xBA"""), CRAWLING_TEXT("\xE7\xBA\xBF""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x9B\xB8""\xE6\x9C\xBA""\xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80""")); }
void DeviceController::shutdown() { disconnectImu(); disconnectCamera(); }
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
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastCameraStatsLogMs_ >= 500) {
        lastCameraStatsLogMs_ = now;
        AppLogger::write(QStringLiteral("CameraStats"),
                         QStringLiteral("poll=%1, not acquiring, frames=%2, timeout=%3")
                             .arg(cameraPollCount_).arg(cameraFrameCount_).arg(cameraTimeoutCount_));
      }
      return;
    }

    // Keep this slot bounded and always consume the freshest SDK frame.
    const auto frame = camera_->tryFetchFrame(std::chrono::milliseconds(5));
    if (!frame) {
      ++cameraTimeoutCount_;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastCameraStatsLogMs_ >= 500) {
        lastCameraStatsLogMs_ = now;
        AppLogger::write(QStringLiteral("CameraStats"),
                         QStringLiteral("poll=%1, no frame timeout, frames=%2, timeout=%3, invalid=%4, empty=%5, lastFrame=%6")
                             .arg(cameraPollCount_).arg(cameraFrameCount_).arg(cameraTimeoutCount_)
                             .arg(cameraInvalidFrameCount_).arg(cameraEmptyDataCount_).arg(lastCameraFrameNumber_));
      }
      return;
    }
    ++cameraFrameCount_;
    lastCameraFrameNumber_ = frame->frame_number;
    if (!frame->valid) {
      ++cameraInvalidFrameCount_;
      AppLogger::write(QStringLiteral("Camera"),
                       QStringLiteral("invalid frame=%1 type=%2 size=%3x%4 data=%5")
                           .arg(frame->frame_number).arg(static_cast<quint32>(frame->type))
                           .arg(frame->width).arg(frame->height).arg(frame->data.size()));
      return;
    }
    if (frame->data.empty() && frame->intensity_data.empty()) {
      ++cameraEmptyDataCount_;
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
      } else {
        AppLogger::write(QStringLiteral("Camera"),
                         QStringLiteral("JPEG decode failed frame=%1 data=%2")
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
    emit cameraFrameChanged(frame->frame_number, frame->width, frame->height, pointCount);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cameraImage.isNull() && now - lastPointCloudEmitMs_ >= kPointCloudPublishIntervalMs) {
      lastPointCloudEmitMs_ = now;
      // Queued QVector delivery is implicitly shared, so this does not copy
      // the sampled points before the latest-frame buffer takes ownership.
      emit pointCloudProfileChanged(profile);
    }
    bool notifyPreview = false;
    if (cameraImage.isNull()) {
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

    // Synchronous file I/O must not run at the camera frame rate.
    if (now - lastCameraLogMs_ >= 500) {
      lastCameraLogMs_ = now;
      AppLogger::write(
          CRAWLING_TEXT("\xE7\x9B\xB8""\xE6\x9C\xba"""),
          CRAWLING_TEXT("frame=%1, type=%2, %3x%4, points=%5, preview=%6")
              .arg(frame->frame_number)
              .arg(static_cast<quint32>(frame->type))
              .arg(frame->width)
              .arg(frame->height)
              .arg(pointCount)
              .arg(previewPointCount));
    }
    if (now - lastCameraStatsLogMs_ >= 500) {
      lastCameraStatsLogMs_ = now;
      AppLogger::write(QStringLiteral("CameraStats"),
                       QStringLiteral("poll=%1, frames=%2, timeout=%3, invalid=%4, empty=%5, lastFrame=%6, previewTotal=%7")
                           .arg(cameraPollCount_).arg(cameraFrameCount_).arg(cameraTimeoutCount_)
                           .arg(cameraInvalidFrameCount_).arg(cameraEmptyDataCount_)
                           .arg(lastCameraFrameNumber_).arg(cameraPreviewPointCount_));
    }
  } catch (const std::exception& e) {
    disconnectCamera();
    const QString message = QString::fromLocal8Bit(e.what());
    emit logMessage(message);
    AppLogger::write(CRAWLING_TEXT("\xE7\x9B\xB8""\xE6\x9C\xba""\xE9\x94\x99""\xE8\xaf\xaf"""), message);
  }
}
}  // namespace crawling
