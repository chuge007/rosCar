// Camera Server - Manages all camera capture in isolated process
#ifndef CRAWLING_CAMERA_SERVER_H_
#define CRAWLING_CAMERA_SERVER_H_

#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QObject>
#include <QSharedMemory>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QAtomicInt>
#include <QElapsedTimer>
#include "robot/drive_settings.h"

namespace crawling {

// Single camera slot managed by server
struct CameraSlot {
  // Camera hardware
  QCamera* camera = nullptr;
  QMediaCaptureSession* captureSession = nullptr;
  QVideoSink* videoSink = nullptr;

  // Network stream
  QMediaPlayer* networkPlayer = nullptr;
  QVideoSink* networkVideoSink = nullptr;

  // Shared memory for this slot
  QSharedMemory* sharedMemory = nullptr;
  QString shmKey;

  // Configuration
  int targetWidth = 640;
  int targetHeight = 480;
  bool flipHorizontal = false;
  bool flipVertical = false;

  // State
  QAtomicInt frameProcessing{0};
  qint64 lastFrameTime = 0;
  int consecutiveDrops = 0;  // Track dropped frames for network streams
  QTimer* bufferClearTimer = nullptr;  // Periodic buffer flush for network
};

class CameraServer : public QObject {
  Q_OBJECT

 public:
  explicit CameraServer(const QString& ipcKeyBase, QObject* parent = nullptr);
  ~CameraServer() override;

  bool start();
  void stop();

 public slots:
  // Camera control (invoked via QLocalServer commands)
  void connectUsbCamera(int slot, int deviceIndex, int fps,
                        bool flipH, bool flipV);
  void connectNetworkCamera(int slot, const QString& url,
                            bool flipH, bool flipV);
  void disconnectCamera(int slot);
  void setDisplaySize(int slot, int width, int height);

 signals:
  void frameReady(int slot, const QString& shmKey);
  void cameraStatusChanged(int slot, bool connected, const QString& message);

 private:
  void applyFrame(int slot, const QVideoFrame& frame);
  bool setupSharedMemory(int slot);
  // Shared memory survives camera switches; only stop() releases it.
  void cleanupSlot(CameraSlot& slot, bool releaseSharedMemory = true);

  QString ipcKeyBase_;
  CameraSlot slots_[kUsbCameraChannelCount];
  QElapsedTimer clock_;
};

}  // namespace crawling

#endif  // CRAWLING_CAMERA_SERVER_H_
