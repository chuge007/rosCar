#pragma once

#include "rim302_protocol.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
namespace crawling {
class DeviceController;
class DeviceWindow final : public QDialog {
  Q_OBJECT
 public:
  explicit DeviceWindow(DeviceController* controller, QWidget* parent = nullptr);
 private slots:
  void refreshPorts();
  void autoDetect();
  void connectImu();
  void scanCamera();
  void connectCamera();
  void updateImu(const ImuSample& sample);
  void updateImuState(bool connected, const QString& message);
  void applySensorDetection(const QString& imuPort, int imuBaudRate,
                            const QString& laserSerialNumber);
  void updateDetectionState(bool running, const QString& message);
  void updateCameraList(const QStringList& devices);
  void updateCameraState(bool connected, const QString& message);
  void updateCameraFrame(quint32 frame, quint32 width, quint32 height, quint64 points);
 private:
  DeviceController* controller_;
  QComboBox* imuPort_;
  QComboBox* imuBaud_;
  QComboBox* imuDivider_;
  QLabel* imuState_;
  QLabel* imuValues_;
  QPushButton* imuConnect_;
  QPushButton* autoDetect_;
  QComboBox* cameraList_;
  QLabel* cameraState_;
  QLabel* cameraFrame_;
  QPushButton* cameraConnect_;
};
}  // namespace crawling
