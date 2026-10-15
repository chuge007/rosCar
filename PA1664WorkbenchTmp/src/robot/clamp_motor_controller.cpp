#include "clamp_motor_controller.h"
#include "mwd_modbus_protocol.h"
#include "servo_protocol.h"
#include "zdt_can_protocol.h"

#include <QtEndian>

namespace crawling {
namespace {
QString portKey(const QString& name) { return name.trimmed().toUpper(); }

QByteArray slcanBitrate(int rate) {
  switch (rate) {
    case 10000: return "S0\r";
    case 20000: return "S1\r";
    case 50000: return "S2\r";
    case 100000: return "S3\r";
    case 125000: return "S4\r";
    case 250000: return "S5\r";
    case 500000: return "S6\r";
    case 800000: return "S7\r";
    case 1000000: return "S8\r";
    default: return {};
  }
}
}

ClampMotorController::ClampMotorController(QObject* parent) : QObject(parent) {}

ClampMotorController::~ClampMotorController() {
  stop();
  closePorts();
}

void ClampMotorController::setSettings(const DriveSettings& settings) {
  const bool changed = settings_.clampMotors != settings.clampMotors;
  if (changed) {
    // Stop with the old routing before replacing IDs or protocols.
    stop();
    const bool hadPorts = !ports_.isEmpty();
    closePorts();
    if (hadPorts) {
      connectionEnabled_ = false;
      emit connectionChanged(false, QStringLiteral("夹子配置已更改，原连接已关闭"));
    }
  }
  settings_ = settings;
  if (changed) emit settingsChanged();
}

void ClampMotorController::closePorts() {
  for (auto* port : ports_) {
    port->close();
    delete port;
  }
  ports_.clear();
}

void ClampMotorController::connectDevice() {
  const QString error = settings_.clampValidationError();
  if (!error.isEmpty()) {
    stop();
    closePorts();
    connectionEnabled_ = false;
    emit statusChanged(error);
    emit connectionChanged(false, error);
    return;
  }
  connectionEnabled_ = true;
  int configured = 0;
  bool success = true;
  for (const auto& axis : settings_.clampMotors) {
    for (const auto& motor : axis) {
      if (motor.serialPort.trimmed().isEmpty()) continue;
      ++configured;
      if (!ensureOpen(motor)) success = false;
    }
  }
  if (!success) {
    stop();
    closePorts();
    connectionEnabled_ = false;
    emit connectionChanged(false, QStringLiteral("夹子电机连接失败，已关闭本次连接"));
  } else {
    connectionEnabled_ = configured > 0;
    emit connectionChanged(configured > 0,
        configured > 0 ? QStringLiteral("夹子通信端口已连接（%1 台电机，尚未确认电机应答）").arg(configured)
                       : QStringLiteral("夹子电机未配置通信端口"));
  }
}

void ClampMotorController::disconnectDevice() {
  stop();
  closePorts();
  connectionEnabled_ = false;
  emit connectionChanged(false, QStringLiteral("夹子电机已断开"));
}

QByteArray ClampMotorController::encodeFrame(std::uint32_t id,
                                             const QByteArray& data,
                                             bool extended) const {
  if (id > (extended ? 0x1FFFFFFFU : 0x7FFU) || data.isEmpty() || data.size() > 8)
    return {};
  // SLCAN: lowercase t = standard, uppercase T = extended.
  QByteArray out(1, extended ? 'T' : 't');
  out += QByteArray::number(id, 16).rightJustified(extended ? 8 : 3, '0').toUpper();
  out += QByteArray::number(data.size(), 16).toUpper();
  out += data.toHex().toUpper();
  out += '\r';
  return out;
}

QSerialPort* ClampMotorController::ensureOpen(const ClampMotorSettings& motor) {
  if (!connectionEnabled_ || motor.serialPort.trimmed().isEmpty()) return nullptr;
  const QString key = portKey(motor.serialPort);
  auto* port = ports_.value(key, nullptr);
  if (port && port->isOpen()) return port;
  if (!port) {
    port = new QSerialPort(this);
    ports_.insert(key, port);
  }
  port->setPortName(motor.serialPort.trimmed());
  port->setBaudRate(motor.serialBaudRate);
  port->setDataBits(QSerialPort::Data8);
  port->setParity(QSerialPort::NoParity);
  port->setStopBits(QSerialPort::OneStop);
  port->setFlowControl(QSerialPort::NoFlowControl);
  if (!port->open(QIODevice::ReadWrite)) {
    emit statusChanged(QStringLiteral("夹子端口 %1 打开失败：%2")
                           .arg(motor.serialPort, port->errorString()));
    return nullptr;
  }
  port->clear(QSerialPort::AllDirections);
  if (motor.communicationMode == ClampCommunicationMode::MwdRs485) return port;
  const QByteArray bitrate = slcanBitrate(motor.canBitrate);
  const auto writeControl = [port](const QByteArray& command) {
    return !command.isEmpty() && port->write(command) == command.size() &&
        port->waitForBytesWritten(100);
  };
  if (bitrate.isEmpty() || !writeControl("C\r") || !writeControl(bitrate) ||
      !writeControl("O\r")) {
    port->close();
    emit statusChanged(QStringLiteral("夹子 CAN 适配器 %1 初始化失败").arg(motor.serialPort));
    return nullptr;
  }
  return port;
}

bool ClampMotorController::sendCanFrame(QSerialPort* port, std::uint32_t id,
                                        const QByteArray& data, bool extended) {
  const QByteArray frame = encodeFrame(id, data, extended);
  return !frame.isEmpty() && port->write(frame) == frame.size() &&
      port->waitForBytesWritten(100);
}

bool ClampMotorController::sendMwdModbusFrame(QSerialPort* port,
                                              const QByteArray& frame) {
  return !frame.isEmpty() && port->write(frame) == frame.size() &&
      port->waitForBytesWritten(100);
}

bool ClampMotorController::sendMwdModbusSpeed(QSerialPort* port, int motorId,
                                              int speed) {
  const auto slave = static_cast<std::uint8_t>(motorId);
  return sendMwdModbusFrame(port, MwdModbusProtocol::velocityModeCommand(slave)) &&
      sendMwdModbusFrame(port, MwdModbusProtocol::velocityCommand(slave, speed)) &&
      sendMwdModbusFrame(port, MwdModbusProtocol::runCommand(slave));
}

bool ClampMotorController::sendMotorCommand(const ClampMotorSettings& motor,
                                            QSerialPort* port, int percent) {
  const int commandSpeed = percent * motor.sign * 10;
  if (motor.communicationMode == ClampCommunicationMode::MwdRs485) {
    return percent == 0
        ? sendMwdModbusFrame(port, MwdModbusProtocol::stopCommand(
                                      static_cast<std::uint8_t>(motor.motorId)))
        : sendMwdModbusSpeed(port, motor.motorId, commandSpeed);
  }
  if (motor.communicationMode == ClampCommunicationMode::ZdtCan) {
    const auto sendFrames = [this, port](const QVector<ZdtCanFrame>& frames) {
      if (frames.isEmpty()) return false;
      for (const auto& frame : frames)
        if (!sendCanFrame(port, frame.id, frame.data, true)) return false;
      return true;
    };
    if (percent == 0) return sendFrames(ZdtCanProtocol::stopCommand(motor.motorId));
    return sendFrames(ZdtCanProtocol::enableCommand(motor.motorId)) &&
        sendFrames(ZdtCanProtocol::speedCommand(
            motor.motorId, motor.zdtFirmware,
            percent * motor.sign * motor.zdtMaximumSpeedRpm / 10, motor.zdtAcceleration));
  }
  if (motor.communicationMode == ClampCommunicationMode::MwdCan) {
    const auto id = static_cast<std::uint8_t>(motor.motorId);
    const auto frame = percent == 0 ? ServoProtocol::stopCommand(id)
                                   : ServoProtocol::speedCommand(id, commandSpeed);
    return sendCanFrame(port, frame.id,
        QByteArray(reinterpret_cast<const char*>(frame.data.data()), 8));
  }
  QByteArray data(8, '\0');
  data[0] = char(0x23);
  data[1] = char(0xFF);
  data[2] = char(0x60);
  qToLittleEndian<qint32>(commandSpeed, reinterpret_cast<uchar*>(data.data() + 4));
  return sendCanFrame(port, 0x600U + static_cast<std::uint32_t>(motor.motorId), data);
}

void ClampMotorController::setMotorSpeed(int axis, int index, int percent) {
  if (axis < 0 || axis >= 3 || index < 0 || index >= settings_.clampMotors[axis].size())
    return;
  percent = qBound(-100, percent, 100);
  if (percent == 0) { stopMotor(axis, index); return; }
  if (!connectionEnabled_) {
    emit statusChanged(QStringLiteral("请先连接夹子电机"));
    return;
  }
  const QString error = settings_.clampValidationError();
  if (!error.isEmpty()) { emit statusChanged(error); return; }
  const auto& motor = settings_.clampMotors[axis][index];
  auto* port = ensureOpen(motor);
  const QString label = QStringLiteral("%1%2 / ID %3 / %4")
      .arg(QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z"))
      .arg(index + 1).arg(motor.motorId).arg(clampModeName(motor.communicationMode));
  if (!port || !sendMotorCommand(motor, port, percent)) {
    if (port) sendMotorCommand(motor, port, 0);
    emit statusChanged(QStringLiteral("夹子 %1 指令发送失败，请检查连接").arg(label));
    return;
  }
  emit statusChanged(QStringLiteral("夹子 %1 已发送速度 %2%").arg(label).arg(percent));
}

void ClampMotorController::stopMotor(int axis, int index) {
  if (axis < 0 || axis >= 3 || index < 0 || index >= settings_.clampMotors[axis].size())
    return;
  const auto& motor = settings_.clampMotors[axis][index];
  auto* port = ports_.value(portKey(motor.serialPort), nullptr);
  // Stopping never opens an unconnected port or enables a motor.
  if (port && port->isOpen() && !sendMotorCommand(motor, port, 0))
    emit statusChanged(QStringLiteral("夹子 %1%2 停止指令发送失败")
        .arg(QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z")).arg(index + 1));
}

void ClampMotorController::move(int axis, int direction) { move(axis, direction, 100); }
void ClampMotorController::move(int axis, int direction, int speedPercent) {
  setSpeed(axis, direction * qBound(0, speedPercent, 100));
}
void ClampMotorController::setSpeed(int axis, int percent) {
  if (axis < 0 || axis >= 3) return;
  for (int index = 0; index < settings_.clampMotors[axis].size(); ++index)
    setMotorSpeed(axis, index, percent);
}
void ClampMotorController::stop() {
  for (int axis = 0; axis < 3; ++axis)
    for (int index = 0; index < settings_.clampMotors[axis].size(); ++index)
      stopMotor(axis, index);
  emit controlsResetRequested();
}
void ClampMotorController::stopX(){setSpeed(0, 0);}
void ClampMotorController::stopY(){setSpeed(1, 0);}
void ClampMotorController::stopZ(){setSpeed(2, 0);}
void ClampMotorController::setXSpeed(int percent){setSpeed(0, percent);}
void ClampMotorController::setYSpeed(int percent){setSpeed(1, percent);}
void ClampMotorController::setZSpeed(int percent){setSpeed(2, percent);}
void ClampMotorController::moveXPositive(){move(0,1);} void ClampMotorController::moveXNegative(){move(0,-1);}
void ClampMotorController::moveYPositive(){move(1,1);} void ClampMotorController::moveYNegative(){move(1,-1);}
void ClampMotorController::moveZPositive(){move(2,1);} void ClampMotorController::moveZNegative(){move(2,-1);}
} // namespace crawling
