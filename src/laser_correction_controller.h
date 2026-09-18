#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QVector3D>

#include "rim302_protocol.h"

namespace crawling {

struct LaserCorrectionSettings {
  double lateralMinM = -0.20;
  double lateralMaxM = 0.20;
  double targetLateralM = 0.0;
  // IMU yaw target. Set this to the weld direction in the IMU frame.
  double headingReferenceRad = 0.0;
  double prominenceThresholdM = 0.0015;
  double contourHalfWidthM = 0.025;
  double filterAlpha = 0.22;
  double derivativeAlpha = 0.18;
  double proportionalGain = 3.0;
  // Heading feedback and gyro damping are both expressed in rad/s per input
  // unit. The gyro term uses the measured yaw rate instead of differentiating
  // the noisy laser position.
  double headingGain = 1.8;
  double derivativeGain = 0.35;
  int steeringSign = 1;
  int headingSign = 1;
  double deadbandM = 0.0015;
  double headingDeadbandRad = 0.00872664626;
  double maxLateralErrorM = 0.08;
  double maxHeadingErrorRad = 0.70;
  double targetSpeedMps = 0.025;
  double minSpeedScale = 0.35;
  double speedReductionGain = 0.8;
  double maxAngularRadps = 0.45;
  double maxAngularAccelerationRadps2 = 0.9;
  double maxLinearAccelerationMps2 = 0.05;
  int profileTimeoutMs = 180;
  int imuTimeoutMs = 250;
  bool lateralUsesY = true;
  bool heightUsesZ = true;
};

struct LaserCorrectionStatus {
  bool active = false;
  bool contourValid = false;
  double contourLateralM = 0.0;
  double lateralErrorM = 0.0;
  double headingErrorRad = 0.0;
  double gyroRadps = 0.0;
  double confidence = 0.0;
  double linearCommandMps = 0.0;
  double angularCommandRadps = 0.0;
  int candidatePoints = 0;
  QString reason;
};

class LaserCorrectionController final : public QObject {
  Q_OBJECT
 public:
  explicit LaserCorrectionController(QObject* parent = nullptr);
 public slots:
  void setSettings(const crawling::LaserCorrectionSettings& settings);
  void setEnabled(bool enabled);
  void resetReferenceToCurrentContour();
  void setImuSample(const crawling::ImuSample& sample);
  void processPointCloud(const QVector<QVector3D>& points);
 signals:
  void commandChanged(double linearMps, double angularRadps);
  void statusChanged(const crawling::LaserCorrectionStatus& status);
  void logMessage(const QString& message);
 private slots:
  void watchdogTick();
 private:
  void stop(const QString& reason);
  void publishStatus();
  LaserCorrectionSettings settings_;
  LaserCorrectionStatus status_;
  QElapsedTimer clock_;
  QTimer watchdog_;
  qint64 lastProfileMs_ = -1;
  qint64 lastImuMs_ = -1;
  qint64 previousControlMs_ = -1;
  double filteredError_ = 0.0;
  double filteredHeadingError_ = 0.0;
  double filteredGyroRadps_ = 0.0;
  double currentYawRad_ = 0.0;
  double currentGyroRadps_ = 0.0;
  double currentLinear_ = 0.0;
  double currentAngular_ = 0.0;
};
}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserCorrectionSettings)
Q_DECLARE_METATYPE(crawling::LaserCorrectionStatus)
