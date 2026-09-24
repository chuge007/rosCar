#pragma once

#include "wheel_synchronizer.h"

#include <QMetaType>
#include <QString>

class QSettings;

namespace crawling {

struct DriveSettings {
  static constexpr int kMinimumFeedbackTimeoutMs = 800;
  static constexpr int kCurrentSettingsSchemaVersion = 6;

  // Legacy shared-port fields are retained for loading older settings files.
  QString serialPort;
  int serialBaudRate = 115200;
  int canBitrate = 1000000;

  QString imuSerialPort;
  int imuBaudRate = 115200;
  int imuOutputDivider = 10;
  QString laserSerialNumber;
  int usbCameraDeviceIndex = -1;
  int usbCameraFps = 30;
  bool usbCameraAutoConnect = true;
  bool usbCameraFlipHorizontal = false;
  bool usbCameraFlipVertical = false;
  bool autoConnectOnStartup = true;
  int manualJogPercent = 30;

  QString clampSerialPort;
  int clampSerialBaudRate = 115200;
  int clampCanBitrate = 500000;
  int clampNodeId = 0;
  int clampXMotorId = 1;
  int clampYMotorId = 2;
  int clampZMotorId = 3;
  int clampXMotorSign = 1;
  int clampYMotorSign = 1;
  int clampZMotorSign = 1;

  int leftMotorId = 1;
  int rightMotorId = 2;
  QString leftMotorSerialPort;
  int leftMotorBaudRate = 115200;
  QString rightMotorSerialPort;
  int rightMotorBaudRate = 115200;
  int leftMotorSign = -1;
  int rightMotorSign = 1;

  // Measured PA1664 chassis geometry.
  double wheelRadiusM = 0.07953;
  double trackWidthM = 0.1473;
  // Motor-shaft revolutions per one wheel revolution. The installed MWD
  // gearbox is 36:1 and the motor output shaft is coupled 1:1 to the wheel.
  double motorOutputToWheelRatio = 36.0;

  // The paired motor ceiling is 4300 dps. With this wheel geometry it is
  // about 166 mm/s, so leave a small margin in the default command limit.
  double maximumWheelSpeedMps = 0.160;
  double maximumLinearSpeedMps = 0.120;
  double maximumAngularSpeedRadps = 0.8726646259971648; // 50 deg/s
  // These correspond to a moderate ramp while remaining inside the motor
  // protocol's 100..60000 dps/s acceleration range.
  double maximumLinearAccelerationMps2 = 0.200;
  double maximumAngularAccelerationRadps2 = 1.000;
  double minimumInnerWheelRatio = 0.50;

  int commandTimeoutMs = 300;
  int feedbackTimeoutMs = kMinimumFeedbackTimeoutMs;
  int armingTimeoutMs = 1000;
  SynchronizerConfig synchronizer;

  QString validationError() const;
  static QString persistentFilePath();
  void save(QSettings& settings) const;
  static DriveSettings load(QSettings& settings);
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::DriveSettings)
