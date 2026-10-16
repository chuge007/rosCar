#include "clamp_motor_controller.h"
#include "candle_can_adapter.h"
#include "pcan_can_adapter.h"
#include "mwd_modbus_protocol.h"
#include "robot_app_logger.h"
#include "servo_protocol.h"
#include "zdt_can_protocol.h"

#include <QtEndian>
#include <QDateTime>
#include <QTimer>

namespace crawling {
namespace {
QString portKey(const QString& name) {
  const QString canonical = PcanCanAdapter::canonicalPort(name);
  return (canonical.isEmpty() ? name.trimmed() : canonical).toUpper();
}

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

QString axisName(int axis) {
  return QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
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
    const bool hadPorts = !ports_.isEmpty() || !candlePorts_.isEmpty() || !pcanPorts_.isEmpty();
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
  for (auto* adapter : candlePorts_) delete adapter;
  candlePorts_.clear();
  for (auto* adapter : pcanPorts_) delete adapter;
  pcanPorts_.clear();
  zdtEnabledMotors_.clear();
  zdtEnablePending_.clear();
  zdtProbePending_.clear();
  zdtRequestedPercent_.clear();
  zdtSpeedTickets_.clear();
  speedLogTime_.clear();
  ++zdtSession_;
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
  zdtEnabledMotors_.clear();
  zdtEnablePending_.clear();
  zdtProbePending_.clear();
  zdtRequestedPercent_.clear();
  zdtSpeedTickets_.clear();
  ++zdtSession_;
  int configured = 0;
  bool success = true;
  for (int axisIndex = 0; axisIndex < settings_.clampMotors.size(); ++axisIndex) {
    const auto& axis = settings_.clampMotors[axisIndex];
    for (int motorIndex = 0; motorIndex < axis.size(); ++motorIndex) {
      const auto& motor = axis[motorIndex];
      if (motor.serialPort.trimmed().isEmpty()) {
        const QString label = QStringLiteral("夹子 %1%2 / ID %3 / %4")
            .arg(axisName(axisIndex)).arg(motorIndex + 1).arg(motor.motorId)
            .arg(clampModeName(motor.communicationMode));
        AppLogger::warning(QStringLiteral("CLAMP.CONNECTION"),
            QStringLiteral("event=port_not_configured %1")
                .arg(motorContext(motor, axisIndex, motorIndex)));
        emit statusChanged(label + QStringLiteral("：未配置端口，电机未连接"));
        continue;
      }
      ++configured;
      const QString context = motorContext(motor, axisIndex, motorIndex);
      AppLogger::write(QStringLiteral("CLAMP.CONNECTION"),
          QStringLiteral("event=open_request %1").arg(context));
      if (!ensureOpen(motor)) {
        AppLogger::error(QStringLiteral("CLAMP.CONNECTION"),
            QStringLiteral("event=open_failed %1").arg(context));
        success = false;
        emit statusChanged(QStringLiteral("夹子 %1%2 / ID %3 / %4：%5 打开失败，电机未连接")
            .arg(axisName(axisIndex)).arg(motorIndex + 1).arg(motor.motorId)
            .arg(clampModeName(motor.communicationMode), motor.serialPort));
      } else {
        AppLogger::write(QStringLiteral("CLAMP.CONNECTION"),
            QStringLiteral("event=open_ok %1").arg(context));
        const QString label = QStringLiteral("夹子 %1%2 / ID %3 / %4 / %5 / %6")
            .arg(axisName(axisIndex)).arg(motorIndex + 1).arg(motor.motorId)
            .arg(clampModeName(motor.communicationMode), motor.serialPort)
            .arg(motor.communicationMode == ClampCommunicationMode::MwdRs485
                     ? QStringLiteral("串口 %1 baud").arg(motor.serialBaudRate)
                     : QStringLiteral("CAN %1 bit/s").arg(motor.canBitrate));
        if (motor.communicationMode != ClampCommunicationMode::ZdtCan) {
          emit statusChanged(label + QStringLiteral("：接口已打开，尚无电机应答验证"));
          continue;
        }
        const QString key = zdtMotorKey(motor, axisIndex, motorIndex);
        const auto frames = ZdtCanProtocol::packetize(motor.motorId,
                                                       QByteArray::fromHex("366B"));
        if (frames.size() != 1 ||
            !sendCanFrame(motor, frames[0].id, frames[0].data, true,
                          context + QStringLiteral(" protocol=ZDT command=probe"))) {
          emit statusChanged(label + QStringLiteral("：接口已打开，但电机探测帧写入失败"));
          success = false;
          continue;
        }
        zdtProbePending_.insert(key);
        emit statusChanged(label + QStringLiteral("：接口已打开；TX 扩展帧 ID=0x%1 DATA=36 6B，等待电机应答")
            .arg(static_cast<qulonglong>(frames[0].id), 0, 16));
        const quint64 session = zdtSession_;
        QTimer::singleShot(700, this, [this, key, label, session] {
          if (session != zdtSession_ || !zdtProbePending_.remove(key)) return;
          AppLogger::warning(QStringLiteral("CLAMP.ZDT"),
              QStringLiteral("event=probe_timeout motor=%1").arg(label));
          emit statusChanged(label + QStringLiteral("：电机探测超时；检查供电、CAN 接线/速率、地址和 0x6B 校验"));
        });
      }
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
  AppLogger::write(QStringLiteral("CLAMP.CONNECTION"),
                   QStringLiteral("event=disconnect"));
  emit connectionChanged(false, QStringLiteral("夹子电机已断开"));
}

QString ClampMotorController::motorContext(const ClampMotorSettings& motor,
                                            int axis, int index) const {
  QString port = motor.serialPort.trimmed();
  port.replace(QLatin1Char('"'), QLatin1Char('\''));
  const QString position = axis >= 0 && index >= 0
      ? QStringLiteral("axis=%1 motor_index=%2").arg(axisName(axis)).arg(index + 1)
      : QStringLiteral("axis=? motor_index=?");
  return QStringLiteral("%1 motor_id=%2 mode=%3 port=\"%4\" serial_baud=%5 "
                        "can_bitrate=%6 direction=%7")
      .arg(position)
      .arg(motor.motorId)
      .arg(clampModeName(motor.communicationMode))
      .arg(port)
      .arg(motor.serialBaudRate)
      .arg(motor.canBitrate)
      .arg(motor.sign);
}

QString ClampMotorController::zdtMotorKey(const ClampMotorSettings& motor,
                                           int axis, int index) const {
  return QStringLiteral("%1/%2/%3/%4/%5")
      .arg(axis)
      .arg(index)
      .arg(portKey(motor.serialPort))
      .arg(motor.motorId)
      .arg(static_cast<int>(motor.communicationMode));
}

void ClampMotorController::logSpeedStatus(const QString& key,
                                           const QString& message) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - speedLogTime_.value(key, 0) < 300) return;
  speedLogTime_.insert(key, now);
  emit statusChanged(message);
}

void ClampMotorController::onCanFrame(const QString& port, std::uint32_t id,
                                      const QByteArray& data, bool extended) {
  if (!extended || (id & 0xFFU) != 0 || data.size() < 3 ||
      quint8(data.back()) != 0x6B) return;
  const int address = int(id >> 8);
  for (int axis = 0; axis < 3; ++axis) {
    for (int index = 0; index < settings_.clampMotors[axis].size(); ++index) {
      const auto& motor = settings_.clampMotors[axis][index];
      if (motor.communicationMode != ClampCommunicationMode::ZdtCan ||
          motor.motorId != address || portKey(motor.serialPort) != port) continue;
      const QString key = zdtMotorKey(motor, axis, index);
      const QString label = QStringLiteral("夹子 %1%2 / ID %3 / %4")
          .arg(axisName(axis)).arg(index + 1).arg(address).arg(motor.serialPort);
      const quint8 command = quint8(data[0]);
      const quint8 result = quint8(data[1]);
      AppLogger::write(QStringLiteral("CLAMP.ZDT"),
          QStringLiteral("event=motor_response %1 can_id=0x%2 data=%3")
              .arg(motorContext(motor, axis, index))
              .arg(static_cast<qulonglong>(id), 0, 16)
              .arg(QString::fromLatin1(data.toHex().toUpper())));
      if (command == 0x36 && data.size() == 7) {
        if (zdtProbePending_.remove(key))
          emit statusChanged(label + QStringLiteral("：RX ID=0x%1 DATA=%2；电机通信正常（不代表已使能）")
              .arg(static_cast<qulonglong>(id), 0, 16)
              .arg(QString::fromLatin1(data.toHex(' ').toUpper())));
      } else if (command == 0x36 && data.size() == 3 &&
                 zdtProbePending_.remove(key)) {
        emit statusChanged(label + QStringLiteral("：电机响应位置查询，但返回错误 36 %1")
            .arg(result, 2, 16, QLatin1Char('0')));
      } else if (command == 0xF3 && data.size() == 3 &&
                 zdtEnablePending_.remove(key)) {
        if (result == 0x02) {
          zdtEnabledMotors_.insert(key);
          emit statusChanged(label + QStringLiteral("：RX ID=0x%1 DATA=F3 02 6B；驱动板使能确认")
              .arg(static_cast<qulonglong>(id), 0, 16));
          const int requested = zdtRequestedPercent_.take(key);
          if (requested != 0) {
            if (sendMotorCommand(motor, requested, axis, index))
              emit statusChanged(label + QStringLiteral("：使能后速度指令已写入，等待电机应答"));
            else
              emit statusChanged(label + QStringLiteral("：使能成功，但速度指令写入失败"));
          }
        } else {
          zdtRequestedPercent_.remove(key);
          emit statusChanged(label + QStringLiteral("：驱动板拒绝使能（F3 %1），未发送速度")
              .arg(result, 2, 16, QLatin1Char('0')));
        }
      } else if (command == 0xF6 && data.size() == 3 &&
                 zdtSpeedTickets_.remove(key) > 0) {
        if (result == 0x02) {
          logSpeedStatus(key + QStringLiteral("/ack"),
                         label + QStringLiteral("：RX ID=0x%1 DATA=F6 02 6B；电机已接收速度指令")
                             .arg(static_cast<qulonglong>(id), 0, 16));
        } else {
          if (result == 0xE2 || result == 0xEE) zdtEnabledMotors_.remove(key);
          emit statusChanged(label + QStringLiteral("：电机速度应答 F6 %1（02 为接收正确；12/22 为状态，E2/EE 为错误）")
              .arg(result, 2, 16, QLatin1Char('0')));
        }
      } else if (command == 0xFE && data.size() == 3) {
        if (result == 0x02)
          emit statusChanged(label + QStringLiteral("：RX FE 02 6B；停止指令已应答"));
        else
          emit statusChanged(label + QStringLiteral("：停止应答异常 FE %1")
              .arg(result, 2, 16, QLatin1Char('0')));
      }
    }
  }
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

bool ClampMotorController::ensureOpen(const ClampMotorSettings& motor) {
  if (!connectionEnabled_ || motor.serialPort.trimmed().isEmpty()) return false;
  const QString key = portKey(motor.serialPort);
  if (PcanCanAdapter::isPcanPort(motor.serialPort)) {
    if (motor.communicationMode == ClampCommunicationMode::MwdRs485) {
      emit statusChanged(QStringLiteral("PCAN 是 CAN 适配器，不能用于 RS485"));
      return false;
    }
    auto* adapter = pcanPorts_.value(key, nullptr);
    if (adapter && adapter->isOpen()) return true;
    if (!adapter) {
      adapter = new PcanCanAdapter(this);
      pcanPorts_.insert(key, adapter);
      connect(adapter, &PcanCanAdapter::frameReceived, this,
          [this, key](std::uint32_t id, const QByteArray& data, bool extended) {
            onCanFrame(key, id, data, extended);
          });
      connect(adapter, &PcanCanAdapter::faulted, this, [this](const QString& error) {
        connectionEnabled_ = false;
        // Attempt a stop on every initialized channel before inhibiting this adapter.
        stop();
        emit statusChanged(error);
        emit connectionChanged(false, error + QStringLiteral("；请检查后重新连接"));
      });
    }
    QString error;
    if (adapter->open(motor.serialPort, motor.canBitrate, &error)) return true;
    emit statusChanged(error);
    return false;
  }
  if (CandleCanAdapter::isCandlePort(motor.serialPort)) {
    if (motor.communicationMode == ClampCommunicationMode::MwdRs485) {
      emit statusChanged(QStringLiteral("candleLight 是 CAN 适配器，不能用于 RS485"));
      return false;
    }
    auto* adapter = candlePorts_.value(key, nullptr);
    if (adapter && adapter->isOpen()) return true;
    if (!adapter) {
      adapter = new CandleCanAdapter(this);
      candlePorts_.insert(key, adapter);
      connect(adapter, &CandleCanAdapter::frameReceived, this,
          [this, key](std::uint32_t id, const QByteArray& data, bool extended) {
            onCanFrame(key, id, data, extended);
          });
      connect(adapter, &CandleCanAdapter::faulted, this, [this](const QString& error) {
        connectionEnabled_ = false;
        emit statusChanged(error);
        emit connectionChanged(false, error + QStringLiteral("；请检查后重新连接"));
      });
    }
    QString error;
    if (adapter->open(motor.serialPort, motor.canBitrate, &error)) return true;
    emit statusChanged(error);
    return false;
  }
  auto* port = ports_.value(key, nullptr);
  if (port && port->isOpen()) return true;
  if (!port) {
    port = new QSerialPort(this);
    ports_.insert(key, port);
    connect(port, &QSerialPort::readyRead, this, [this, port, key, buffer = QByteArray()]() mutable {
      buffer += port->readAll();
      while (true) {
        const int end = buffer.indexOf('\r');
        if (end < 0) break;
        const QByteArray line = buffer.left(end).trimmed();
        buffer.remove(0, end + 1);
        if (line.size() < 6 || line[0] != 'T') continue;
        bool idOk = false, lengthOk = false;
        const auto id = line.mid(1, 8).toUInt(&idOk, 16);
        const int length = line.mid(9, 1).toInt(&lengthOk, 16);
        const QByteArray data = QByteArray::fromHex(line.mid(10));
        if (idOk && lengthOk && length <= 8 && data.size() == length)
          onCanFrame(key, id, data, true);
      }
      if (buffer.size() > 1024) buffer.clear();
    });
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
    return false;
  }
  port->clear(QSerialPort::AllDirections);
  if (motor.communicationMode == ClampCommunicationMode::MwdRs485) return true;
  const QByteArray bitrate = slcanBitrate(motor.canBitrate);
  const auto writeControl = [port](const QByteArray& command) {
    return !command.isEmpty() && port->write(command) == command.size() &&
        port->waitForBytesWritten(100);
  };
  if (bitrate.isEmpty() || !writeControl("C\r") || !writeControl(bitrate) ||
      !writeControl("O\r")) {
    port->close();
    emit statusChanged(QStringLiteral("夹子 CAN 适配器 %1 初始化失败").arg(motor.serialPort));
    return false;
  }
  return true;
}

bool ClampMotorController::sendCanFrame(const ClampMotorSettings& motor,
                                        std::uint32_t id,
                                        const QByteArray& data, bool extended,
                                        const QString& context) {
  const QString frameContext = context.isEmpty()
      ? motorContext(motor, -1, -1) : context;
  const QString frameDetails = QStringLiteral(
      "event=can_frame %1 can_id=0x%2 extended=%3 dlc=%4 data=%5")
      .arg(frameContext)
      .arg(static_cast<qulonglong>(id), 0, 16)
      .arg(extended ? 1 : 0)
      .arg(data.size())
      .arg(QString::fromLatin1(data.toHex().toUpper()));
  const auto fail = [this, &frameDetails](const QString& error) {
    AppLogger::error(QStringLiteral("CLAMP.CAN"),
                     QStringLiteral("%1 result=FAILED error=%2")
                         .arg(frameDetails, error));
    emit statusChanged(error);
    return false;
  };
  const auto sent = [this, &frameDetails] {
    AppLogger::write(QStringLiteral("CLAMP.CAN"),
                     QStringLiteral("%1 result=OK").arg(frameDetails));
    return true;
  };
  if (PcanCanAdapter::isPcanPort(motor.serialPort)) {
    auto* adapter = pcanPorts_.value(portKey(motor.serialPort), nullptr);
    if (!adapter) return fail(QStringLiteral("PCAN 通道未初始化"));
    QString error;
    if (adapter->sendFrame(id, data, extended, &error)) return sent();
    return fail(error.isEmpty() ? QStringLiteral("PCAN CAN 帧发送失败") : error);
  }
  if (CandleCanAdapter::isCandlePort(motor.serialPort)) {
    auto* adapter = candlePorts_.value(portKey(motor.serialPort), nullptr);
    if (!adapter) return fail(QStringLiteral("candle CAN 通道未初始化"));
    QString error;
    if (adapter->sendFrame(id, data, extended, &error)) return sent();
    return fail(error.isEmpty() ? QStringLiteral("candle CAN 帧发送失败") : error);
  }
  auto* port = ports_.value(portKey(motor.serialPort), nullptr);
  if (!port || !port->isOpen()) return fail(QStringLiteral("串口未连接"));
  const QByteArray frame = encodeFrame(id, data, extended);
  if (frame.isEmpty()) return fail(QStringLiteral("SLCAN CAN 帧格式无效"));
  if (port->write(frame) == frame.size() && port->waitForBytesWritten(100))
    return sent();
  return fail(QStringLiteral("SLCAN CAN 帧发送失败：%1").arg(port->errorString()));
}

bool ClampMotorController::sendMwdModbusFrame(QSerialPort* port,
                                              const QByteArray& frame,
                                              const QString& context) {
  const QString frameDetails = QStringLiteral("event=rs485_frame %1 data=%2")
      .arg(context.isEmpty() ? QStringLiteral("axis=? motor_index=?") : context)
      .arg(QString::fromLatin1(frame.toHex().toUpper()));
  if (!port || !port->isOpen()) {
    AppLogger::error(QStringLiteral("CLAMP.RS485"),
        QStringLiteral("%1 result=FAILED error=串口未连接").arg(frameDetails));
    return false;
  }
  if (!frame.isEmpty() && port->write(frame) == frame.size() &&
      port->waitForBytesWritten(100)) {
    AppLogger::write(QStringLiteral("CLAMP.RS485"),
                     QStringLiteral("%1 result=OK").arg(frameDetails));
    return true;
  }
  AppLogger::error(QStringLiteral("CLAMP.RS485"),
      QStringLiteral("%1 result=FAILED error=%2")
          .arg(frameDetails, port->errorString()));
  return false;
}

bool ClampMotorController::sendMwdModbusSpeed(QSerialPort* port, int motorId,
                                              int speed,
                                              const QString& context) {
  const auto slave = static_cast<std::uint8_t>(motorId);
  const auto commandContext = [&context](const QString& command) {
    return QStringLiteral("%1 command=%2").arg(context, command);
  };
  return sendMwdModbusFrame(port, MwdModbusProtocol::velocityModeCommand(slave),
                            commandContext(QStringLiteral("velocity_mode"))) &&
      sendMwdModbusFrame(port, MwdModbusProtocol::velocityCommand(slave, speed),
                         commandContext(QStringLiteral("velocity"))) &&
      sendMwdModbusFrame(port, MwdModbusProtocol::runCommand(slave),
                         commandContext(QStringLiteral("run")));
}

bool ClampMotorController::sendMotorCommand(const ClampMotorSettings& motor,
                                             int percent, int axis, int index) {
  const QString context = motorContext(motor, axis, index);
  AppLogger::write(QStringLiteral("CLAMP.MOTOR"),
      QStringLiteral("event=command_request %1 slider_percent=%2")
          .arg(context).arg(percent));
  const int commandSpeed = percent * motor.sign * 10;
  if (motor.communicationMode == ClampCommunicationMode::MwdRs485) {
    auto* port = ports_.value(portKey(motor.serialPort), nullptr);
    if (!port || !port->isOpen()) {
      AppLogger::error(QStringLiteral("CLAMP.RS485"),
          QStringLiteral("event=command_failed %1 reason=串口未连接")
              .arg(context));
      return false;
    }
    return percent == 0
        ? sendMwdModbusFrame(port, MwdModbusProtocol::stopCommand(
                                      static_cast<std::uint8_t>(motor.motorId)),
                             context + QStringLiteral(" command=stop"))
        : sendMwdModbusSpeed(port, motor.motorId, commandSpeed, context);
  }
  if (motor.communicationMode == ClampCommunicationMode::ZdtCan) {
    const QString key = zdtMotorKey(motor, axis, index);
    const auto sendFrames = [this, &motor, &context](const QVector<ZdtCanFrame>& frames,
                                                     const QString& command) {
      if (frames.isEmpty()) {
        AppLogger::error(QStringLiteral("CLAMP.ZDT"),
            QStringLiteral("event=frame_build_failed %1 command=%2")
                .arg(context, command));
        return false;
      }
      for (const auto& frame : frames) {
        const QString frameContext = QStringLiteral("%1 protocol=ZDT command=%2")
            .arg(context, command);
        if (!sendCanFrame(motor, frame.id, frame.data, true, frameContext)) return false;
      }
      return true;
    };
    if (percent == 0) {
      zdtRequestedPercent_.remove(key);
      zdtSpeedTickets_.remove(key);
      return sendFrames(ZdtCanProtocol::stopCommand(motor.motorId),
                        QStringLiteral("stop"));
    }
    if (!zdtEnabledMotors_.contains(key)) {
      zdtRequestedPercent_.insert(key, percent);
      if (zdtEnablePending_.contains(key)) return true;
      if (!sendFrames(ZdtCanProtocol::enableCommand(motor.motorId),
                      QStringLiteral("enable"))) {
        zdtRequestedPercent_.remove(key);
        AppLogger::error(QStringLiteral("CLAMP.ZDT"),
            QStringLiteral("event=enable_failed %1").arg(context));
        return false;
      }
      zdtEnablePending_.insert(key);
      AppLogger::write(QStringLiteral("CLAMP.ZDT"),
          QStringLiteral("event=enable_state %1 state=waiting_for_motor_ack").arg(context));
      emit statusChanged(QStringLiteral("夹子 %1%2 / ID %3：TX 扩展帧 ID=0x%4 DATA=F3 AB 01 00 6B；等待驱动板使能确认")
          .arg(axisName(axis)).arg(index + 1).arg(motor.motorId)
          .arg(static_cast<qulonglong>(static_cast<std::uint32_t>(motor.motorId) << 8), 0, 16));
      const quint64 session = zdtSession_;
      QTimer::singleShot(700, this, [this, key, context, session] {
        if (session != zdtSession_ || !zdtEnablePending_.remove(key)) return;
        zdtRequestedPercent_.remove(key);
        AppLogger::error(QStringLiteral("CLAMP.ZDT"),
            QStringLiteral("event=enable_timeout %1").arg(context));
        emit statusChanged(QStringLiteral("夹子 %1：驱动板使能应答超时，速度未发送；检查电源、CAN 线/速率、ID 和 0x6B")
            .arg(context));
      });
      return true;
    }
    const int signedTenthsRpm =
        percent * motor.sign * motor.zdtMaximumSpeedRpm / 10;
    AppLogger::write(QStringLiteral("CLAMP.ZDT"),
        QStringLiteral("event=speed_request %1 slider_percent=%2 signed_tenths_rpm=%3 "
                       "firmware=%4 acceleration=%5 enable_confirmed=1")
            .arg(context)
            .arg(percent)
            .arg(signedTenthsRpm)
            .arg(motor.zdtFirmware == ZdtFirmware::X ? QStringLiteral("X")
                                                      : QStringLiteral("Emm"))
            .arg(motor.zdtAcceleration));
    const bool sent = sendFrames(ZdtCanProtocol::speedCommand(
                                      motor.motorId, motor.zdtFirmware,
                                      signedTenthsRpm, motor.zdtAcceleration),
                                  QStringLiteral("speed"));
    if (!sent) {
      // The adapter may have failed after accepting the enable frame. Force a
      // fresh enable on the next non-zero command rather than assuming state.
      zdtEnabledMotors_.remove(key);
      AppLogger::error(QStringLiteral("CLAMP.ZDT"),
          QStringLiteral("event=speed_failed %1 signed_tenths_rpm=%2")
              .arg(context).arg(signedTenthsRpm));
    } else {
      const auto frames = ZdtCanProtocol::speedCommand(motor.motorId, motor.zdtFirmware,
                                                        signedTenthsRpm, motor.zdtAcceleration);
      if (!frames.isEmpty())
        logSpeedStatus(key + QStringLiteral("/tx"),
            QStringLiteral("夹子 %1%2 / ID %3：TX 扩展帧 ID=0x%4 DATA=%5；速度 %6%，等待应答")
                .arg(axisName(axis)).arg(index + 1).arg(motor.motorId)
                .arg(static_cast<qulonglong>(frames[0].id), 0, 16)
                .arg(QString::fromLatin1(frames[0].data.toHex(' ').toUpper()))
                .arg(percent));
      const int ticket = ++nextSpeedTicket_;
      zdtSpeedTickets_.insert(key, ticket);
      const quint64 session = zdtSession_;
      QTimer::singleShot(700, this, [this, key, context, ticket, session] {
        if (session != zdtSession_ || zdtSpeedTickets_.value(key) != ticket) return;
        zdtSpeedTickets_.remove(key);
        AppLogger::warning(QStringLiteral("CLAMP.ZDT"),
            QStringLiteral("event=speed_ack_timeout %1").arg(context));
        emit statusChanged(QStringLiteral("夹子 %1：速度帧已写入但电机未应答，不能确认已运行")
            .arg(context));
      });
    }
    return sent;
  }
  if (motor.communicationMode == ClampCommunicationMode::MwdCan) {
    const auto id = static_cast<std::uint8_t>(motor.motorId);
    const auto frame = percent == 0 ? ServoProtocol::stopCommand(id)
                                   : ServoProtocol::speedCommand(id, commandSpeed);
    return sendCanFrame(motor, frame.id,
        QByteArray(reinterpret_cast<const char*>(frame.data.data()), 8), false,
        context + (percent == 0 ? QStringLiteral(" command=stop")
                                : QStringLiteral(" command=speed")));
  }
  QByteArray data(8, '\0');
  data[0] = char(0x23);
  data[1] = char(0xFF);
  data[2] = char(0x60);
  qToLittleEndian<qint32>(commandSpeed, reinterpret_cast<uchar*>(data.data() + 4));
  return sendCanFrame(motor, 0x600U + static_cast<std::uint32_t>(motor.motorId), data,
                      false, context + QStringLiteral(" command=sdo_speed"));
}

void ClampMotorController::setMotorSpeed(int axis, int index, int percent) {
  if (axis < 0 || axis >= 3 || index < 0 || index >= settings_.clampMotors[axis].size())
    return;
  percent = qBound(-100, percent, 100);
  if (percent == 0) { stopMotor(axis, index); return; }
  const auto& motor = settings_.clampMotors[axis][index];
  const QString context = motorContext(motor, axis, index);
  AppLogger::write(QStringLiteral("CLAMP.MOTOR"),
      QStringLiteral("event=slider_change %1 slider_percent=%2")
          .arg(context).arg(percent));
  if (!connectionEnabled_) {
    AppLogger::warning(QStringLiteral("CLAMP.MOTOR"),
        QStringLiteral("event=command_rejected %1 reason=not_connected")
            .arg(context));
    emit statusChanged(QStringLiteral("请先连接夹子电机"));
    return;
  }
  const QString error = settings_.clampValidationError();
  if (!error.isEmpty()) {
    AppLogger::error(QStringLiteral("CLAMP.MOTOR"),
        QStringLiteral("event=command_rejected %1 reason=invalid_settings error=%2")
            .arg(context, error));
    emit statusChanged(error);
    return;
  }
  const bool opened = ensureOpen(motor);
  const QString label = QStringLiteral("%1%2 / ID %3 / %4")
      .arg(QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z"))
      .arg(index + 1).arg(motor.motorId).arg(clampModeName(motor.communicationMode));
  if (!opened || !sendMotorCommand(motor, percent, axis, index)) {
    if (opened) sendMotorCommand(motor, 0, axis, index);
    AppLogger::error(QStringLiteral("CLAMP.MOTOR"),
        QStringLiteral("event=command_failed %1 slider_percent=%2")
            .arg(context).arg(percent));
    emit statusChanged(QStringLiteral("夹子 %1 指令发送失败，请检查连接").arg(label));
    return;
  }
  AppLogger::write(QStringLiteral("CLAMP.MOTOR"),
      QStringLiteral("event=command_complete %1 slider_percent=%2")
          .arg(context).arg(percent));
  const QString key = zdtMotorKey(motor, axis, index);
  if (motor.communicationMode == ClampCommunicationMode::ZdtCan) {
    if (zdtEnablePending_.contains(key))
      logSpeedStatus(key, QStringLiteral("夹子 %1：等待使能确认，暂缓速度 %2%")
          .arg(label).arg(percent));
  } else {
    logSpeedStatus(key, QStringLiteral("夹子 %1：速度 %2% 已写入接口，电机未验证")
        .arg(label).arg(percent));
  }
}

void ClampMotorController::stopMotor(int axis, int index) {
  if (axis < 0 || axis >= 3 || index < 0 || index >= settings_.clampMotors[axis].size())
    return;
  const auto& motor = settings_.clampMotors[axis][index];
  auto* port = ports_.value(portKey(motor.serialPort), nullptr);
  auto* adapter = candlePorts_.value(portKey(motor.serialPort), nullptr);
  auto* pcan = pcanPorts_.value(portKey(motor.serialPort), nullptr);
  // Stopping never opens an unconnected port or enables a motor.
  if (!((port && port->isOpen()) || (adapter && adapter->isOpen()) ||
        (pcan && pcan->isOpen()))) return;
  const QString label = QStringLiteral("夹子 %1%2 / ID %3")
      .arg(axisName(axis)).arg(index + 1).arg(motor.motorId);
  if (!sendMotorCommand(motor, 0, axis, index))
    emit statusChanged(label + QStringLiteral("：停止指令写入失败"));
  else
    emit statusChanged(label + (motor.communicationMode == ClampCommunicationMode::ZdtCan
        ? QStringLiteral("：TX FE 98 00 6B；停止帧已写入，等待电机应答")
        : QStringLiteral("：停止指令已写入接口，电机未验证")));
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
