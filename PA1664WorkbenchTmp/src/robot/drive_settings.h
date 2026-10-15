#pragma once

#include "wheel_synchronizer.h"

#include <QMetaType>
#include <QString>
#include <QVector>

#include <array>

class QSettings;

namespace crawling {

enum class WheelCommunicationMode {
  Can,
  Rs485,
};

enum class ClampCommunicationMode {
  TtCanopen,
  MwdRs485,
  MwdCan,
  ZdtCan,
};

enum class ZdtFirmware { X, Emm };

struct ClampMotorSettings {
  ClampCommunicationMode communicationMode = ClampCommunicationMode::TtCanopen;
  QString serialPort;
  int serialBaudRate = 115200;
  int canBitrate = 500000;
  int motorId = 1;
  int sign = 1;
  ZdtFirmware zdtFirmware = ZdtFirmware::X;
  int zdtMaximumSpeedRpm = 100;
  int zdtAcceleration = 1000; // RPM/s for X; 0..255 for Emm.

  bool operator==(const ClampMotorSettings& other) const;
};

using ClampMotorAxes = std::array<QVector<ClampMotorSettings>, 3>;
QString clampModeKey(ClampCommunicationMode mode);
ClampCommunicationMode clampModeFromKey(const QString& key);
QString clampModeName(ClampCommunicationMode mode);

struct DriveSettings {
  static constexpr int kMinimumFeedbackTimeoutMs = 800;
  static constexpr int kCurrentSettingsSchemaVersion = 11;
  static constexpr int kMaximumClampMotorsPerAxis = 16;

  // Legacy shared-port fields are retained for loading older settings files.
  QString serialPort;
  int serialBaudRate = 115200;
  int canBitrate = 1000000;

  QString imuSerialPort;
  int imuBaudRate = 115200;
  int imuOutputDivider = 10;
  QString laserSerialNumber;
  int usbCameraDeviceIndex = -1;
  QString networkCameraUrl;
  int usbCameraFps = 30;
  bool usbCameraAutoConnect = true;
  bool usbCameraFlipHorizontal = false;
  bool usbCameraFlipVertical = false;
  bool autoConnectOnStartup = true;
  int manualJogPercent = 30;

  ClampCommunicationMode clampCommunicationMode =
      ClampCommunicationMode::TtCanopen;
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

  ClampMotorAxes clampMotors = [] {
    ClampMotorAxes axes;
    for (int axis = 0; axis < 3; ++axis) {
      ClampMotorSettings motor;
      motor.motorId = axis + 1;
      axes[axis].append(motor);
    }
    return axes;
  }();

  WheelCommunicationMode wheelCommunicationMode =
      WheelCommunicationMode::Can;
  QString wheelCanSerialPort;
  int wheelCanSerialBaudRate = 115200;
  int wheelCanBitrate = 1000000;

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
  double motorOutputToWheelRatio = 1.0;

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
  QString clampValidationError() const;
  static QString persistentFilePath();
  void save(QSettings& settings) const;
  static DriveSettings load(QSettings& settings);
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::DriveSettings)
