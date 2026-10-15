#include "drive_settings.h"
#include "utf8_compat.h"

#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QVector>

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

bool isSupportedMwdRs485BaudRate(int baudRate) {
  return baudRate == 115200 || baudRate == 500000 ||
      baudRate == 1000000 || baudRate == 1500000 || baudRate == 2500000;
}

bool isSupportedClampMwdRs485BaudRate(int baudRate) {
  return baudRate == 9600 || baudRate == 19200 || baudRate == 38400 ||
      baudRate == 57600 || baudRate == 115200 || baudRate == 230400 ||
      baudRate == 460800 || baudRate == 1000000 || baudRate == 2000000;
}

bool isSupportedCanBitrate(int bitrate) {
  return bitrate == 125000 || bitrate == 250000 || bitrate == 500000 ||
      bitrate == 800000 || bitrate == 1000000;
}

bool isSupportedMwdCanBitrate(int bitrate) {
  return bitrate == 100000 || isSupportedCanBitrate(bitrate);
}

bool approximately(double actual, double expected) {
  return std::abs(actual - expected) <= 1e-9;
}

bool isLegacyDefaultMotionProfile(const DriveSettings& value) {
  const bool measuredChassis =
      approximately(value.wheelRadiusM, 0.07953) &&
      approximately(value.trackWidthM, 0.1473) &&
      approximately(value.motorOutputToWheelRatio, 36.0);
  const bool deployedDefaults =
      approximately(value.maximumWheelSpeedMps, 0.30) &&
      approximately(value.maximumLinearSpeedMps, 0.30) &&
      approximately(value.maximumAngularSpeedRadps, 0.8726646259971648) &&
      approximately(value.maximumLinearAccelerationMps2, 0.50) &&
      approximately(value.maximumAngularAccelerationRadps2, 0.17453292519943295) &&
      approximately(value.minimumInnerWheelRatio, 0.50) &&
      approximately(value.synchronizer.proportionalGain, 0.30) &&
      approximately(value.synchronizer.integralGain, 0.08) &&
      value.synchronizer.maximumCorrectionMps >= 0.294 &&
      value.synchronizer.maximumCorrectionMps <= 0.300 &&
      approximately(value.synchronizer.minimumControlledSpeedMps, 0.015);
  return measuredChassis && deployedDefaults;
}

}  // namespace

bool ClampMotorSettings::operator==(const ClampMotorSettings& other) const {
  return communicationMode == other.communicationMode &&
      serialPort.trimmed().compare(other.serialPort.trimmed(), Qt::CaseInsensitive) == 0 &&
      serialBaudRate == other.serialBaudRate && canBitrate == other.canBitrate &&
      motorId == other.motorId && sign == other.sign &&
      zdtFirmware == other.zdtFirmware &&
      zdtMaximumSpeedRpm == other.zdtMaximumSpeedRpm &&
      zdtAcceleration == other.zdtAcceleration;
}

QString clampModeKey(ClampCommunicationMode mode) {
  switch (mode) {
    case ClampCommunicationMode::TtCanopen: return QStringLiteral("tt_canopen");
    case ClampCommunicationMode::MwdRs485: return QStringLiteral("mwd_rs485");
    case ClampCommunicationMode::MwdCan: return QStringLiteral("mwd_can");
    case ClampCommunicationMode::ZdtCan: return QStringLiteral("zdt_can");
  }
  return {};
}

ClampCommunicationMode clampModeFromKey(const QString& key) {
  const QString mode = key.trimmed().toLower();
  if (mode == QStringLiteral("mwd_rs485") || mode == QStringLiteral("rs485"))
    return ClampCommunicationMode::MwdRs485;
  if (mode == QStringLiteral("mwd_can")) return ClampCommunicationMode::MwdCan;
  if (mode == QStringLiteral("zdt_can")) return ClampCommunicationMode::ZdtCan;
  return ClampCommunicationMode::TtCanopen;
}

QString clampModeName(ClampCommunicationMode mode) {
  switch (mode) {
    case ClampCommunicationMode::TtCanopen: return QStringLiteral("TT CANopen");
    case ClampCommunicationMode::MwdRs485: return QStringLiteral("MWD RS485");
    case ClampCommunicationMode::MwdCan: return QStringLiteral("MWD CAN");
    case ClampCommunicationMode::ZdtCan: return QStringLiteral("ZDT CAN");
  }
  return {};
}

