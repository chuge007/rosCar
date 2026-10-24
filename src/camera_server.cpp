// Camera Server Implementation
#include "camera_server.h"

#include <QBuffer>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QDebug>
#include <QImage>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>
#include <limits>

namespace crawling {

namespace {
constexpr int kSharedMemorySize = 4 * 1024 * 1024;  // 4MB per slot
}

CameraServer::CameraServer(const QString& ipcKeyBase, QObject* parent)
    : QObject(parent), ipcKeyBase_(ipcKeyBase) {
  clock_.start();
}

CameraServer::~CameraServer() {
  stop();
}

bool CameraServer::start() {
  for (int i = 0; i < kUsbCameraChannelCount; ++i) {
    slots_[i].shmKey = QString("%1_slot%2").arg(ipcKeyBase_).arg(i);
    if (!setupSharedMemory(i)) {
      qWarning() << "Failed to setup shared memory for slot" << i;
      return false;
    }
  }
  qInfo() << "Camera server initialized with" << kUsbCameraChannelCount << "slots";
  return true;
}

void CameraServer::stop() {
  for (int i = 0; i < kUsbCameraChannelCount; ++i) {
    cleanupSlot(slots_[i]);
  }
}

bool CameraServer::setupSharedMemory(int slot) {
  CameraSlot& s = slots_[slot];
  if (s.sharedMemory) {
    delete s.sharedMemory;
  }

  s.sharedMemory = new QSharedMemory(s.shmKey, this);

  // Try to attach first (in case of previous unclean shutdown)
  if (s.sharedMemory->attach()) {
    s.sharedMemory->detach();
  }

  if (!s.sharedMemory->create(kSharedMemorySize)) {
    qWarning() << "Failed to create shared memory" << s.shmKey
               << ":" << s.sharedMemory->errorString();
    return false;
  }

  qInfo() << "Created shared memory" << s.shmKey << "size" << kSharedMemorySize;
  return true;
}

void CameraServer::cleanupSlot(CameraSlot& slot, bool releaseSharedMemory) {
  // Detach the sink before destroying either endpoint so the session cannot
  // retain a dangling camera/sink pointer during a camera switch.
  if (slot.captureSession) {
    slot.captureSession->setCamera(nullptr);
    slot.captureSession->setVideoSink(nullptr);
  }
  if (slot.camera) {
    slot.camera->stop();
    delete slot.camera;
    slot.camera = nullptr;
  }
  if (slot.captureSession) {
    delete slot.captureSession;
    slot.captureSession = nullptr;
  }
  if (slot.videoSink) {
    delete slot.videoSink;
    slot.videoSink = nullptr;
  }
  if (slot.networkPlayer) {
    slot.networkPlayer->stop();
    delete slot.networkPlayer;
    slot.networkPlayer = nullptr;
  }
  if (slot.networkVideoSink) {
    delete slot.networkVideoSink;
    slot.networkVideoSink = nullptr;
  }
  if (slot.bufferClearTimer) {
    slot.bufferClearTimer->stop();
    delete slot.bufferClearTimer;
    slot.bufferClearTimer = nullptr;
  }
  if (releaseSharedMemory && slot.sharedMemory) {
    delete slot.sharedMemory;
    slot.sharedMemory = nullptr;
  }
  slot.consecutiveDrops = 0;
}

void CameraServer::connectUsbCamera(int slot, int deviceIndex, int fps,
                                   bool flipH, bool flipV) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;

  CameraSlot& s = slots_[slot];
  cleanupSlot(s, false);

  s.flipHorizontal = flipH;
  s.flipVertical = flipV;

  // Clamp FPS to maximum 15 for lower latency
  fps = std::min(fps, 15);

  const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
  if (deviceIndex < 0 || deviceIndex >= cameras.size()) {
    emit cameraStatusChanged(slot, false, QString("Camera %1 not found").arg(deviceIndex));
    return;
  }

  const QCameraDevice& device = cameras[deviceIndex];
  s.camera = new QCamera(device, this);
  s.videoSink = new QVideoSink(this);

  // Select ultra-low-resolution format (320x240 preferred)
  QCameraFormat selectedFormat;
  double bestScore = std::numeric_limits<double>::max();
  for (const QCameraFormat& format : device.videoFormats()) {
    const QSize res = format.resolution();
    const int pixels = res.width() * res.height();
    const int targetPixels = 320 * 240;  // Much lower resolution

    const bool fpsMatch =
        fps >= format.minFrameRate() && fps <= format.maxFrameRate();
    const double fpsPenalty = fpsMatch ? 0.0 : std::abs(format.minFrameRate() - fps);
    const double resPenalty =
        pixels <= targetPixels
            ? static_cast<double>(targetPixels - pixels) / targetPixels
            : 4.0 + static_cast<double>(pixels - targetPixels) / targetPixels;

    const double score = fpsPenalty * 100.0 + resPenalty;
    if (score < bestScore) {
      bestScore = score;
      selectedFormat = format;
    }
  }

  if (selectedFormat.isNull()) {
    emit cameraStatusChanged(slot, false, "No suitable format found");
    return;
  }

