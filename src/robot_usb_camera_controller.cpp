#include "robot_usb_camera_controller.h"

#include "robot_app_logger.h"
#include "utf8_compat.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QStringBuilder>
#include <QTimer>
#include <QUrl>
#include <QSize>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>
#include <limits>

namespace crawling {

namespace {

constexpr int kNetworkWatchdogIntervalMs = 1000;
constexpr int kNetworkFrameTimeoutMs = 6000;
constexpr int kNetworkReconnectMaxDelayMs = 10000;

QString slotLabel(int slot) {
  return CRAWLING_TEXT("USB 摄像头 %1").arg(slot + 1);
}

}  // namespace

RobotUsbCameraController::RobotUsbCameraController(QObject* parent) : QObject(parent) {
  clock_.start();
}

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

void RobotUsbCameraController::teardownSlot(UsbSlot& slot) const {
  if (slot.camera)
    slot.camera->stop();
  if (slot.captureSession) {
    slot.captureSession->setCamera(nullptr);
    slot.captureSession->setVideoSink(nullptr);
  }
  delete slot.camera;
  slot.camera = nullptr;
  delete slot.videoSink;
  slot.videoSink = nullptr;
  delete slot.captureSession;
  slot.captureSession = nullptr;

  if (slot.networkPlayer)
    slot.networkPlayer->stop();
  delete slot.networkPlayer;
  slot.networkPlayer = nullptr;
  delete slot.networkVideoSink;
  slot.networkVideoSink = nullptr;
  delete slot.networkWatchdog;
  slot.networkWatchdog = nullptr;
  delete slot.networkReconnectTimer;
  slot.networkReconnectTimer = nullptr;
  slot.networkWanted = false;
  slot.reconnectAttempts = 0;
  slot.frameProcessing.storeRelease(0);
}

void RobotUsbCameraController::disconnectCamera(int slot) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  UsbSlot& target = slots_[slot];
  const bool wasCamera = target.camera && target.camera->isActive();
  const bool wasNetwork = target.networkPlayer != nullptr;
  teardownSlot(target);
  emit connectionChanged(slot, false, CRAWLING_TEXT("%1 未连接").arg(slotLabel(slot)));
  if (wasCamera || wasNetwork) {
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=disconnect_complete slot=%1 result=OK").arg(slot));
  }
}

void RobotUsbCameraController::shutdown() {
  for (int slot = 0; slot < kUsbCameraChannelCount; ++slot) {
    teardownSlot(slots_[slot]);
    emit connectionChanged(slot, false, CRAWLING_TEXT("%1 未连接").arg(slotLabel(slot)));
  }
}

void RobotUsbCameraController::frameConsumed(int slot) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  slots_[slot].frameProcessing.storeRelease(0);
}

void RobotUsbCameraController::setDisplaySize(int slot, int width, int height) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  slots_[slot].targetWidth.storeRelease(width);
  slots_[slot].targetHeight.storeRelease(height);
}

void RobotUsbCameraController::applyFrame(UsbSlot& slot, int slotIndex,
                                         const QVideoFrame& frame) {
  if (!frame.isValid())
    return;
  if (!slot.frameProcessing.testAndSetAcquire(0, 1))
    return;
  QImage image = frame.toImage();
  if (image.isNull()) {
    slot.frameProcessing.storeRelease(0);
    return;
  }
  const QSize target(slot.targetWidth.loadAcquire(), slot.targetHeight.loadAcquire());
  if (target.width() > 0 && target.height() > 0 &&
      (image.width() > target.width() || image.height() > target.height()))
    image = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (slot.flipHorizontal || slot.flipVertical)
    image = image.mirrored(slot.flipHorizontal, slot.flipVertical);
  emit frameChanged(slotIndex, image);
}