QString DriveSettings::clampValidationError() const {
  QVector<ClampMotorSettings> configured;
  const char* names[] = {"X", "Y", "Z"};
  for (int axis = 0; axis < 3; ++axis) {
    if (clampMotors[axis].size() > kMaximumClampMotorsPerAxis)
      return QStringLiteral("夹子 %1 轴最多配置 %2 台电机。")
          .arg(QString::fromLatin1(names[axis])).arg(kMaximumClampMotorsPerAxis);
    for (int index = 0; index < clampMotors[axis].size(); ++index) {
      const auto& motor = clampMotors[axis][index];
      const QString label = QStringLiteral("夹子 %1%2：")
          .arg(QString::fromLatin1(names[axis])).arg(index + 1);
      const bool rs485 = motor.communicationMode == ClampCommunicationMode::MwdRs485;
      const bool zdt = motor.communicationMode == ClampCommunicationMode::ZdtCan;
      const int maximumId = zdt ? 255 :
          motor.communicationMode == ClampCommunicationMode::TtCanopen ? 127 : 32;
      if (motor.motorId < 1 || motor.motorId > maximumId)
        return label + QStringLiteral("电机 ID 必须为 1..%1。").arg(maximumId);
      if (motor.sign != 1 && motor.sign != -1)
        return label + QStringLiteral("方向必须为 +1 或 -1。");
      if (motor.serialBaudRate <= 0 ||
          (rs485 && !isSupportedClampMwdRs485BaudRate(motor.serialBaudRate)))
        return label + QStringLiteral("串口波特率无效。");
      const bool validCanRate =
          motor.communicationMode == ClampCommunicationMode::TtCanopen
              ? isSupportedCanBitrate(motor.canBitrate)
              : isSupportedMwdCanBitrate(motor.canBitrate) ||
                  (zdt && (motor.canBitrate == 10000 || motor.canBitrate == 20000 ||
                           motor.canBitrate == 50000));
      if (!rs485 && !validCanRate)
        return label + QStringLiteral("所选 CAN 波特率不受支持。");
      if (zdt && (motor.zdtMaximumSpeedRpm < 1 || motor.zdtMaximumSpeedRpm > 3000 ||
                  motor.zdtAcceleration < 0 ||
                  motor.zdtAcceleration > (motor.zdtFirmware == ZdtFirmware::X ? 65535 : 255)))
        return label + QStringLiteral("ZDT 速度或加速度超出所选固件范围。");
      if (motor.serialPort.trimmed().isEmpty())
        continue;
      for (const auto& other : configured) {
        if (motor.serialPort.trimmed().compare(other.serialPort.trimmed(), Qt::CaseInsensitive) != 0)
          continue;
        const bool otherRs485 = other.communicationMode == ClampCommunicationMode::MwdRs485;
        if (rs485 != otherRs485 || motor.serialBaudRate != other.serialBaudRate ||
            (!rs485 && motor.canBitrate != other.canBitrate))
          return label + QStringLiteral("共用串口的电机必须使用相同总线类型及波特率。");
        // TT and MWD use standard identifiers with potentially overlapping ranges.
        if (!rs485 && !zdt && other.communicationMode != ClampCommunicationMode::ZdtCan &&
            motor.communicationMode != other.communicationMode)
          return label + QStringLiteral("TT CANopen 和 MWD CAN 请使用不同适配器端口。");
        const bool sameAddressSpace = zdt ==
            (other.communicationMode == ClampCommunicationMode::ZdtCan);
        if (sameAddressSpace && motor.motorId == other.motorId)
          return label + QStringLiteral("同一总线上存在重复的电机 ID。");
      }
      configured.append(motor);
    }
  }
  return {};
}

QString DriveSettings::persistentFilePath() {
  const QDir base(QCoreApplication::applicationDirPath());
  base.mkpath(CRAWLING_TEXT("config"));
  return base.filePath(CRAWLING_TEXT("config/drive_settings.ini"));
}