  s.camera->setCameraFormat(selectedFormat);
  s.captureSession = new QMediaCaptureSession(this);
  s.captureSession->setCamera(s.camera);
  s.captureSession->setVideoSink(s.videoSink);

  connect(s.videoSink, &QVideoSink::videoFrameChanged, this,
          [this, slot](const QVideoFrame& frame) { applyFrame(slot, frame); });

  s.camera->start();

  emit cameraStatusChanged(
      slot, true,
      QString("USB Camera %1 connected: %2x%3@%4fps")
          .arg(deviceIndex)
          .arg(selectedFormat.resolution().width())
          .arg(selectedFormat.resolution().height())
          .arg(selectedFormat.maxFrameRate()));
}

void CameraServer::connectNetworkCamera(int slot, const QString& url,
                                       bool flipH, bool flipV) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;

  CameraSlot& s = slots_[slot];
  cleanupSlot(s, false);

  s.flipHorizontal = flipH;
  s.flipVertical = flipV;
  s.consecutiveDrops = 0;

  s.networkPlayer = new QMediaPlayer(this);
  s.networkVideoSink = new QVideoSink(this);
  s.networkPlayer->setVideoSink(s.networkVideoSink);

  // Ultra-low latency configuration
  // Qt Multimedia does not forward these FFmpeg options as QObject properties.
  // The standalone server is only the optional fallback path; the workbench
  // uses NetworkStreamReader for genuinely low-latency RTSP/HTTP capture.

  connect(s.networkVideoSink, &QVideoSink::videoFrameChanged, this,
          [this, slot](const QVideoFrame& frame) { applyFrame(slot, frame); });

  // Periodic buffer clear to prevent accumulation
  s.bufferClearTimer = new QTimer(this);
  s.bufferClearTimer->setInterval(50);  // Every 50ms, aggressively clear old frames
  connect(s.bufferClearTimer, &QTimer::timeout, this, [this, slot]() {
    CameraSlot& s = slots_[slot];
    if (s.consecutiveDrops > 5) {
      // If we're dropping too many frames, the stream might be buffering
      // Force a position update to flush buffer
      if (s.networkPlayer && s.networkPlayer->isPlaying()) {
        qint64 pos = s.networkPlayer->position();
        s.networkPlayer->setPosition(pos);
      }
      s.consecutiveDrops = 0;
    }
  });
  s.bufferClearTimer->start();

  s.networkPlayer->setSource(QUrl(url));
  s.networkPlayer->play();

  emit cameraStatusChanged(slot, true, QString("Network camera connected: %1").arg(url));
}

void CameraServer::disconnectCamera(int slot) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  cleanupSlot(slots_[slot], false);
  emit cameraStatusChanged(slot, false, "Disconnected");
}

void CameraServer::setDisplaySize(int slot, int width, int height) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  slots_[slot].targetWidth = width;
  slots_[slot].targetHeight = height;
}

void CameraServer::applyFrame(int slot, const QVideoFrame& frame) {
  if (!frame.isValid())
    return;

  CameraSlot& s = slots_[slot];
  if (!s.frameProcessing.testAndSetAcquire(0, 1)) {
    // Frame dropped - increment counter for network streams
    if (s.networkPlayer) {
      s.consecutiveDrops++;
    }
    return;  // Drop frame if previous is still processing
  }

  // Reset drop counter on successful processing
  s.consecutiveDrops = 0;

  QImage image = frame.toImage();
  if (image.isNull()) {
    s.frameProcessing.storeRelease(0);
    return;
  }

  // Scale to target size (network cameras often stream at high resolution)
  if (s.targetWidth > 0 && s.targetHeight > 0) {
    // Force scale down to 320x240 maximum for network streams
    int maxWidth = std::min(s.targetWidth, 320);
    int maxHeight = std::min(s.targetHeight, 240);
    if (image.width() > maxWidth || image.height() > maxHeight) {
      image = image.scaled(QSize(maxWidth, maxHeight),
                          Qt::KeepAspectRatio, Qt::FastTransformation);
    }
  }

  // Mirror
  if (s.flipHorizontal || s.flipVertical) {
    image = image.mirrored(s.flipHorizontal, s.flipVertical);
  }

  // Write to shared memory
  if (s.sharedMemory && s.sharedMemory->isAttached()) {
    QBuffer buffer;
    buffer.open(QBuffer::WriteOnly);
    // Lower JPEG quality for network streams to reduce latency
    int quality = s.networkPlayer ? 65 : 75;
    if (image.save(&buffer, "JPG", quality)) {
      const QByteArray data = buffer.data();
      if (data.size() + sizeof(quint32) <= kSharedMemorySize) {
        s.sharedMemory->lock();
        // Publish the JPEG before its length so the reader cannot observe a
        // new size while the payload is still being copied.
        memcpy(static_cast<char*>(s.sharedMemory->data()) + sizeof(quint32),
               data.constData(), data.size());
        *reinterpret_cast<quint32*>(s.sharedMemory->data()) = data.size();
        s.sharedMemory->unlock();

        emit frameReady(slot, s.shmKey);
      }
    }
  }

  s.frameProcessing.storeRelease(0);
}

}  // namespace crawling