void RobotUsbCameraController::connectCamera(int slot, int deviceIndex, int fps,
                                             bool flipHorizontal, bool flipVertical) {
  if (slot < 0 || slot >= kUsbCameraChannelCount) {
    const QString message =
        CRAWLING_TEXT("%1 槽位参数无效").arg(slotLabel(slot));
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"), message);
    return;
  }
  UsbSlot& target = slots_[slot];
  teardownSlot(target);

  const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
  if (deviceIndex < 0 || deviceIndex >= cameras.size()) {
    const QString message =
        CRAWLING_TEXT("%1 打开失败：设备编号 %2 不存在，请先重新扫描")
            .arg(slotLabel(slot))
            .arg(deviceIndex);
    emit connectionChanged(slot, false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"), message);
    return;
  }

  const QCameraDevice device = cameras.at(deviceIndex);
  target.flipHorizontal = flipHorizontal;
  target.flipVertical = flipVertical;

  target.camera = new QCamera(device, this);
  target.captureSession = new QMediaCaptureSession(this);
  target.videoSink = new QVideoSink(this);
  target.captureSession->setCamera(target.camera);
  target.captureSession->setVideoSink(target.videoSink);

  const int slotIndexForLambda = slot;
  connect(target.videoSink, &QVideoSink::videoFrameChanged, this,
          [this, slotIndexForLambda](const QVideoFrame& frame) {
            applyFrame(slots_[slotIndexForLambda], slotIndexForLambda, frame);
          });
  connect(target.camera, &QCamera::errorOccurred, this,
          [this, slot, slotIndexForLambda](QCamera::Error, const QString& errorText) {
            (void)slotIndexForLambda;
            const QString message =
                CRAWLING_TEXT("%1 错误：%2")
                    .arg(slotLabel(slot))
                    .arg(errorText.isEmpty() ? CRAWLING_TEXT("未知采集错误") : errorText);
            emit connectionChanged(slot, false, message);
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
    target.camera->setCameraFormat(selectedFormat);

  connect(target.camera, &QCamera::activeChanged, this,
          [this, slot, deviceIndex, device](bool active) {
            if (!active || !slots_[slot].camera)
              return;
            const QCameraFormat format = slots_[slot].camera->cameraFormat();
            const QSize resolution = format.resolution();
            const QString message =
                CRAWLING_TEXT("%1 已连接：设备 %2（%3），画面 %4 x %5，最高 %6 fps")
                    .arg(slotLabel(slot))
                    .arg(deviceIndex)
                    .arg(device.description())
                    .arg(resolution.width())
                    .arg(resolution.height())
                    .arg(format.maxFrameRate(), 0, 'f', 1);
            emit connectionChanged(slot, true, message);
            emit logMessage(message);
            AppLogger::write(
                QStringLiteral("USB_CAMERA.CONNECTION"),
                QStringLiteral("event=connect_complete slot=%1 result=OK device=%2 width=%3 height=%4 fps=%5")
                    .arg(slot)
                    .arg(deviceIndex)
                    .arg(resolution.width())
                    .arg(resolution.height())
                    .arg(format.maxFrameRate()));
          });

  target.camera->start();
}

void RobotUsbCameraController::connectNetworkCamera(int slot, const QString& address,
                                            bool flipHorizontal, bool flipVertical) {
  if (slot < 0 || slot >= kUsbCameraChannelCount) {
    emit logMessage(CRAWLING_TEXT("%1 槽位参数无效").arg(slotLabel(slot)));
    return;
  }
  UsbSlot& target = slots_[slot];
  teardownSlot(target);
  target.flipHorizontal = flipHorizontal;
  target.flipVertical = flipVertical;

  QString value = address.trimmed();
  if (value.isEmpty()) {
    emit connectionChanged(slot, false,
                           CRAWLING_TEXT("%1 地址为空").arg(slotLabel(slot)));
    return;
  }
  if (!value.contains(QStringLiteral("://")))
    value.prepend(QStringLiteral("rtsp://"));
  const QUrl url(value);
  if (!url.isValid() || url.host().isEmpty()) {
    const QString message =
        CRAWLING_TEXT("%1 地址无效：%2").arg(slotLabel(slot), value);
    emit connectionChanged(slot, false, message);
    emit logMessage(message);
    return;
  }

  target.networkAddress = value;
  target.networkWanted = true;
  target.reconnectAttempts = 0;

  target.networkWatchdog = new QTimer(this);
  target.networkWatchdog->setInterval(kNetworkWatchdogIntervalMs);
  connect(target.networkWatchdog, &QTimer::timeout, this,
          [this, slot] { checkNetworkWatchdog(slot); });
  target.networkReconnectTimer = new QTimer(this);
  target.networkReconnectTimer->setSingleShot(true);
  connect(target.networkReconnectTimer, &QTimer::timeout, this,
          [this, slot] { startNetworkStream(slot); });

  startNetworkStream(slot);
  target.networkWatchdog->start();
  emit logMessage(CRAWLING_TEXT("正在连接%1：%2").arg(slotLabel(slot), value));
}

void RobotUsbCameraController::startNetworkStream(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted)
    return;

  if (target.networkPlayer) {
    disconnect(target.networkPlayer, nullptr, this, nullptr);
    target.networkPlayer->stop();
    target.networkPlayer->deleteLater();
    target.networkPlayer = nullptr;
  }
  if (target.networkVideoSink) {
    disconnect(target.networkVideoSink, nullptr, this, nullptr);
    target.networkVideoSink->deleteLater();
    target.networkVideoSink = nullptr;
  }
  target.frameProcessing.storeRelease(0);
  target.lastFrameTime = clock_.elapsed();

  const QString value = target.networkAddress;
  target.networkPlayer = new QMediaPlayer(this);
  target.networkVideoSink = new QVideoSink(this);
  target.networkPlayer->setVideoOutput(target.networkVideoSink);
  connect(target.networkVideoSink, &QVideoSink::videoFrameChanged, this,
          [this, slot](const QVideoFrame& frame) {
            UsbSlot& current = slots_[slot];
            current.lastFrameTime = clock_.elapsed();
            if (current.reconnectAttempts > 0) {
              const QString message =
                  CRAWLING_TEXT("%1 已重新连接：%2").arg(slotLabel(slot), current.networkAddress);
              current.reconnectAttempts = 0;
              emit connectionChanged(slot, true, message);
              emit logMessage(message);
              AppLogger::write(
                  QStringLiteral("USB_CAMERA.CONNECTION"),
                  QStringLiteral("event=network_reconnect slot=%1 result=OK address=%2")
                      .arg(slot)
                      .arg(current.networkAddress));
            }
            applyFrame(current, slot, frame);
          });
  connect(target.networkPlayer, &QMediaPlayer::mediaStatusChanged, this,
          [this, slot, value](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
              scheduleNetworkReconnect(slot);
              return;
            }
            if (slots_[slot].reconnectAttempts > 0)
              return;
            if (status == QMediaPlayer::LoadedMedia ||
                status == QMediaPlayer::BufferedMedia ||
                status == QMediaPlayer::BufferingMedia) {
              emit connectionChanged(
                  slot, true,
                  CRAWLING_TEXT("%1 已连接：%2").arg(slotLabel(slot), value));
              AppLogger::write(
                  QStringLiteral("USB_CAMERA.CONNECTION"),
                  QStringLiteral("event=network_connect slot=%1 status=OK address=%2")
                      .arg(slot)
                      .arg(value));
            }
          });
  connect(target.networkPlayer, &QMediaPlayer::errorOccurred, this,
          [this, slot, value](QMediaPlayer::Error, const QString& errorText) {
            emit logMessage(CRAWLING_TEXT("%1 打开失败：%2（%3）")
                                .arg(slotLabel(slot), value, errorText));
            AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"),
                             QStringLiteral("event=network_connect slot=%1 result=FAILED address=%2 error=%3")
                                 .arg(slot)
                                 .arg(value)
                                 .arg(errorText));
            scheduleNetworkReconnect(slot);
          });
  target.networkPlayer->setSource(QUrl(value));
  target.networkPlayer->play();
}

