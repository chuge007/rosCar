#pragma once

#include "rim302_protocol.h"

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QSerialPort>
#include <QTimer>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <memory>

namespace mv3dlp { class Driver; }

namespace crawling {
class DeviceController final : public QObject {
 Q_OBJECT
 public:
  explicit DeviceController(QObject* parent = nullptr);
  QVector<QVector3D> takeLatestPointCloudProfile();
  QImage takeLatestCameraImage();
 public slots:
  void connectImu(const QString& portName, int baudRate, int divider);
  void disconnectImu();
  void autoDetectDevices(const QString& preferredImuPort, const QString& preferredLaserSerial);
  void scanCamera();
  void connectCamera(const QString& serialNumber);
  void disconnectCamera();
  void shutdown();
 signals:
  void imuConnectionChanged(bool connected, const QString& message);
  void sensorSettingsDetected(const QString& imuPort, int imuBaudRate,
                              const QString& laserSerialNumber);
  void deviceDetectionChanged(bool running, const QString& message);
  void imuSampleChanged(const crawling::ImuSample& sample);
  void cameraDevicesChanged(const QStringList& devices);
  void cameraConnectionChanged(bool connected, const QString& message);
  void cameraFrameChanged(quint32 frameNumber, quint32 width, quint32 height, quint64 pointCount);
  void pointCloudProfileChanged(const QVector<QVector3D>& points);
  void pointCloudProfileReady();
  void cameraImageReady();
  void logMessage(const QString& message);
 private slots:
  void readImu();
  void captureCameraFrame();
 private:
  QSerialPort imuPort_;
  Rim302FrameParser imuParser_;
  QTimer* cameraTimer_ = nullptr;
  std::unique_ptr<mv3dlp::Driver> camera_;
  qint64 lastImuLogMs_ = 0;
  qint64 lastCameraLogMs_ = 0;
  qint64 lastCameraStatsLogMs_ = 0;
  qint64 lastPointCloudEmitMs_ = -1000;
  quint64 cameraPollCount_ = 0;
  quint64 cameraTimeoutCount_ = 0;
  quint64 cameraInvalidFrameCount_ = 0;
  quint64 cameraEmptyDataCount_ = 0;
  quint64 cameraFrameCount_ = 0;
  quint64 cameraPreviewPointCount_ = 0;
  quint32 lastCameraFrameNumber_ = 0;
  int imuBaudRate_ = 115200;
  QMutex pointCloudMutex_;
  QVector<QVector3D> latestPointCloudProfile_;
  QImage latestCameraImage_;
  bool pointCloudSignalPending_ = false;
  bool cameraImageSignalPending_ = false;
};
}  // namespace crawling

Q_DECLARE_METATYPE(crawling::ImuSample)
Q_DECLARE_METATYPE(QVector<QPointF>)
Q_DECLARE_METATYPE(QVector<QVector3D>)
