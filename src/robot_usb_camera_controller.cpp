#include "robot_usb_camera_controller.h"

#include "robot_app_logger.h"
#include "utf8_compat.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>
#include <QSize>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>
#include <limits>

namespace crawling {

RobotUsbCameraController::RobotUsbCameraController(QObject* parent) : QObject(parent) {}

RobotUsbCameraController::~RobotUsbCameraController() = default;

void RobotUsbCameraController::scanDevices() {
  const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
  QStringList devices;
  for (int index = 0; index < cameras.size(); ++index) {
    const QString description = cameras.at(index).description().trimmed();
    devices.append(CRAWLING_TEXT("%1 | %2")
                       .arg(index)
                       .arg(description.isEmpty() ? CRAWLING_TEXT("USB 摄像头")
                                                  : description));
  }

  emit devicesChanged(devices);
  const QString message = CRAWLING_TEXT("USB 摄像头扫描到 %1 个设备").arg(devices.size());
  emit logMessage(message);
  AppLogger::write(QStringLiteral("USB_CAMERA.DISCOVERY"),
                   QStringLiteral("event=scan_complete result=OK device_count=%1")
                       .arg(devices.size()));
}

void RobotUsbCameraController::connectCamera(int deviceIndex, int fps,
                                             bool flipHorizontal,
                                             bool flipVertical) {
  disconnectCamera();
  const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
  if (deviceIndex < 0 || deviceIndex >= cameras.size()) {
    const QString message =
        CRAWLING_TEXT("USB 摄像头打开失败：设备编号 %1 不存在，请先重新扫描")
            .arg(deviceIndex);
    emit connectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"), message);
    return;
  }

  const QCameraDevice device = cameras.at(deviceIndex);
  flipHorizontal_ = flipHorizontal;
  flipVertical_ = flipVertical;

  camera_ = new QCamera(device, this);
  captureSession_ = new QMediaCaptureSession(this);
  videoSink_ = new QVideoSink(this);
  captureSession_->setCamera(camera_);
  captureSession_->setVideoSink(videoSink_);

  connect(videoSink_, &QVideoSink::videoFrameChanged,
          this, &RobotUsbCameraController::handleVideoFrame);
  connect(camera_, &QCamera::errorOccurred, this,
          [this](QCamera::Error, const QString& errorText) {
            const QString message = CRAWLING_TEXT("USB 摄像头错误：%1")
                                        .arg(errorText.isEmpty()
                                                 ? CRAWLING_TEXT("未知采集错误")
                                                 : errorText);
            emit connectionChanged(false, message);
            emit logMessage(message);
            AppLogger::error(QStringLiteral("USB_CAMERA.ACQUISITION"), message);
          });

  QCameraFormat selectedFormat;
  double bestScore = std::numeric_limits<double>::max();
  const int requestedFps = qMax(1, fps);
  for (const QCameraFormat& format : device.videoFormats()) {
    const QSize resolution = format.resolution();
    const bool containsRequestedFps =
        requestedFps >= format.minFrameRate() && requestedFps <= format.maxFrameRate();
    const double fpsPenalty = containsRequestedFps
                                  ? 0.0
                                  : std::min(std::abs(format.minFrameRate() - requestedFps),
                                             std::abs(format.maxFrameRate() - requestedFps));
    // Prefer a 720p-or-lower preview when several formats support the
    // requested rate, avoiding unnecessarily large USB frames.
    const int pixels = resolution.width() * resolution.height();
    const int targetPixels = 1280 * 720;
    const double resolutionPenalty =
        pixels <= targetPixels
            ? static_cast<double>(targetPixels - pixels) / targetPixels
            : 4.0 + static_cast<double>(pixels - targetPixels) / targetPixels;
    const double score = fpsPenalty * 100.0 + resolutionPenalty;
    if (score < bestScore) {
      bestScore = score;
      selectedFormat = format;
    }
  }
  if (!selectedFormat.isNull())
    camera_->setCameraFormat(selectedFormat);

  connect(camera_, &QCamera::activeChanged, this,
          [this, deviceIndex, device](bool active) {
    if (!active || !camera_)
      return;
    const QCameraFormat format = camera_->cameraFormat();
    const QSize resolution = format.resolution();
    const QString message =
        CRAWLING_TEXT("USB 摄像头已连接：设备 %1（%2），画面 %3 x %4，最高 %5 fps")
            .arg(deviceIndex)
            .arg(device.description())
            .arg(resolution.width())
            .arg(resolution.height())
            .arg(format.maxFrameRate(), 0, 'f', 1);
    emit connectionChanged(true, message);
    emit logMessage(message);
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=connect_complete result=OK device=%1 width=%2 height=%3 fps=%4")
                         .arg(deviceIndex)
                         .arg(resolution.width())
                         .arg(resolution.height())
                         .arg(format.maxFrameRate()));
  });