void RobotUsbCameraController::scheduleNetworkReconnect(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted || !target.networkReconnectTimer ||
      target.networkReconnectTimer->isActive())
    return;

  if (target.networkPlayer) {
    disconnect(target.networkPlayer, nullptr, this, nullptr);
    target.networkPlayer->stop();
    target.networkPlayer->deleteLater();
    target.networkPlayer = nullptr;
  }
  if (target.networkVideoSink) {
    disconnect(target.networkVideoSink, nullptr, this, nullptr);
    target.networkVideoSink->deleteLater();
    target.networkVideoSink = nullptr;
  }
  target.frameProcessing.storeRelease(0);

  ++target.reconnectAttempts;
  const int shift = qMin(target.reconnectAttempts - 1, 4);
  const int delayMs = qMin(1000 << shift, kNetworkReconnectMaxDelayMs);
  const QString message =
      CRAWLING_TEXT("%1 连接中断，%2 秒后第 %3 次重连")
          .arg(slotLabel(slot))
          .arg(delayMs / 1000)
          .arg(target.reconnectAttempts);
  emit connectionChanged(slot, false, message);
  emit logMessage(message);
  AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                   QStringLiteral("event=network_reconnect_scheduled slot=%1 attempt=%2 delay_ms=%3 address=%4")
                       .arg(slot)
                       .arg(target.reconnectAttempts)
                       .arg(delayMs)
                       .arg(target.networkAddress));
  target.networkReconnectTimer->start(delayMs);
}

void RobotUsbCameraController::checkNetworkWatchdog(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted || !target.networkPlayer)
    return;
  if (target.networkReconnectTimer && target.networkReconnectTimer->isActive())
    return;
  if (clock_.elapsed() - target.lastFrameTime < kNetworkFrameTimeoutMs)
    return;
  AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                   QStringLiteral("event=network_frame_timeout slot=%1 timeout_ms=%2")
                       .arg(slot)
                       .arg(kNetworkFrameTimeoutMs));
  scheduleNetworkReconnect(slot);
}

}  // namespace crawling
