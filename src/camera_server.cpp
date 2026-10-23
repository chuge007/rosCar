// Camera Server Implementation
#include "camera_server.h"

#include <QBuffer>
#include <QCameraDevice>
#include <QImage>
#include <QMediaDevices>
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

void CameraServer::cleanupSlot(CameraSlot& slot) {
  if (slot.camera) {
    slot.camera->stop();
    delete slot.camera;
    slot.camera = nullptr;
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
  if (slot.sharedMemory) {
    delete slot.sharedMemory;
    slot.sharedMemory = nullptr;
  }
}

void CameraServer::connectUsbCamera(int slot, int deviceIndex, int fps,
                                   bool flipH, bool flipV) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;

  CameraSlot& s = slots_[slot];
  cleanupSlot(s);

  s.flipHorizontal = flipH;
  s.flipVertical = flipV;

  const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
  if (deviceIndex < 0 || deviceIndex >= cameras.size()) {
    emit cameraStatusChanged(slot, false, QString("Camera %1 not found").arg(deviceIndex));
    return;
  }

  const QCameraDevice& device = cameras[deviceIndex];
  s.camera = new QCamera(device, this);
  s.videoSink = new QVideoSink(this);

  // Select low-resolution format
  QCameraFormat selectedFormat;
  double bestScore = std::numeric_limits<double>::max();
  for (const QCameraFormat& format : device.videoFormats()) {
    const QSize res = format.resolution();
    const int pixels = res.width() * res.height();
    const int targetPixels = 640 * 480;

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
  s.camera->setVideoOutput(s.videoSink);

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
  cleanupSlot(s);

  s.flipHorizontal = flipH;
  s.flipVertical = flipV;

  s.networkPlayer = new QMediaPlayer(this);
  s.networkVideoSink = new QVideoSink(this);
  s.networkPlayer->setVideoOutput(s.networkVideoSink);

  // Ultra-low latency configuration
  s.networkPlayer->setProperty("bufferSize", 1);
  s.networkPlayer->setProperty("rtsp_transport", "tcp");
  s.networkPlayer->setProperty("fflags", "nobuffer");
  s.networkPlayer->setProperty("flags", "low_delay");
  s.networkPlayer->setProperty("probesize", 32);
  s.networkPlayer->setProperty("analyzeduration", 0);

  connect(s.networkVideoSink, &QVideoSink::videoFrameChanged, this,
          [this, slot](const QVideoFrame& frame) { applyFrame(slot, frame); });

  s.networkPlayer->setSource(QUrl(url));
  s.networkPlayer->play();

  emit cameraStatusChanged(slot, true, QString("Network camera connected: %1").arg(url));
}

void CameraServer::disconnectCamera(int slot) {
  if (slot < 0 || slot >= kUsbCameraChannelCount)
    return;
  cleanupSlot(slots_[slot]);
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
  if (!s.frameProcessing.testAndSetAcquire(0, 1))
    return;  // Drop frame if previous is still processing

  QImage image = frame.toImage();
  if (image.isNull()) {
    s.frameProcessing.storeRelease(0);
    return;
  }

  // Scale to target size
  if (s.targetWidth > 0 && s.targetHeight > 0 &&
      (image.width() > s.targetWidth || image.height() > s.targetHeight)) {
    image = image.scaled(QSize(s.targetWidth, s.targetHeight),
                        Qt::KeepAspectRatio, Qt::FastTransformation);
  }

  // Mirror
  if (s.flipHorizontal || s.flipVertical) {
    image = image.mirrored(s.flipHorizontal, s.flipVertical);
  }

  // Write to shared memory
  if (s.sharedMemory && s.sharedMemory->isAttached()) {
    QBuffer buffer;
    buffer.open(QBuffer::WriteOnly);
    if (image.save(&buffer, "JPG", 85)) {  // 85% quality JPEG
      const QByteArray data = buffer.data();
      if (data.size() <= kSharedMemorySize) {
        s.sharedMemory->lock();
        memcpy(s.sharedMemory->data(), data.constData(), data.size());
        // Write size at the beginning
        *reinterpret_cast<quint32*>(s.sharedMemory->data()) = data.size();
        s.sharedMemory->unlock();

        emit frameReady(slot, s.shmKey);
      }
    }
  }

  s.frameProcessing.storeRelease(0);
}

}  // namespace crawling
