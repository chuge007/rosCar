#include "robot_usb_camera_controller.h"

#include "camera_frame_processor.h"
#include "camera_server_client.h"
#include "network_stream_reader.h"
#include "robot_app_logger.h"
#include "utf8_compat.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QStringBuilder>
#include <QTimer>
#include <QUrl>
#include <QSize>
#include <QVariant>
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

RobotUsbCameraController::RobotUsbCameraController(QObject* parent, CameraProcessMode mode)
    : QObject(parent), mode_(mode) {
  clock_.start();

  if (mode_ == CameraProcessMode::IsolatedProcess) {
    // Use isolated camera server process
    cameraClient_ = new CameraServerClient(this);
    connect(cameraClient_, &CameraServerClient::frameReady,
            this, &RobotUsbCameraController::frameChanged);
    connect(cameraClient_, &CameraServerClient::cameraStatusChanged,
            this, &RobotUsbCameraController::connectionChanged);

    if (!cameraClient_->start()) {
      qCritical() << "Failed to start camera server, falling back to in-process mode";
      delete cameraClient_;
      cameraClient_ = nullptr;
      mode_ = CameraProcessMode::InProcess;
    } else {
      qInfo() << "Camera controller using ISOLATED PROCESS mode";
    }
  }

  if (mode_ == CameraProcessMode::InProcess) {
    // Use in-process worker threads
    frameProcessor_ = new CameraFrameProcessor(this);
    connect(frameProcessor_, &CameraFrameProcessor::frameReady, this,
            [this](int slot, const QImage& image) {
              if (image.isNull()) {
                frameConsumed(slot);
                return;
              }
              emit frameChanged(slot, image);
            });
    qInfo() << "Camera controller using IN-PROCESS mode";
  }

  AppLogger::write(QStringLiteral("CAMERA.CONNECTION"),
                   QStringLiteral("event=controller_ready mode=%1")
                       .arg(mode_ == CameraProcessMode::IsolatedProcess
                                ? QStringLiteral("isolated_process")
                                : QStringLiteral("in_process")));
  NetworkStreamReader::logEnvironmentSnapshot(QStringLiteral("controller_constructor"));
}

RobotUsbCameraController::~RobotUsbCameraController() {
  if (frameProcessor_)
    frameProcessor_->shutdown();
  if (cameraClient_)
    cameraClient_->stop();
}

void RobotUsbCameraController::scanDevices() {
  // Isolated process mode does not support device scanning yet.
  // Always use in-process enumeration via QMediaDevices.
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

void RobotUsbCameraController::teardownSlot(UsbSlot& slot) {
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

  stopNetworkReader(slot);
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

  // Use isolated process mode if available
  if (mode_ == CameraProcessMode::IsolatedProcess && cameraClient_) {
    cameraClient_->disconnectCamera(slot);
    emit connectionChanged(slot, false, CRAWLING_TEXT("%1 未连接").arg(slotLabel(slot)));
    return;
  }

  // Fallback to in-process mode
  UsbSlot& target = slots_[slot];
  const bool wasCamera = target.camera && target.camera->isActive();
  const bool wasNetwork = target.networkReader != nullptr;
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
  if (slots_[slot].networkReader)
    slots_[slot].networkReader->releaseFrame();
}

void RobotUsbCameraController::setDisplaySize(int slot, int width, int height) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  slots_[slot].targetWidth.storeRelease(width);
  slots_[slot].targetHeight.storeRelease(height);
  if (slots_[slot].networkReader)
    slots_[slot].networkReader->setTargetSize(width, height);
}

