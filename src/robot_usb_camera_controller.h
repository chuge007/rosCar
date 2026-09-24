#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

#include <memory>

class QTimer;
namespace cv { class VideoCapture; }

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
  void captureFrame();

 private:
  QTimer* captureTimer_ = nullptr;
  std::unique_ptr<cv::VideoCapture> capture_;
  int failedReadCount_ = 0;
  bool flipHorizontal_ = false;
  bool flipVertical_ = false;
};

}  // namespace crawling

