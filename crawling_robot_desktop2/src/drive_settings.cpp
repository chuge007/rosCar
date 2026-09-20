#include "drive_settings.h"
#include "utf8_compat.h"

#include <QSettings>
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <algorithm>

namespace crawling {
namespace {

bool positiveFinite(double value) {
  return value > 0.0 && std::isfinite(value);
}

// Values below these limits are normally caused by an old configuration that
// stored the UI unit (mm/deg) as the internal SI unit.  Keeping them would make
// a manual turn effectively invisible for several seconds.
constexpr double kMinimumPracticalTrackWidthM = 0.050;
constexpr double kMinimumPracticalLinearAccelerationMps2 = 0.050;
constexpr double kMinimumPracticalAngularAccelerationRadps2 = 0.10;
constexpr double kInstalledMotorOutputToWheelRatio = 36.0;

}  // namespace

QString DriveSettings::persistentFilePath() {
  const QDir base(QCoreApplication::applicationDirPath());
  base.mkpath(CRAWLING_TEXT("config"));
  return base.filePath(CRAWLING_TEXT("config/drive_settings.ini"));
}

QString DriveSettings::validationError() const {
  if (leftMotorId < 1 || leftMotorId > 32 || rightMotorId < 1 || rightMotorId > 32) {
    return QStringLiteral("Motor RS485 IDs must be in the range 1..32.");
  }
  if (!leftMotorSerialPort.trimmed().isEmpty() && !rightMotorSerialPort.trimmed().isEmpty() &&
      leftMotorSerialPort.trimmed().compare(rightMotorSerialPort.trimmed(), Qt::CaseInsensitive) == 0) {
    if (leftMotorId == rightMotorId) {
      return QStringLiteral("Two motors cannot use the same RS485 ID on the same port.");
    }
    if (leftMotorBaudRate != rightMotorBaudRate) {
      return QStringLiteral("Motors sharing one RS485 port must use the same baud rate.");
    }
  }
  if (leftMotorBaudRate <= 0 || rightMotorBaudRate <= 0) {
    return QStringLiteral("Motor RS485 baud rates must be positive.");
  }
  if ((leftMotorSign != -1 && leftMotorSign != 1) ||
      (rightMotorSign != -1 && rightMotorSign != 1)) {
    return CRAWLING_TEXT("\xE6\xAF\x8F""\xE4\xB8\xAA""\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\x96\xB9""\xE5\x90\x91""\xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\xBA"" +1 \xE6\x88\x96"" -1\xE3\x80\x82""");
  }
  if (clampNodeId < 0 || clampNodeId > 127) {
    return CRAWLING_TEXT("\xE5\xA4\xB9""\xE5\xAD\x90"" CANopen \xE8\x8A\x82""\xE7\x82\xB9"" ID \xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\xBA"" 0 \xE5\x88\xB0"" 127\xEF\xBC\x9B""0 \xE8\xA1\xA8""\xE7\xA4\xBA""\xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0""\xE5\xA4\xB9""\xE5\xAD\x90""\xE3\x80\x82""");
  }
  if (!positiveFinite(wheelRadiusM) || !positiveFinite(trackWidthM) ||
      !positiveFinite(motorOutputToWheelRatio)) {
    return CRAWLING_TEXT("\xE8\xBD\xAE""\xE5\x8D\x8A""\xE5\xBE\x84""\xE3\x80\x81""\xE8\xBD\xAE""\xE8\xB7\x9D""\xE5\x92\x8C""\xE4\xBC\xA0""\xE5\x8A\xA8""\xE6\xAF\x94""\xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\xBA""\xE6\xAD\xA3""\xE6\x95\xB0""\xE3\x80\x82""");
  }
  if (!positiveFinite(maximumWheelSpeedMps) || !positiveFinite(maximumLinearSpeedMps) ||
      !positiveFinite(maximumAngularSpeedRadps) ||
      !positiveFinite(maximumLinearAccelerationMps2) ||
      !positiveFinite(maximumAngularAccelerationRadps2)) {
    return CRAWLING_TEXT("\xE6\x89\x80""\xE6\x9C\x89""\xE9\x80\x9F""\xE5\xBA\xA6""\xE5\x92\x8C""\xE5\x8A\xA0""\xE9\x80\x9F""\xE5\xBA\xA6""\xE9\x99\x90""\xE5\x88\xB6""\xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\xBA""\xE6\xAD\xA3""\xE6\x95\xB0""\xE3\x80\x82""");
  }
  if (!std::isfinite(minimumInnerWheelRatio) || minimumInnerWheelRatio < 0.0 ||
      minimumInnerWheelRatio > 1.0) {
    return CRAWLING_TEXT("\xE5\x86\x85""\xE4\xBE\xA7""\xE8\xBD\xAE""\xE6\x9C\x80""\xE5\xB0\x8F""\xE6\xAF\x94""\xE4\xBE\x8B""\xE5\xBF\x85""\xE9\xA1\xBB""\xE5\x9C\xA8"" 0 \xE5\x88\xB0"" 1 \xE4\xB9\x8B""\xE9\x97\xB4""\xE3\x80\x82""");
  }
  if (commandTimeoutMs < 100 ||
      feedbackTimeoutMs < DriveSettings::kMinimumFeedbackTimeoutMs ||
      armingTimeoutMs < feedbackTimeoutMs) {
    return CRAWLING_TEXT("\xE5\xAE\x89""\xE5\x85\xA8""\xE7\x9C\x8B""\xE9\x97\xA8""\xE7\x8B\x97""\xE8\xB6\x85""\xE6\x97\xB6""\xE6\x97\xB6""\xE9\x97\xB4""\xE6\x97\xA0""\xE6\x95\x88""\xE3\x80\x82""");
  }
  if (!positiveFinite(synchronizer.maximumCorrectionMps) ||
      !positiveFinite(synchronizer.minimumControlledSpeedMps) ||
      synchronizer.proportionalGain < 0.0 || synchronizer.integralGain < 0.0) {
    return CRAWLING_TEXT("\xE5\x90\x8C""\xE6\xAD\xA5""\xE6\x8E\xA7""\xE5\x88\xB6""\xE5\x99\xA8""\xE5\x8F\x82""\xE6\x95\xB0""\xE6\x97\xA0""\xE6\x95\x88""\xE3\x80\x82""");
  }
  return {};
}

void DriveSettings::save(QSettings& settings) const {
  settings.beginGroup(CRAWLING_TEXT("drive"));
  settings.setValue(CRAWLING_TEXT("settingsSchemaVersion"),
                    kCurrentSettingsSchemaVersion);
  settings.setValue(CRAWLING_TEXT("serialPort"), serialPort);
  settings.setValue(CRAWLING_TEXT("serialBaudRate"), serialBaudRate);
  settings.setValue(CRAWLING_TEXT("canBitrate"), canBitrate);
  settings.setValue(CRAWLING_TEXT("imuSerialPort"), imuSerialPort);
  settings.setValue(CRAWLING_TEXT("imuBaudRate"), imuBaudRate);
  settings.setValue(CRAWLING_TEXT("imuOutputDivider"), imuOutputDivider);
  settings.setValue(CRAWLING_TEXT("laserSerialNumber"), laserSerialNumber);
  settings.setValue(CRAWLING_TEXT("autoDetectPhysicalInterfaces"),
                    autoDetectPhysicalInterfaces);
  settings.setValue(CRAWLING_TEXT("manualJogPercent"), manualJogPercent);
  settings.setValue(CRAWLING_TEXT("clampSerialPort"), clampSerialPort);
  settings.setValue(CRAWLING_TEXT("clampSerialBaudRate"), clampSerialBaudRate);
  settings.setValue(CRAWLING_TEXT("clampCanBitrate"), clampCanBitrate);
  settings.setValue(CRAWLING_TEXT("clampNodeId"), clampNodeId);
  settings.setValue(CRAWLING_TEXT("leftMotorId"), leftMotorId);
  settings.setValue(CRAWLING_TEXT("rightMotorId"), rightMotorId);
  settings.setValue(CRAWLING_TEXT("leftMotorSerialPort"), leftMotorSerialPort);
  settings.setValue(CRAWLING_TEXT("leftMotorBaudRate"), leftMotorBaudRate);
  settings.setValue(CRAWLING_TEXT("rightMotorSerialPort"), rightMotorSerialPort);
  settings.setValue(CRAWLING_TEXT("rightMotorBaudRate"), rightMotorBaudRate);
  settings.setValue(CRAWLING_TEXT("leftMotorSign"), leftMotorSign);
  settings.setValue(CRAWLING_TEXT("rightMotorSign"), rightMotorSign);
  settings.setValue(CRAWLING_TEXT("wheelRadiusM"), wheelRadiusM);
  settings.setValue(CRAWLING_TEXT("trackWidthM"), trackWidthM);
  settings.setValue(CRAWLING_TEXT("motorOutputToWheelRatio"), motorOutputToWheelRatio);
  settings.setValue(CRAWLING_TEXT("maximumWheelSpeedMps"), maximumWheelSpeedMps);
  settings.setValue(CRAWLING_TEXT("maximumLinearSpeedMps"), maximumLinearSpeedMps);
  settings.setValue(CRAWLING_TEXT("maximumAngularSpeedRadps"), maximumAngularSpeedRadps);
  settings.setValue(CRAWLING_TEXT("maximumLinearAccelerationMps2"), maximumLinearAccelerationMps2);
  settings.setValue(CRAWLING_TEXT("maximumAngularAccelerationRadps2"), maximumAngularAccelerationRadps2);
  settings.setValue(CRAWLING_TEXT("minimumInnerWheelRatio"), minimumInnerWheelRatio);
  settings.setValue(CRAWLING_TEXT("commandTimeoutMs"), commandTimeoutMs);
  settings.setValue(CRAWLING_TEXT("feedbackTimeoutMs"), feedbackTimeoutMs);
  settings.setValue(CRAWLING_TEXT("armingTimeoutMs"), armingTimeoutMs);
  settings.setValue(CRAWLING_TEXT("synchronizerP"), synchronizer.proportionalGain);
  settings.setValue(CRAWLING_TEXT("synchronizerI"), synchronizer.integralGain);
  settings.setValue(CRAWLING_TEXT("synchronizerMaxCorrection"), synchronizer.maximumCorrectionMps);
  settings.setValue(CRAWLING_TEXT("synchronizerMinSpeed"), synchronizer.minimumControlledSpeedMps);
  settings.endGroup();
}

DriveSettings DriveSettings::load(QSettings& settings) {
  DriveSettings value;
  const DriveSettings defaults;
  settings.beginGroup(CRAWLING_TEXT("drive"));
  const int settingsSchemaVersion =
      settings.value(CRAWLING_TEXT("settingsSchemaVersion"), 0).toInt();
  const bool hasStoredMotorOutputToWheelRatio =
      settings.contains(CRAWLING_TEXT("motorOutputToWheelRatio"));
  value.serialPort = settings.value(CRAWLING_TEXT("serialPort"), value.serialPort).toString();
  value.serialBaudRate = settings.value(CRAWLING_TEXT("serialBaudRate"), value.serialBaudRate).toInt();
  value.canBitrate = settings.value(CRAWLING_TEXT("canBitrate"), value.canBitrate).toInt();
  value.imuSerialPort = settings.value(CRAWLING_TEXT("imuSerialPort"), value.imuSerialPort).toString();
  value.imuBaudRate = settings.value(CRAWLING_TEXT("imuBaudRate"), value.imuBaudRate).toInt();
  value.imuOutputDivider = std::clamp(
      settings.value(CRAWLING_TEXT("imuOutputDivider"),
                     value.imuOutputDivider).toInt(),
      1, 200);
  value.laserSerialNumber = settings.value(CRAWLING_TEXT("laserSerialNumber"), value.laserSerialNumber).toString();
  value.autoDetectPhysicalInterfaces =
      settings.value(CRAWLING_TEXT("autoDetectPhysicalInterfaces"),
                     value.autoDetectPhysicalInterfaces).toBool();
  value.manualJogPercent = std::clamp(
      settings.value(CRAWLING_TEXT("manualJogPercent"),
                     value.manualJogPercent).toInt(),
      5, 100);
  value.clampSerialPort = settings.value(CRAWLING_TEXT("clampSerialPort"), value.clampSerialPort).toString();
  value.clampSerialBaudRate = settings.value(CRAWLING_TEXT("clampSerialBaudRate"), value.clampSerialBaudRate).toInt();
  value.clampCanBitrate = settings.value(CRAWLING_TEXT("clampCanBitrate"), value.clampCanBitrate).toInt();
  value.clampNodeId = settings.value(CRAWLING_TEXT("clampNodeId"), value.clampNodeId).toInt();
  value.leftMotorId = settings.value(CRAWLING_TEXT("leftMotorId"), value.leftMotorId).toInt();
  value.rightMotorId = settings.value(CRAWLING_TEXT("rightMotorId"), value.rightMotorId).toInt();
  value.leftMotorSerialPort = settings.value(CRAWLING_TEXT("leftMotorSerialPort"), QString()).toString();
  value.leftMotorBaudRate = settings.value(CRAWLING_TEXT("leftMotorBaudRate"), value.leftMotorBaudRate).toInt();
  value.rightMotorSerialPort = settings.value(CRAWLING_TEXT("rightMotorSerialPort"), QString()).toString();
  value.rightMotorBaudRate = settings.value(CRAWLING_TEXT("rightMotorBaudRate"), value.rightMotorBaudRate).toInt();
  value.leftMotorSign = settings.value(CRAWLING_TEXT("leftMotorSign"), value.leftMotorSign).toInt();
  value.rightMotorSign = settings.value(CRAWLING_TEXT("rightMotorSign"), value.rightMotorSign).toInt();
  value.wheelRadiusM = settings.value(CRAWLING_TEXT("wheelRadiusM"), value.wheelRadiusM).toDouble();
  value.trackWidthM = settings.value(CRAWLING_TEXT("trackWidthM"), value.trackWidthM).toDouble();
  value.motorOutputToWheelRatio = settings.value(CRAWLING_TEXT("motorOutputToWheelRatio"), value.motorOutputToWheelRatio).toDouble();
  value.maximumWheelSpeedMps = settings.value(CRAWLING_TEXT("maximumWheelSpeedMps"), value.maximumWheelSpeedMps).toDouble();
  value.maximumLinearSpeedMps = settings.value(CRAWLING_TEXT("maximumLinearSpeedMps"), value.maximumLinearSpeedMps).toDouble();
  value.maximumAngularSpeedRadps = settings.value(CRAWLING_TEXT("maximumAngularSpeedRadps"), value.maximumAngularSpeedRadps).toDouble();
  value.maximumLinearAccelerationMps2 = settings.value(CRAWLING_TEXT("maximumLinearAccelerationMps2"), value.maximumLinearAccelerationMps2).toDouble();
  value.maximumAngularAccelerationRadps2 = settings.value(CRAWLING_TEXT("maximumAngularAccelerationRadps2"), value.maximumAngularAccelerationRadps2).toDouble();
  value.minimumInnerWheelRatio = settings.value(CRAWLING_TEXT("minimumInnerWheelRatio"), value.minimumInnerWheelRatio).toDouble();
  value.commandTimeoutMs = settings.value(CRAWLING_TEXT("commandTimeoutMs"), value.commandTimeoutMs).toInt();
  // Three cyclic exchanges per second need enough margin for one missed
  // reply without declaring both motors disconnected.
  value.feedbackTimeoutMs = std::max(
      DriveSettings::kMinimumFeedbackTimeoutMs,
      settings.value(CRAWLING_TEXT("feedbackTimeoutMs"),
                     value.feedbackTimeoutMs).toInt());
  value.armingTimeoutMs = std::max(
      value.feedbackTimeoutMs,
      settings.value(CRAWLING_TEXT("armingTimeoutMs"),
                     value.armingTimeoutMs).toInt());
  value.synchronizer.proportionalGain = settings.value(CRAWLING_TEXT("synchronizerP"), value.synchronizer.proportionalGain).toDouble();
  value.synchronizer.integralGain = settings.value(CRAWLING_TEXT("synchronizerI"), value.synchronizer.integralGain).toDouble();
  value.synchronizer.maximumCorrectionMps = settings.value(CRAWLING_TEXT("synchronizerMaxCorrection"), value.synchronizer.maximumCorrectionMps).toDouble();
  value.synchronizer.minimumControlledSpeedMps = settings.value(CRAWLING_TEXT("synchronizerMinSpeed"), value.synchronizer.minimumControlledSpeedMps).toDouble();
  settings.endGroup();

  // Schema 1 used 1.0 as an incorrect placeholder for the installed 100:1
  // gearboxes. MainWindow saves the migrated value immediately after loading,
  // so an explicit 1.0 saved by schema 2 or later remains a valid custom value.
  if (settingsSchemaVersion < 2 &&
      hasStoredMotorOutputToWheelRatio &&
      std::abs(value.motorOutputToWheelRatio - 1.0) <= 1e-9) {
    value.motorOutputToWheelRatio = defaults.motorOutputToWheelRatio;
  }

  // Schema 3 replaced the small PI trim with adaptive one-sided throttling.
  // The former 0.030 m/s default cannot correct the observed 3:1 drive
  // mismatch, so migrate it to the new 0.300 m/s throttle allowance.
  if (settingsSchemaVersion < 3) {
    value.synchronizer.maximumCorrectionMps =
        defaults.synchronizer.maximumCorrectionMps;
  }

  // Schema 4 records the installed gearbox explicitly. Older builds shipped
  // 100:1 as a placeholder and some schema-3 profiles were saved as 1:1;
  // both values produce an incorrect wheel speed for the installed 36:1
  // motor/axle assembly. Preserve other custom ratios.
  if (settingsSchemaVersion < 4 &&
      (std::abs(value.motorOutputToWheelRatio - 100.0) <= 1e-9 ||
       std::abs(value.motorOutputToWheelRatio - 1.0) <= 1e-9)) {
    value.motorOutputToWheelRatio = kInstalledMotorOutputToWheelRatio;
  }

  // Migrate the former shared motor bus configuration to both wheel ports.
  if (value.leftMotorSerialPort.trimmed().isEmpty()) {
    value.leftMotorSerialPort = value.serialPort;
  }
  if (value.rightMotorSerialPort.trimmed().isEmpty()) {
    value.rightMotorSerialPort = value.serialPort;
  }
  if (value.leftMotorBaudRate <= 0) {
    value.leftMotorBaudRate = value.serialBaudRate;
  }
  if (value.rightMotorBaudRate <= 0) {
    value.rightMotorBaudRate = value.serialBaudRate;
  }

  // A 10 mm track width with 40 mm wheels, or old UI-unit acceleration values,
  // produces almost no wheel-speed change when a direction button is pressed.
  // Migrate these stale values to the tested defaults so short commands remain
  // responsive after an upgrade.
  const double minimumTrackWidth = std::max(
      kMinimumPracticalTrackWidthM,
      positiveFinite(value.wheelRadiusM) ? value.wheelRadiusM * 1.5 : 0.0);
  if (!positiveFinite(value.trackWidthM) || value.trackWidthM < minimumTrackWidth) {
    value.trackWidthM = defaults.trackWidthM;
  }
  if (!positiveFinite(value.maximumLinearAccelerationMps2) ||
      value.maximumLinearAccelerationMps2 < kMinimumPracticalLinearAccelerationMps2) {
    value.maximumLinearAccelerationMps2 = defaults.maximumLinearAccelerationMps2;
  }
  if (!positiveFinite(value.maximumAngularAccelerationRadps2) ||
      value.maximumAngularAccelerationRadps2 < kMinimumPracticalAngularAccelerationRadps2) {
    value.maximumAngularAccelerationRadps2 = defaults.maximumAngularAccelerationRadps2;
  }
  return value;
}

}  // namespace crawling