void RobotUsbCameraController::applyFrame(UsbSlot& slot, int slotIndex,
                                         const QVideoFrame& frame) {
  if (!frame.isValid())
    return;
  if (!slot.frameProcessing.testAndSetAcquire(0, 1))
    return;

  // Delegate heavy processing to worker thread
  const int targetWidth = slot.targetWidth.loadAcquire();
  const int targetHeight = slot.targetHeight.loadAcquire();

  if (frameProcessor_) {
    frameProcessor_->submitFrame(frame, slotIndex, targetWidth, targetHeight,
                                 slot.flipHorizontal, slot.flipVertical);
  } else {
    slot.frameProcessing.storeRelease(0);
  }
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

  // Use isolated process mode if available
  if (mode_ == CameraProcessMode::IsolatedProcess && cameraClient_) {
    cameraClient_->connectUsbCamera(slot, deviceIndex, fps, flipHorizontal, flipVertical);
    return;
  }

  // Fallback to in-process mode
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
  const int requestedFps = qBound(1, fps, 15);
  for (const QCameraFormat& format : device.videoFormats()) {
    const QSize resolution = format.resolution();
    const bool containsRequestedFps =
        requestedFps >= format.minFrameRate() && requestedFps <= format.maxFrameRate();
    const double fpsPenalty = containsRequestedFps
                                 ? 0.0
                                 : std::min(std::abs(format.minFrameRate() - requestedFps),
                                            std::abs(format.maxFrameRate() - requestedFps));
    const int pixels = resolution.width() * resolution.height();
    // The preview is small; prefer a native 320x240 format so USB bandwidth,
    // conversion and scaling work all drop substantially.
    const int targetPixels = 320 * 240;
    const double resolutionPenalty =
        pixels <= targetPixels
            ? static_cast<double>(targetPixels - pixels) / targetPixels
            : 10.0 + static_cast<double>(pixels - targetPixels) / targetPixels;
    // Resolution dominates: a native 320x240 format is better for preview
    // latency than a higher-resolution stream even if its max FPS differs.
    const double score = resolutionPenalty + fpsPenalty * 0.05;
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
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=network_connect_rejected slot=%1 reason=invalid_slot")
                         .arg(slot));
    return;
  }

  QString value = address.trimmed();
  AppLogger::write(
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("event=network_connect_request slot=%1 address=\"%2\" flip_h=%3 "
                     "flip_v=%4 mode=%5")
          .arg(slot)
          .arg(value.isEmpty() ? QStringLiteral("<empty>")
                               : NetworkStreamReader::redactStreamUrl(value))
          .arg(flipHorizontal ? 1 : 0)
          .arg(flipVertical ? 1 : 0)
          .arg(mode_ == CameraProcessMode::IsolatedProcess
                   ? QStringLiteral("isolated_process")
                   : QStringLiteral("in_process")));
  NetworkStreamReader::logEnvironmentSnapshot(QStringLiteral("network_connect_request"));

  if (value.isEmpty()) {
    const QString message =
        CRAWLING_TEXT("%1 地址为空").arg(slotLabel(slot));
    emit connectionChanged(slot, false, message);
    emit logMessage(message);
    AppLogger::warning(QStringLiteral("USB_CAMERA.CONNECTION"),
                       QStringLiteral("event=network_connect_rejected slot=%1 reason=empty_address")
                           .arg(slot));
    return;
  }
  if (!value.contains(QStringLiteral("://")))
    value.prepend(QStringLiteral("rtsp://"));

  // Use isolated process mode if available
  if (mode_ == CameraProcessMode::IsolatedProcess && cameraClient_) {
    cameraClient_->connectNetworkCamera(slot, value, flipHorizontal, flipVertical);
    return;
  }

  // Fallback to in-process mode
  UsbSlot& target = slots_[slot];
  teardownSlot(target);
  target.flipHorizontal = flipHorizontal;
  target.flipVertical = flipVertical;

  const QUrl url(value);
  if (!url.isValid() || url.host().isEmpty()) {
    const QString message =
        CRAWLING_TEXT("%1 地址无效：%2").arg(slotLabel(slot), value);
    emit connectionChanged(slot, false, message);
    emit logMessage(message);
    AppLogger::warning(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=network_connect_rejected slot=%1 reason=invalid_url address=\"%2\"")
            .arg(slot)
            .arg(NetworkStreamReader::redactStreamUrl(value)));
    return;
  }

  target.networkAddress = value;
  target.networkWanted = true;
  target.reconnectAttempts = 0;
  // pushUsbDisplaySize() is queued right after this command, so use the same
  // small preview size rather than decoding at the stream's native resolution.
  target.targetWidth.storeRelease(260);
  target.targetHeight.storeRelease(160);

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

void RobotUsbCameraController::stopNetworkReader(UsbSlot& slot) {
  if (!slot.networkReader)
    return;
  NetworkStreamReader* reader = slot.networkReader;
  slot.networkReader = nullptr;
  disconnect(reader, nullptr, this, nullptr);
  reader->requestStop();
  if (!reader->wait(400)) {
    // Avoid blocking shutdown forever if FFmpeg is stuck in a socket read.
    reader->terminate();
    if (!reader->wait(700)) {
      // Deleting a running QThread would abort the process. Detach it and let
      // Qt delete the wrapper once the OS finally releases the thread.
      reader->setParent(nullptr);
      connect(reader, &QThread::finished, reader, &QObject::deleteLater);
      return;
    }
  }
  delete reader;
}

