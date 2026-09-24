#include "robot_usb_camera_controller.h"

#include "robot_app_logger.h"
#include "utf8_compat.h"

#include <QTimer>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <exception>

namespace crawling {

RobotUsbCameraController::RobotUsbCameraController(QObject* parent) : QObject(parent) {
  captureTimer_ = new QTimer(this);
  captureTimer_->setTimerType(Qt::PreciseTimer);
  connect(captureTimer_, &QTimer::timeout, this, &RobotUsbCameraController::captureFrame);
}

RobotUsbCameraController::~RobotUsbCameraController() = default;

void RobotUsbCameraController::scanDevices() {
  QStringList devices;
  try {
    for (int index = 0; index < 10; ++index) {
      cv::VideoCapture probe;
      bool opened = probe.open(index, cv::CAP_DSHOW);
      if (!opened) {
        probe.release();
        opened = probe.open(index, cv::CAP_ANY);
      }
      if (opened) {
        devices.append(CRAWLING_TEXT("%1 | USB 摄像头").arg(index));
        probe.release();
      }
    }
    emit devicesChanged(devices);
    const QString message = CRAWLING_TEXT("USB 摄像头扫描到 %1 个设备").arg(devices.size());
    emit logMessage(message);
    AppLogger::write(QStringLiteral("USB_CAMERA.DISCOVERY"),
                     QStringLiteral("event=scan_complete result=OK device_count=%1")
                         .arg(devices.size()));
  } catch (const std::exception& error) {
    const QString message = CRAWLING_TEXT("USB 摄像头扫描失败：%1")
                                .arg(QString::fromLocal8Bit(error.what()));
    emit connectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.DISCOVERY"), message);
  }
}

void RobotUsbCameraController::connectCamera(int deviceIndex, int fps,
                                        bool flipHorizontal, bool flipVertical) {
  disconnectCamera();
  if (deviceIndex < 0) {
    emit connectionChanged(false, CRAWLING_TEXT("USB 摄像头打开失败：设备编号无效"));
    return;
  }

  try {
    capture_ = std::make_unique<cv::VideoCapture>();
    bool opened = capture_->open(deviceIndex, cv::CAP_DSHOW);
    if (!opened) {
      capture_->release();
      opened = capture_->open(deviceIndex, cv::CAP_ANY);
    }
    if (!opened) {
      capture_.reset();
      const QString message = CRAWLING_TEXT("USB 摄像头打开失败：设备 %1").arg(deviceIndex);
      emit connectionChanged(false, message);
      emit logMessage(message);
      return;
    }

    capture_->set(cv::CAP_PROP_FPS, fps);
    capture_->set(cv::CAP_PROP_BUFFERSIZE, 1);
    flipHorizontal_ = flipHorizontal;
    flipVertical_ = flipVertical;
    failedReadCount_ = 0;
    captureTimer_->setInterval(std::max(1, 1000 / std::max(1, fps)));
    captureTimer_->start();

    int actualWidth = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_WIDTH));
    int actualHeight = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_HEIGHT));
    if (actualWidth <= 0 || actualHeight <= 0) {
      actualWidth = 0;
      actualHeight = 0;
    }
    double actualFps = capture_->get(cv::CAP_PROP_FPS);
    if (actualFps <= 0.0) actualFps = fps;
    const QString message =
        CRAWLING_TEXT("USB 摄像头已连接：设备 %1，实际画面 %2 x %3 @ %4 fps")
            .arg(deviceIndex).arg(actualWidth).arg(actualHeight).arg(actualFps, 0, 'f', 1);
    emit connectionChanged(true, message);
    emit logMessage(message);
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=connect_complete result=OK device=%1 width=%2 height=%3 fps=%4")
                         .arg(deviceIndex).arg(actualWidth).arg(actualHeight).arg(actualFps));
  } catch (const std::exception& error) {
    capture_.reset();
    const QString message = CRAWLING_TEXT("USB 摄像头打开失败：%1")
                                .arg(QString::fromLocal8Bit(error.what()));
    emit connectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.CONNECTION"), message);
  }
}

void RobotUsbCameraController::disconnectCamera() {
  captureTimer_->stop();
  const bool wasConnected = capture_ && capture_->isOpened();
  if (capture_) {
    capture_->release();
    capture_.reset();
  }
  failedReadCount_ = 0;
  emit connectionChanged(false, CRAWLING_TEXT("USB 摄像头未连接"));
  if (wasConnected) {
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"),
                     QStringLiteral("event=disconnect_complete result=OK"));
  }
}

void RobotUsbCameraController::shutdown() {
  disconnectCamera();
}

void RobotUsbCameraController::captureFrame() {
  if (!capture_ || !capture_->isOpened()) return;
  try {
    cv::Mat frame;
    if (!capture_->read(frame) || frame.empty()) {
      if (++failedReadCount_ >= 15) {
        disconnectCamera();
        const QString message = CRAWLING_TEXT("USB 摄像头取帧失败，连接已关闭");
        emit connectionChanged(false, message);
        emit logMessage(message);
      }
      return;
    }
    failedReadCount_ = 0;
    if (flipHorizontal_ || flipVertical_) {
      int flipCode = flipHorizontal_ && flipVertical_ ? -1 : (flipHorizontal_ ? 1 : 0);
      cv::flip(frame, frame, flipCode);
    }
    if (frame.channels() == 1) {
      const QImage image(frame.data, frame.cols, frame.rows,
                         static_cast<int>(frame.step), QImage::Format_Grayscale8);
      emit frameChanged(image.copy());
      return;
    }

    cv::Mat rgb;
    cv::cvtColor(frame, rgb,
                 frame.channels() == 4 ? cv::COLOR_BGRA2RGB : cv::COLOR_BGR2RGB);
    const QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                       QImage::Format_RGB888);
    emit frameChanged(image.copy());
  } catch (const std::exception& error) {
    disconnectCamera();
    const QString message = CRAWLING_TEXT("USB 摄像头取帧失败：%1")
                                .arg(QString::fromLocal8Bit(error.what()));
    emit connectionChanged(false, message);
    emit logMessage(message);
    AppLogger::error(QStringLiteral("USB_CAMERA.ACQUISITION"), message);
  }
}

}  // namespace crawling