  camera_->start();
}

void RobotUsbCameraController::connectNetworkCamera(const QString& address) {
  disconnectCamera();
  QString value = address.trimmed();
  if (value.isEmpty()) {
    emit connectionChanged(false, CRAWLING_TEXT("网络摄像头地址为空"));
    return;
  }
  if (!value.contains(QStringLiteral("://")))
    value.prepend(QStringLiteral("rtsp://"));
  const QUrl url(value);
  if (!url.isValid() || url.host().isEmpty()) {
    const QString message = CRAWLING_TEXT("网络摄像头地址无效：%1").arg(value);
    emit connectionChanged(false, message);
    emit logMessage(message);
    return;
  }

  networkPlayer_ = new QMediaPlayer(this);
  networkVideoSink_ = new QVideoSink(this);
  networkPlayer_->setVideoOutput(networkVideoSink_);
  connect(networkVideoSink_, &QVideoSink::videoFrameChanged,
          this, &RobotUsbCameraController::handleVideoFrame);
  connect(networkPlayer_, &QMediaPlayer::mediaStatusChanged, this,
          [this, value](QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::LoadedMedia ||
        status == QMediaPlayer::BufferedMedia ||
        status == QMediaPlayer::BufferingMedia) {
      emit connectionChanged(true, CRAWLING_TEXT("网络摄像头已连接：%1").arg(value));
      AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                       QStringLiteral("event=network_connect status=OK address=%1").arg(value));
    }
  });
  connect(networkPlayer_, &QMediaPlayer::errorOccurred, this,
          [this, value](QMediaPlayer::Error, const QString& errorText) {
    const QString message = CRAWLING_TEXT("网络摄像头打开失败：%1（%2）")
                                .arg(value, errorText);
    emit connectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=network_connect result=FAILED address=%1 error=%2")
                         .arg(value, errorText));
  });
  networkPlayer_->setSource(url);
  networkPlayer_->play();
  emit logMessage(CRAWLING_TEXT("正在连接网络摄像头：%1").arg(value));
}

void RobotUsbCameraController::disconnectCamera() {
  const bool wasConnected = camera_ && camera_->isActive();
  if (camera_)
    camera_->stop();
  if (captureSession_) {
    captureSession_->setCamera(nullptr);
    captureSession_->setVideoSink(nullptr);
  }
  delete camera_;
  camera_ = nullptr;
  delete videoSink_;
  videoSink_ = nullptr;
  delete captureSession_;
  captureSession_ = nullptr;

  const bool wasNetworkConnected = networkPlayer_ != nullptr;
  if (networkPlayer_)
    networkPlayer_->stop();
  delete networkPlayer_;
  networkPlayer_ = nullptr;
  delete networkVideoSink_;
  networkVideoSink_ = nullptr;

  emit connectionChanged(false, CRAWLING_TEXT("USB 摄像头未连接"));
  if (wasConnected || wasNetworkConnected) {
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=disconnect_complete result=OK"));
  }
}

void RobotUsbCameraController::shutdown() {
  disconnectCamera();
}

void RobotUsbCameraController::handleVideoFrame(const QVideoFrame& frame) {
  if (!frame.isValid())
    return;
  QImage image = frame.toImage();
  if (image.isNull())
    return;
  if (flipHorizontal_ || flipVertical_)
    image = image.mirrored(flipHorizontal_, flipVertical_);
  emit frameChanged(image);
}

}  // namespace crawling