QString DriveSettings::validationError() const {
  if (wheelCommunicationMode == WheelCommunicationMode::Can) {
    if (wheelCanSerialPort.trimmed().isEmpty()) {
      return QStringLiteral("Select the SLCAN serial port for wheel CAN.");
    }
    if (wheelCanSerialBaudRate <= 0) {
      return QStringLiteral("Wheel CAN adapter baud rate must be positive.");
    }
    if (!isSupportedCanBitrate(wheelCanBitrate)) {
      return QStringLiteral(
          "Wheel CAN bitrate must be 125000, 250000, 500000, 800000, or 1000000.");
    }
  }
  if (leftMotorId < 1 || leftMotorId > 32 || rightMotorId < 1 || rightMotorId > 32) {
    return QStringLiteral("Motor IDs must be in the range 1..32.");
  }
  if (wheelCommunicationMode == WheelCommunicationMode::Rs485 &&
      !leftMotorSerialPort.trimmed().isEmpty() && !rightMotorSerialPort.trimmed().isEmpty() &&
      leftMotorSerialPort.trimmed().compare(rightMotorSerialPort.trimmed(), Qt::CaseInsensitive) == 0) {
    if (leftMotorId == rightMotorId) {
      return QStringLiteral("Two motors cannot use the same ID on the same port.");
    }
    if (leftMotorBaudRate != rightMotorBaudRate) {
      return QStringLiteral("Motors sharing one RS485 port must use the same baud rate.");
    }
  }
  if (wheelCommunicationMode == WheelCommunicationMode::Rs485 &&
      (leftMotorBaudRate <= 0 || rightMotorBaudRate <= 0)) {
    return QStringLiteral("Motor RS485 baud rates must be positive.");
  }
  if (wheelCommunicationMode == WheelCommunicationMode::Rs485 &&
      (!isSupportedMwdRs485BaudRate(leftMotorBaudRate) ||
       !isSupportedMwdRs485BaudRate(rightMotorBaudRate))) {
    return QStringLiteral(
        "MWD V3.8 motor RS485 baud rates must be 115200, 500000, "
        "1000000, 1500000, or 2500000.");
  }
  if ((leftMotorSign != -1 && leftMotorSign != 1) ||
      (rightMotorSign != -1 && rightMotorSign != 1)) {
    return CRAWLING_TEXT("\xE6\xAF\x8F""\xE4\xB8\xAA""\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\x96\xB9""\xE5\x90\x91""\xE5\xBF\x85""\xE9\xA1\xBB""\xE4\xB8\xBA"" +1 \xE6\x88\x96"" -1\xE3\x80\x82""");
  }
  const QString clampError = clampValidationError();
  if (!clampError.isEmpty())
    return clampError;
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
  settings.setValue(CRAWLING_TEXT("usbCameraDeviceIndex"), usbCameraDeviceIndex);
  settings.setValue(CRAWLING_TEXT("networkCameraUrl"), networkCameraUrl);
  settings.setValue(CRAWLING_TEXT("usbCameraFps"), usbCameraFps);
  settings.setValue(CRAWLING_TEXT("usbCameraAutoConnect"), usbCameraAutoConnect);
  settings.setValue(CRAWLING_TEXT("usbCameraFlipHorizontal"), usbCameraFlipHorizontal);
  settings.setValue(CRAWLING_TEXT("usbCameraFlipVertical"), usbCameraFlipVertical);
  settings.setValue(CRAWLING_TEXT("autoConnectOnStartup"),
                    autoConnectOnStartup);
  settings.setValue(CRAWLING_TEXT("manualJogPercent"), manualJogPercent);
  settings.setValue(QStringLiteral("clampAxesConfigured"), true);
  const char* axisKeys[] = {"clampXMotors", "clampYMotors", "clampZMotors"};
  for (int axis = 0; axis < 3; ++axis) {
    settings.remove(QString::fromLatin1(axisKeys[axis]));
    settings.beginWriteArray(QString::fromLatin1(axisKeys[axis]),
                             clampMotors[axis].size());
    for (int index = 0; index < clampMotors[axis].size(); ++index) {
      settings.setArrayIndex(index);
      const auto& motor = clampMotors[axis][index];
      settings.setValue(QStringLiteral("mode"), clampModeKey(motor.communicationMode));
      settings.setValue(QStringLiteral("serialPort"), motor.serialPort);
      settings.setValue(QStringLiteral("serialBaudRate"), motor.serialBaudRate);
      settings.setValue(QStringLiteral("canBitrate"), motor.canBitrate);
      settings.setValue(QStringLiteral("motorId"), motor.motorId);
      settings.setValue(QStringLiteral("sign"), motor.sign);
      settings.setValue(QStringLiteral("zdtFirmware"),
                        motor.zdtFirmware == ZdtFirmware::X ? "x" : "emm");
      settings.setValue(QStringLiteral("zdtMaximumSpeedRpm"), motor.zdtMaximumSpeedRpm);
      settings.setValue(QStringLiteral("zdtAcceleration"), motor.zdtAcceleration);
    }
    settings.endArray();
  }
  settings.setValue(
      CRAWLING_TEXT("wheelCommunicationMode"),
      wheelCommunicationMode == WheelCommunicationMode::Can
          ? CRAWLING_TEXT("can")
          : CRAWLING_TEXT("rs485"));
  settings.setValue(CRAWLING_TEXT("wheelCanSerialPort"), wheelCanSerialPort);
  settings.setValue(CRAWLING_TEXT("wheelCanSerialBaudRate"),
                    wheelCanSerialBaudRate);
  settings.setValue(CRAWLING_TEXT("wheelCanBitrate"), wheelCanBitrate);
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
  value.usbCameraDeviceIndex = settings.value(CRAWLING_TEXT("usbCameraDeviceIndex"), value.usbCameraDeviceIndex).toInt();
  value.networkCameraUrl = settings.value(CRAWLING_TEXT("networkCameraUrl"), value.networkCameraUrl).toString();
  value.usbCameraFps = std::clamp(settings.value(CRAWLING_TEXT("usbCameraFps"), value.usbCameraFps).toInt(), 1, 120);
  value.usbCameraAutoConnect = settings.value(CRAWLING_TEXT("usbCameraAutoConnect"), value.usbCameraAutoConnect).toBool();
  value.usbCameraFlipHorizontal = settings.value(CRAWLING_TEXT("usbCameraFlipHorizontal"), value.usbCameraFlipHorizontal).toBool();
  value.usbCameraFlipVertical = settings.value(CRAWLING_TEXT("usbCameraFlipVertical"), value.usbCameraFlipVertical).toBool();
  value.autoConnectOnStartup =
      settings.value(
          CRAWLING_TEXT("autoConnectOnStartup"),
          settings.value(CRAWLING_TEXT("autoDetectPhysicalInterfaces"),
                         value.autoConnectOnStartup))
          .toBool();
  value.manualJogPercent = std::clamp(
      settings.value(CRAWLING_TEXT("manualJogPercent"),
                     value.manualJogPercent).toInt(),
      5, 100);
  const QString clampMode = settings.value(
      CRAWLING_TEXT("clampCommunicationMode"), CRAWLING_TEXT("can"))
      .toString().trimmed().toLower();
  value.clampCommunicationMode = clampModeFromKey(clampMode);
  value.clampSerialPort = settings.value(CRAWLING_TEXT("clampSerialPort"), value.clampSerialPort).toString();
  value.clampSerialBaudRate = settings.value(CRAWLING_TEXT("clampSerialBaudRate"), value.clampSerialBaudRate).toInt();
  value.clampCanBitrate = settings.value(CRAWLING_TEXT("clampCanBitrate"), value.clampCanBitrate).toInt();
  value.clampNodeId = settings.value(CRAWLING_TEXT("clampNodeId"), value.clampNodeId).toInt();
  value.clampXMotorId = settings.value(CRAWLING_TEXT("clampXMotorId"), value.clampXMotorId).toInt();
  value.clampYMotorId = settings.value(CRAWLING_TEXT("clampYMotorId"), value.clampYMotorId).toInt();
  value.clampZMotorId = settings.value(CRAWLING_TEXT("clampZMotorId"), value.clampZMotorId).toInt();
  value.clampXMotorSign = settings.value(CRAWLING_TEXT("clampXMotorSign"), value.clampXMotorSign).toInt() < 0 ? -1 : 1;
  value.clampYMotorSign = settings.value(CRAWLING_TEXT("clampYMotorSign"), value.clampYMotorSign).toInt() < 0 ? -1 : 1;
  value.clampZMotorSign = settings.value(CRAWLING_TEXT("clampZMotorSign"), value.clampZMotorSign).toInt() < 0 ? -1 : 1;
  if (settings.value(QStringLiteral("clampAxesConfigured"), false).toBool()) {
    const char* axisKeys[] = {"clampXMotors", "clampYMotors", "clampZMotors"};
    for (int axis = 0; axis < 3; ++axis) {
      value.clampMotors[axis].clear();
      const int count = settings.beginReadArray(QString::fromLatin1(axisKeys[axis]));
      for (int index = 0; index < qMin(count, kMaximumClampMotorsPerAxis); ++index) {
        settings.setArrayIndex(index);
        ClampMotorSettings motor;
        motor.communicationMode = clampModeFromKey(
            settings.value(QStringLiteral("mode"), "tt_canopen").toString());
        motor.serialPort = settings.value(QStringLiteral("serialPort")).toString();
        motor.serialBaudRate = settings.value(QStringLiteral("serialBaudRate"), 115200).toInt();
        motor.canBitrate = settings.value(QStringLiteral("canBitrate"), 500000).toInt();
        motor.motorId = settings.value(QStringLiteral("motorId"), axis + 1).toInt();
        motor.sign = settings.value(QStringLiteral("sign"), 1).toInt();
        motor.zdtFirmware = settings.value(QStringLiteral("zdtFirmware"), "x").toString()
                == QStringLiteral("emm") ? ZdtFirmware::Emm : ZdtFirmware::X;
        motor.zdtMaximumSpeedRpm = settings.value(QStringLiteral("zdtMaximumSpeedRpm"), 100).toInt();
        motor.zdtAcceleration = settings.value(
            QStringLiteral("zdtAcceleration"), motor.zdtFirmware == ZdtFirmware::X ? 1000 : 10).toInt();
        value.clampMotors[axis].append(motor);
      }
      settings.endArray();
    }
  } else {
    const int ids[] = {value.clampXMotorId, value.clampYMotorId, value.clampZMotorId};
    const int signs[] = {value.clampXMotorSign, value.clampYMotorSign, value.clampZMotorSign};
    for (int axis = 0; axis < 3; ++axis) {
      ClampMotorSettings motor;
      motor.communicationMode = value.clampCommunicationMode;
      motor.serialPort = value.clampSerialPort;
      motor.serialBaudRate = value.clampSerialBaudRate;
      motor.canBitrate = value.clampCanBitrate;
      motor.motorId = ids[axis];
      motor.sign = signs[axis];
      value.clampMotors[axis] = {motor};
    }
  }
  const QString wheelMode = settings.value(
      CRAWLING_TEXT("wheelCommunicationMode"), CRAWLING_TEXT("can"))
      .toString().trimmed().toLower();
  value.wheelCommunicationMode =
      wheelMode == CRAWLING_TEXT("rs485")
          ? WheelCommunicationMode::Rs485
          : WheelCommunicationMode::Can;
  value.wheelCanSerialPort = settings.value(
      CRAWLING_TEXT("wheelCanSerialPort"), value.wheelCanSerialPort).toString();
  value.wheelCanSerialBaudRate = settings.value(
      CRAWLING_TEXT("wheelCanSerialBaudRate"),
      value.wheelCanSerialBaudRate).toInt();
  value.wheelCanBitrate = settings.value(
      CRAWLING_TEXT("wheelCanBitrate"), value.wheelCanBitrate).toInt();
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

  // Schema 3 replaced the small PI trim with adaptive one-sided throttling.
  // The former 0.030 m/s default cannot correct the observed 3:1 drive
  // mismatch, so migrate it to the new 0.300 m/s throttle allowance.
  if (settingsSchemaVersion < 3) {
    value.synchronizer.maximumCorrectionMps =
        defaults.synchronizer.maximumCorrectionMps;
  }

  // Schema 6 replaces the old generic motion profile with defaults derived
  // from the measured PA1664 chassis and the MWD V3.8 motor limits. Only the
  // known shipped profiles are migrated; user-tuned motion values remain.
  if (settingsSchemaVersion < 6 &&
      isLegacyDefaultMotionProfile(value)) {
    value.maximumWheelSpeedMps = defaults.maximumWheelSpeedMps;
    value.maximumLinearSpeedMps = defaults.maximumLinearSpeedMps;
    value.maximumAngularSpeedRadps = defaults.maximumAngularSpeedRadps;
    value.maximumLinearAccelerationMps2 =
        defaults.maximumLinearAccelerationMps2;
    value.maximumAngularAccelerationRadps2 =
        defaults.maximumAngularAccelerationRadps2;
    value.minimumInnerWheelRatio = defaults.minimumInnerWheelRatio;
    value.synchronizer = defaults.synchronizer;
  }

  if (settingsSchemaVersion < 10 &&
      (approximately(value.motorOutputToWheelRatio, 36.0) ||
       approximately(value.motorOutputToWheelRatio, 80.0) ||
       approximately(value.motorOutputToWheelRatio, 100.0))) {
    value.motorOutputToWheelRatio = defaults.motorOutputToWheelRatio;
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
  if (!isSupportedMwdRs485BaudRate(value.leftMotorBaudRate)) {
    value.leftMotorBaudRate = defaults.leftMotorBaudRate;
  }
  if (!isSupportedMwdRs485BaudRate(value.rightMotorBaudRate)) {
    value.rightMotorBaudRate = defaults.rightMotorBaudRate;
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
