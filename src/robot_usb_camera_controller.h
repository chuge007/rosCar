#pragma once

#include "drive_settings.h"

#include <QImage>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

class QCamera;
class QMediaCaptureSession;
class QVideoFrame;
class QVideoSink;
class QTimer;

namespace crawling {

class CameraFrameProcessor;
class CameraServerClient;
class NetworkStreamReader;

// Mode selection for camera processing
enum class CameraProcessMode {
  InProcess,       // Low-latency in-process capture (default)
  IsolatedProcess  // Optional camera server process
};

class RobotUsbCameraController final : public QObject {
  Q_OBJECT

 public:
  explicit RobotUsbCameraController(QObject* parent = nullptr,
                                   CameraProcessMode mode = CameraProcessMode::InProcess);
  ~RobotUsbCameraController() override;

 public slots:
  void scanDevices();
  void frameConsumed(int slot);
  void setDisplaySize(int slot, int width, int height);
  void connectCamera(int slot, int deviceIndex, int fps, bool flipHorizontal, bool flipVertical);
  void connectNetworkCamera(int slot, const QString& address, bool flipHorizontal, bool flipVertical);
  void disconnectCamera(int slot);
  void shutdown();

 signals:
  void devicesChanged(const QStringList& devices);
  void connectionChanged(int slot, bool connected, const QString& message);
  void frameChanged(int slot, const QImage& image);
  void logMessage(const QString& message);

 private:
  struct UsbSlot {
    QCamera* camera = nullptr;
    QMediaCaptureSession* captureSession = nullptr;
    QVideoSink* videoSink = nullptr;
    QTimer* networkWatchdog = nullptr;
    QTimer* networkReconnectTimer = nullptr;
    NetworkStreamReader* networkReader = nullptr;
    quint64 networkGeneration = 0;
    QString networkAddress;
    qint64 lastFrameTime = 0;
    int reconnectAttempts = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool networkWanted = false;
    bool networkOpening = false;
    QAtomicInt frameProcessing{0};
    QAtomicInt targetWidth{260};
    QAtomicInt targetHeight{160};
    // 2 s window counters for CAMERA.CONTROLLER: proves that frameReady
    // arrives, frameChanged is emitted, frameConsumed releases it, and that
    // stale-generation frames are dropped without latching the reader.
    quint64 frameReadyCount = 0;
    quint64 frameChangedCount = 0;
    quint64 frameConsumedCount = 0;
    quint64 staleGenerationCount = 0;
  };
  void maybeLogControllerStats(qint64 nowMs);
  void teardownSlot(UsbSlot& slot);
  void stopNetworkReader(UsbSlot& slot);
  void startNetworkStream(int slot);
  void scheduleNetworkReconnect(int slot, const QString& trigger);
  void checkNetworkWatchdog(int slot);
  void applyFrame(UsbSlot& slot, int slotIndex, const QVideoFrame& frame);

  CameraProcessMode mode_;
  QElapsedTimer clock_;
  qint64 lastControllerLogMs_ = -100000;
  UsbSlot slots_[kUsbCameraChannelCount];

  // In-process mode components
  CameraFrameProcessor* frameProcessor_ = nullptr;

  // Isolated process mode components
  CameraServerClient* cameraClient_ = nullptr;
};

}  // namespace crawling