void RobotUsbCameraController::startNetworkStream(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted)
    return;

  stopNetworkReader(target);
  target.frameProcessing.storeRelease(0);
  target.lastFrameTime = clock_.elapsed();

  const QString value = target.networkAddress;
  const quint64 generation = ++target.networkGeneration;
  AppLogger::write(
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("event=stream_start slot=%1 generation=%2 address=\"%3\" "
                     "reconnect_attempt=%4")
          .arg(slot)
          .arg(generation)
          .arg(NetworkStreamReader::redactStreamUrl(value))
          .arg(target.reconnectAttempts));

  // QMediaPlayer follows a playback clock and buffers live RTSP streams, which
  // caused multi-second latency. Decode with OpenCV/FFmpeg on a dedicated thread
  // and emit only frames the UI is ready to consume.
  NetworkStreamReader* reader = new NetworkStreamReader(value, slot, this);
  target.networkReader = reader;
  target.networkReader->setTargetSize(target.targetWidth.loadAcquire(),
                                      target.targetHeight.loadAcquire());
  target.networkReader->setFlip(target.flipHorizontal, target.flipVertical);

  connect(reader, &NetworkStreamReader::opened, this,
          [this, slot, generation, reader](int, int width, int height, double fps) {
            UsbSlot& current = slots_[slot];
            if (current.networkReader != reader || current.networkGeneration != generation)
              return;
            current.lastFrameTime = clock_.elapsed();
            if (current.reconnectAttempts > 0) {
              const QString message = CRAWLING_TEXT("%1 已重新连接：%2")
                                          .arg(slotLabel(slot), current.networkAddress);
              current.reconnectAttempts = 0;
              emit connectionChanged(slot, true, message);
              emit logMessage(message);
              AppLogger::write(
                  QStringLiteral("USB_CAMERA.CONNECTION"),
                  QStringLiteral("event=network_reconnect slot=%1 result=OK address=%2")
                      .arg(slot)
                      .arg(NetworkStreamReader::redactStreamUrl(
                          current.networkAddress)));
            } else {
              const QString message =
                  CRAWLING_TEXT("%1 已连接：%2（%3x%4，%5 fps，低延迟）")
                      .arg(slotLabel(slot), current.networkAddress)
                      .arg(width)
                      .arg(height)
                      .arg(fps, 0, 'f', 1);
              emit connectionChanged(slot, true, message);
              emit logMessage(message);
              AppLogger::write(
                  QStringLiteral("USB_CAMERA.CONNECTION"),
                  QStringLiteral("event=network_connect slot=%1 result=OK address=%2 width=%3 height=%4 fps=%5")
                      .arg(slot)
                      .arg(NetworkStreamReader::redactStreamUrl(
                          current.networkAddress))
                      .arg(width)
                      .arg(height)
                      .arg(fps));
            }
          },
          Qt::QueuedConnection);

  connect(reader, &NetworkStreamReader::frameReady, this,
          [this, slot, generation, reader](int, const QImage& image) {
            UsbSlot& current = slots_[slot];
            if (!current.networkWanted || current.networkReader != reader ||
                current.networkGeneration != generation)
              return;
            current.lastFrameTime = clock_.elapsed();
            if (current.reconnectAttempts > 0) {
              const QString message = CRAWLING_TEXT("%1 已重新连接：%2")
                                          .arg(slotLabel(slot), current.networkAddress);
              current.reconnectAttempts = 0;
              emit connectionChanged(slot, true, message);
              emit logMessage(message);
            }
            emit frameChanged(slot, image);
          },
          Qt::QueuedConnection);

  connect(reader, &NetworkStreamReader::failed, this,
          [this, slot, generation, reader](int, const QString& reason) {
            UsbSlot& current = slots_[slot];
            if (!current.networkWanted || current.networkReader != reader ||
                current.networkGeneration != generation)
              return;
            emit logMessage(CRAWLING_TEXT("%1 打开失败：%2（%3）")
                                .arg(slotLabel(slot), slots_[slot].networkAddress, reason));
            AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"),
                             QStringLiteral("event=network_connect slot=%1 result=FAILED "
                                            "address=%2 error=%3")
                                 .arg(slot)
                                 .arg(NetworkStreamReader::redactStreamUrl(
                                     slots_[slot].networkAddress))
                                 .arg(reason));
            scheduleNetworkReconnect(slot);
          },
          Qt::QueuedConnection);

  reader->start();
}

void RobotUsbCameraController::scheduleNetworkReconnect(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted || !target.networkReconnectTimer ||
      target.networkReconnectTimer->isActive())
    return;

  stopNetworkReader(target);
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
                       .arg(NetworkStreamReader::redactStreamUrl(
                           target.networkAddress)));
  target.networkReconnectTimer->start(delayMs);
}

void RobotUsbCameraController::checkNetworkWatchdog(int slot) {
  UsbSlot& target = slots_[slot];
  if (!target.networkWanted || !target.networkReader)
    return;
  if (target.networkReconnectTimer && target.networkReconnectTimer->isActive())
    return;
  if (clock_.elapsed() - target.lastFrameTime < kNetworkFrameTimeoutMs)
    return;
  AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                   QStringLiteral("event=network_frame_timeout slot=%1 timeout_ms=%2 "
                                  "address=\"%3\"")
                       .arg(slot)
                       .arg(kNetworkFrameTimeoutMs)
                       .arg(NetworkStreamReader::redactStreamUrl(
                           target.networkAddress)));
  scheduleNetworkReconnect(slot);
}

}  // namespace crawling
