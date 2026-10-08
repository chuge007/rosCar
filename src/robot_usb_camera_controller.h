#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

class QCamera;
class QMediaCaptureSession;
class QVideoFrame;
class QVideoSink;

namespace crawling {

class RobotUsbCameraController final : public QObject {
  Q_OBJECT

 public:
  explicit RobotUsbCameraController(QObject* parent = nullptr);
  ~RobotUsbCameraController() override;

 public slots:
  void scanDevices();
  void connectCamera(int deviceIndex, int fps, bool flipHorizontal, bool flipVertical);
  void disconnectCamera();
  void shutdown();

 signals:
  void devicesChanged(const QStringList& devices);
  void connectionChanged(bool connected, const QString& message);
  void frameChanged(const QImage& image);
  void logMessage(const QString& message);

 private slots:
  void handleVideoFrame(const QVideoFrame& frame);

 private:
  QCamera* camera_ = nullptr;
  QMediaCaptureSession* captureSession_ = nullptr;
  QVideoSink* videoSink_ = nullptr;
  bool flipHorizontal_ = false;
  bool flipVertical_ = false;
};

}  // namespace crawling

