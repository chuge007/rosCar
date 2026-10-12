#include "clamp_motor_controller.h"
#include "mwd_modbus_protocol.h"
#include "servo_protocol.h"
#include "utf8_compat.h"

#include <QtEndian>

#include <array>

namespace crawling {

ClampMotorController::ClampMotorController(QObject* parent) : QObject(parent) {}

void ClampMotorController::setSettings(const DriveSettings& settings) {
  const bool transportChanged =
      settings_.clampCommunicationMode != settings.clampCommunicationMode ||
      settings_.clampSerialPort != settings.clampSerialPort ||
      settings_.clampSerialBaudRate != settings.clampSerialBaudRate ||
      settings_.clampCanBitrate != settings.clampCanBitrate;
  if (transportChanged && port_.isOpen()) {
    stop();
    port_.close();
    connectionEnabled_ = false;
    emit connectionChanged(false, CRAWLING_TEXT("夹子电机配置已更改，连接已断开"));
  }
  settings_ = settings;
}

void ClampMotorController::connectDevice() {
  connectionEnabled_ = true;
  if (ensureOpen()) {
    emit connectionChanged(true, CRAWLING_TEXT("夹子电机已连接"));
  } else {
    emit connectionChanged(false, CRAWLING_TEXT("夹子电机连接失败"));
  }
}

void ClampMotorController::disconnectDevice() {
  if (port_.isOpen())
    stop();
  port_.close();
  connectionEnabled_ = false;
  emit connectionChanged(false, CRAWLING_TEXT("夹子电机已断开"));
}

QByteArray ClampMotorController::encodeFrame(std::uint32_t id, const QByteArray& data) const {
  if (id > 0x7FFU || data.size() != 8) {
    return {};
  }
  QByteArray out = CRAWLING_TEXT("t%1%2")
                       .arg(id, 3, 16, QLatin1Char('0'))
                       .arg(8, 1, 16, QLatin1Char('0'))
                       .toUpper()
                       .toLatin1();
  for (const char byte : data) {
    out += CRAWLING_TEXT("%1")
               .arg(static_cast<unsigned char>(byte), 2, 16, QLatin1Char('0'))
               .toUpper()
               .toLatin1();
  }
  out += '\r';
  return out;
}

bool ClampMotorController::ensureOpen() {
  if (port_.isOpen()) {
    return true;
  }
  port_.setPortName(settings_.clampSerialPort.trimmed());
  port_.setBaudRate(settings_.clampSerialBaudRate);
  port_.setDataBits(QSerialPort::Data8);
  port_.setParity(QSerialPort::NoParity);
  port_.setStopBits(QSerialPort::OneStop);
  port_.setFlowControl(QSerialPort::NoFlowControl);
  if (!port_.open(QIODevice::ReadWrite)) {
    emit statusChanged(CRAWLING_TEXT("夹子电机串口打开失败：%1")
                           .arg(port_.errorString()));
    return false;
  }
  port_.clear(QSerialPort::AllDirections);
  if (settings_.clampCommunicationMode == ClampCommunicationMode::MwdRs485) {
    return true;
  }

  const QByteArray bitrate =
      settings_.clampCanBitrate == 100000 ? "S3\r" :
      settings_.clampCanBitrate == 125000 ? "S4\r" :
      settings_.clampCanBitrate == 250000 ? "S5\r" :
      settings_.clampCanBitrate == 500000 ? "S6\r" :
      settings_.clampCanBitrate == 800000 ? "S7\r" : "S8\r";
  const auto writeControl = [this](const QByteArray& command) {
    return port_.write(command) == command.size() &&
           port_.waitForBytesWritten(100);
  };
  if (!writeControl("C\r") || !writeControl(bitrate) ||
      !writeControl("O\r")) {
    port_.close();
    emit statusChanged(CRAWLING_TEXT("夹子电机 CAN 适配器初始化失败"));
    return false;
  }
  return true;
}

bool ClampMotorController::sendCanFrame(std::uint32_t id,
                                        const QByteArray& data) {
  const QByteArray frame = encodeFrame(id, data);
  if (frame.isEmpty() || port_.write(frame) != frame.size() ||
      !port_.waitForBytesWritten(100)) {
    return false;
  }
  return true;
}

bool ClampMotorController::sendMwdModbusFrame(const QByteArray& frame) {
  if (frame.isEmpty() || port_.write(frame) != frame.size() ||
      !port_.waitForBytesWritten(100)) {
    emit statusChanged(CRAWLING_TEXT("夹子 MWD RS485 报文发送失败"));
    return false;
  }
  return true;
}

bool ClampMotorController::sendMwdModbusSpeed(int motorId, int direction,
                                              int sign) {
  const auto slaveId = static_cast<std::uint8_t>(motorId);
  const int pulsesPerSecond = direction * sign;
  if (!sendMwdModbusFrame(
          MwdModbusProtocol::velocityModeCommand(slaveId)) ||
      !sendMwdModbusFrame(
          MwdModbusProtocol::velocityCommand(slaveId, pulsesPerSecond)) ||
      !sendMwdModbusFrame(MwdModbusProtocol::runCommand(slaveId))) {
    emit statusChanged(CRAWLING_TEXT("夹子 MWD RS485 速度命令发送失败"));
    return false;
  }
  return true;
}

void ClampMotorController::move(int axis, int direction) {
  move(axis, direction, 100);
}

void ClampMotorController::move(int axis, int direction, int speedPercent) {
  if (!connectionEnabled_ || axis < 0 || axis > 2 || !ensureOpen()) {
    return;
  }
  speedPercent = qBound(0, speedPercent, 100);
  const int commandSpeed = direction * speedPercent * 10;
  const int ids[] = {settings_.clampXMotorId, settings_.clampYMotorId, settings_.clampZMotorId};
  const int signs[] = {settings_.clampXMotorSign, settings_.clampYMotorSign, settings_.clampZMotorSign};
  const int motorId = ids[axis];
  const int sign = signs[axis];
  const QString axisName = axis == 0 ? QStringLiteral("X")
                                     : axis == 1 ? QStringLiteral("Y")
                                                 : QStringLiteral("Z");

  if (settings_.clampCommunicationMode == ClampCommunicationMode::MwdRs485) {
    const auto slaveId = static_cast<std::uint8_t>(motorId);
    const bool sent =
        direction == 0
            ? sendMwdModbusFrame(MwdModbusProtocol::stopCommand(slaveId))
            : sendMwdModbusSpeed(motorId, commandSpeed, sign);
    if (!sent) {
      return;
    }
    emit statusChanged(
        direction == 0
            ? CRAWLING_TEXT("夹子 MWD RS485 %1 轴停止").arg(axisName)
            : CRAWLING_TEXT("夹子 MWD RS485 %1 轴 %2")
                  .arg(axisName)
                  .arg(direction > 0 ? "+" : "-"));
    return;
  }

  bool sent = false;
  if (settings_.clampCommunicationMode == ClampCommunicationMode::MwdCan) {
    const auto motor = static_cast<std::uint8_t>(motorId);
    const auto canFrame =
        direction == 0
            ? ServoProtocol::stopCommand(motor)
            : ServoProtocol::speedCommand(
                  motor, static_cast<double>(commandSpeed * sign));
    QByteArray data(reinterpret_cast<const char*>(canFrame.data.data()), 8);
    sent = sendCanFrame(canFrame.id, data);
  } else {
    QByteArray data(8, '\0');
    data[0] = char(0x23);
    data[1] = char(0xFF);
    data[2] = char(0x60);
    qToLittleEndian<qint32>(commandSpeed * sign,
                            reinterpret_cast<uchar*>(data.data() + 4));
    sent = sendCanFrame(0x600U + static_cast<std::uint32_t>(motorId), data);
  }
  if (!sent) {
    emit statusChanged(CRAWLING_TEXT("夹子电机报文发送失败"));
    return;
  }
  emit statusChanged(
      direction == 0
          ? CRAWLING_TEXT("夹子 %1 轴停止").arg(axisName)
          : CRAWLING_TEXT("夹子 %1 轴 %2")
                .arg(axisName)
                .arg(direction > 0 ? "+" : "-"));
}

void ClampMotorController::stop() {
  if (port_.isOpen()) {
    for (int i = 0; i < 3; ++i)
      move(i, 0);
  }
}
void ClampMotorController::stopX(){if (port_.isOpen()) move(0,0);}
void ClampMotorController::stopY(){if (port_.isOpen()) move(1,0);}
void ClampMotorController::stopZ(){if (port_.isOpen()) move(2,0);}
void ClampMotorController::setSpeed(int axis, int percent) {
  percent = qBound(-100, percent, 100);
  if (percent == 0) {
    if (port_.isOpen())
      move(axis, 0);
    return;
  }
  move(axis, percent > 0 ? 1 : -1, qAbs(percent));
}
void ClampMotorController::setXSpeed(int percent){setSpeed(0, percent);}
void ClampMotorController::setYSpeed(int percent){setSpeed(1, percent);}
void ClampMotorController::setZSpeed(int percent){setSpeed(2, percent);}
void ClampMotorController::moveXPositive(){move(0,1);} void ClampMotorController::moveXNegative(){move(0,-1);}
void ClampMotorController::moveYPositive(){move(1,1);} void ClampMotorController::moveYNegative(){move(1,-1);}
void ClampMotorController::moveZPositive(){move(2,1);} void ClampMotorController::moveZNegative(){move(2,-1);}
}
