#pragma once

#include "wheel_synchronizer.h"

#include <QMetaType>
#include <QString>

class QSettings;

namespace crawling {

struct DriveSettings {
  static constexpr int kMinimumFeedbackTimeoutMs = 800;
  static constexpr int kCurrentSettingsSchemaVersion = 4;

  // Legacy shared-port fields are retained for loading older settings files.
  QString serialPort;
  int serialBaudRate = 115200;
  int canBitrate = 1000000;

  QString imuSerialPort;
  int imuBaudRate = 115200;
  int imuOutputDivider = 10;
  QString laserSerialNumber;
  bool autoConnectOnStartup = true;
  int manualJogPercent = 30;

  QString clampSerialPort;
  int clampSerialBaudRate = 115200;
  int clampCanBitrate = 500000;
  int clampNodeId = 0;

  int leftMotorId = 1;
  int rightMotorId = 2;
  QString leftMotorSerialPort;
  int leftMotorBaudRate = 115200;
  QString rightMotorSerialPort;
  int rightMotorBaudRate = 115200;
  int leftMotorSign = 1;
  int rightMotorSign = -1;

  double wheelRadiusM = 0.040;
  double trackWidthM = 0.300;
  // Motor-shaft revolutions per one wheel revolution. The installed MWD
  // gearbox is 36:1 and the motor output shaft is coupled 1:1 to the wheel.
  double motorOutputToWheelRatio = 36.0;

  double maximumWheelSpeedMps = 0.30;
  double maximumLinearSpeedMps = 0.15;
  double maximumAngularSpeedRadps = 1.00;
  double maximumLinearAccelerationMps2 = 0.50;
  double maximumAngularAccelerationRadps2 = 2.00;
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
