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

}  // namespace

QString DriveSettings::persistentFilePath() {
  const QDir base(QCoreApplication::applicationDirPath());
  base.mkpath(CRAWLING_TEXT("config"));
  return base.filePath(CRAWLING_TEXT("config/drive_settings.ini"));
}

QString DriveSettings::validationError() const {
  if (leftMotorId < 1 || leftMotorId > 32 || rightMotorId < 1 || rightMotorId > 32 ||
      leftMotorId == rightMotorId) {
    return CRAWLING_TEXT("\xE4\xB8\xA4""\xE4\xB8\xAA""\xE7\x94\xB5""\xE6\x9C\xBA"" CAN ID \xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\x8D""\xE5\x90\x8C""\xEF\xBC\x8C""\xE4\xB8\x94""\xE8\x8C\x83""\xE5\x9B\xB4""\xE4\xB8\xBA"" 1 \xE5\x88\xB0"" 32\xE3\x80\x82""");
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
  if (commandTimeoutMs < 100 || feedbackTimeoutMs < 50 || armingTimeoutMs < feedbackTimeoutMs) {
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
  settings.setValue(CRAWLING_TEXT("serialPort"), serialPort);
  settings.setValue(CRAWLING_TEXT("serialBaudRate"), serialBaudRate);
  settings.setValue(CRAWLING_TEXT("canBitrate"), canBitrate);
  settings.setValue(CRAWLING_TEXT("imuSerialPort"), imuSerialPort);
  settings.setValue(CRAWLING_TEXT("imuBaudRate"), imuBaudRate);
  settings.setValue(CRAWLING_TEXT("laserSerialNumber"), laserSerialNumber);
  settings.setValue(CRAWLING_TEXT("clampSerialPort"), clampSerialPort);
  settings.setValue(CRAWLING_TEXT("clampSerialBaudRate"), clampSerialBaudRate);
  settings.setValue(CRAWLING_TEXT("clampCanBitrate"), clampCanBitrate);
  settings.setValue(CRAWLING_TEXT("clampNodeId"), clampNodeId);
  settings.setValue(CRAWLING_TEXT("leftMotorId"), leftMotorId);
  settings.setValue(CRAWLING_TEXT("rightMotorId"), rightMotorId);
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
  value.serialPort = settings.value(CRAWLING_TEXT("serialPort"), value.serialPort).toString();
  value.serialBaudRate = settings.value(CRAWLING_TEXT("serialBaudRate"), value.serialBaudRate).toInt();
  value.canBitrate = settings.value(CRAWLING_TEXT("canBitrate"), value.canBitrate).toInt();
  value.imuSerialPort = settings.value(CRAWLING_TEXT("imuSerialPort"), value.imuSerialPort).toString();
  value.imuBaudRate = settings.value(CRAWLING_TEXT("imuBaudRate"), value.imuBaudRate).toInt();
  value.laserSerialNumber = settings.value(CRAWLING_TEXT("laserSerialNumber"), value.laserSerialNumber).toString();
  value.clampSerialPort = settings.value(CRAWLING_TEXT("clampSerialPort"), value.clampSerialPort).toString();
  value.clampSerialBaudRate = settings.value(CRAWLING_TEXT("clampSerialBaudRate"), value.clampSerialBaudRate).toInt();
  value.clampCanBitrate = settings.value(CRAWLING_TEXT("clampCanBitrate"), value.clampCanBitrate).toInt();
  value.clampNodeId = settings.value(CRAWLING_TEXT("clampNodeId"), value.clampNodeId).toInt();
  value.leftMotorId = settings.value(CRAWLING_TEXT("leftMotorId"), value.leftMotorId).toInt();
  value.rightMotorId = settings.value(CRAWLING_TEXT("rightMotorId"), value.rightMotorId).toInt();
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
  // Older config files used 150 ms, which is shorter than the actual
  // adapter/motor reply interval and caused a false safety fault.
  value.feedbackTimeoutMs = std::max(500, settings.value(CRAWLING_TEXT("feedbackTimeoutMs"), value.feedbackTimeoutMs).toInt());
  value.armingTimeoutMs = settings.value(CRAWLING_TEXT("armingTimeoutMs"), value.armingTimeoutMs).toInt();
  value.synchronizer.proportionalGain = settings.value(CRAWLING_TEXT("synchronizerP"), value.synchronizer.proportionalGain).toDouble();
  value.synchronizer.integralGain = settings.value(CRAWLING_TEXT("synchronizerI"), value.synchronizer.integralGain).toDouble();
  value.synchronizer.maximumCorrectionMps = settings.value(CRAWLING_TEXT("synchronizerMaxCorrection"), value.synchronizer.maximumCorrectionMps).toDouble();
  value.synchronizer.minimumControlledSpeedMps = settings.value(CRAWLING_TEXT("synchronizerMinSpeed"), value.synchronizer.minimumControlledSpeedMps).toDouble();
  settings.endGroup();

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
