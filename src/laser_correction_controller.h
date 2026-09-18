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
  // Distance from the vehicle control point to the laser plane. Set to zero
  // when the incoming profile already represents the laser-point error.
  double lookaheadDistanceM = 0.30;
  // IMU yaw target. Set this to the weld direction in the IMU frame.
  double headingReferenceRad = 0.0;
  double prominenceThresholdM = 0.0015;
  double contourHalfWidthM = 0.025;
  // Legacy fixed-alpha values are retained as fallbacks when a cutoff is 0.
  double filterAlpha = 0.22;
  double derivativeAlpha = 0.18;
  double lateralFilterCutoffHz = 4.0;
  double headingFilterCutoffHz = 4.0;
  double gyroFilterCutoffHz = 8.0;
  double proportionalGain = 3.0;
  double integralGain = 0.0;
  double integralLimitMs = 0.30;
  // Heading feedback and gyro damping use measured IMU signals. The gyro term
  // is negative feedback, not a derivative of the noisy laser position.
  double headingGain = 1.8;
  double derivativeGain = 0.35;
  int steeringSign = 1;
  int headingSign = 1;
  int gyroSign = 1;
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
  double sensorLateralErrorM = 0.0;
  double lookaheadOffsetM = 0.0;
  double lateralErrorM = 0.0;
  double headingErrorRad = 0.0;
  double gyroRadps = 0.0;
  double lateralIntegralMs = 0.0;
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
  qint64 previousImuMs_ = -1;
  bool lateralFilterInitialized_ = false;
  bool headingFilterInitialized_ = false;
  bool gyroFilterInitialized_ = false;
  double filteredError_ = 0.0;
  double filteredHeadingError_ = 0.0;
  double filteredGyroRadps_ = 0.0;
  double lateralIntegralMs_ = 0.0;
  double currentYawRad_ = 0.0;
  double currentGyroRadps_ = 0.0;
  double currentLinear_ = 0.0;
  double currentAngular_ = 0.0;
};
}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserCorrectionSettings)
Q_DECLARE_METATYPE(crawling::LaserCorrectionStatus)
