// Camera Server Client - Manages camera server process and receives frames
#ifndef CRAWLING_CAMERA_SERVER_CLIENT_H_
#define CRAWLING_CAMERA_SERVER_CLIENT_H_

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSharedMemory>
#include <QTimer>
#include "robot/drive_settings.h"  // for kUsbCameraChannelCount

namespace crawling {

// Client that connects to camera server process via shared memory
class CameraServerClient : public QObject {
  Q_OBJECT

 public:
  explicit CameraServerClient(QObject* parent = nullptr);
  ~CameraServerClient() override;

  bool start();
  void stop();

  // Camera control
  void connectUsbCamera(int slot, int deviceIndex, int fps,
                        bool flipH, bool flipV);
  void connectNetworkCamera(int slot, const QString& url,
                            bool flipH, bool flipV);
  void disconnectCamera(int slot);
  void setDisplaySize(int slot, int width, int height);

 signals:
  void frameReady(int slot, const QImage& image);
  void cameraStatusChanged(int slot, bool connected, const QString& message);
  void serverStatusChanged(bool running);

 private slots:
  void onServerStarted();
  void onServerFinished(int exitCode, QProcess::ExitStatus status);
  void onServerError(QProcess::ProcessError error);
  void onServerOutput();
  void pollSharedMemory();

 private:
  void sendCommand(const QString& command);
  QImage readFrameFromSharedMemory(int slot);

  QProcess* serverProcess_ = nullptr;
  QSharedMemory* sharedMemory_[kUsbCameraChannelCount] = {nullptr};
  QString ipcKeyBase_;
  QTimer* pollTimer_ = nullptr;

  quint32 lastFrameSize_[kUsbCameraChannelCount] = {0};
};

}  // namespace crawling

#endif  // CRAWLING_CAMERA_SERVER_CLIENT_H_
