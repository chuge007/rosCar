#include "hardware_discovery.h"

#include "rim302_protocol.h"
#include "servo_protocol.h"

#include <QElapsedTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include "utf8_compat.h"

#include <algorithm>

namespace crawling {
namespace {

const QVector<int> kSerialBauds{115200, 230400, 460800, 921600};
const QVector<int> kCanBitrates{1000000, 500000, 800000, 250000, 125000};

QByteArray bitrateCommand(int bitrate) {
  switch (bitrate) {
    case 125000: return "S4\r";
    case 250000: return "S5\r";
    case 500000: return "S6\r";
    case 800000: return "S7\r";
    case 1000000: return "S8\r";
    default: return {};
  }
}

QByteArray encodeFrame(const CanFrame& frame) {
  if (frame.id > 0x7FFU) {
    return {};
  }
  QByteArray command = CRAWLING_TEXT("t%1%2")
                           .arg(frame.id, 3, 16, QLatin1Char('0'))
                           .arg(8, 1, 16, QLatin1Char('0'))
                           .toUpper()
                           .toLatin1();
  command[0] = 't';
  for (const std::uint8_t byte : frame.data) {
    command.append(CRAWLING_TEXT("%1").arg(byte, 2, 16, QLatin1Char('0'))
                       .toUpper()
                       .toLatin1());
  }
  command.append('\r');
  return command;
}

bool parseHex(const QByteArray& text, int* result) {
  bool okay = false;
  const int value = text.toInt(&okay, 16);
  if (okay) {
    *result = value;
  }
  return okay;
}

bool parseFrame(const QByteArray& line, CanFrame* frame) {
  if (line.size() != 21 || (line.at(0) != 't' && line.at(0) != 'T')) {
    return false;
  }
  int id = 0;
  int length = 0;
  if (!parseHex(line.mid(1, 3), &id) || !parseHex(line.mid(4, 1), &length) ||
      length != 8) {
    return false;
  }
  frame->id = static_cast<std::uint32_t>(id);
  for (int index = 0; index < length; ++index) {
    int value = 0;
    if (!parseHex(line.mid(5 + index * 2, 2), &value)) {
      return false;
    }
    frame->data[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
  }
  return true;
}

void configureSerial(QSerialPort* port, const QString& name, int baudRate) {
  port->setPortName(name);
  port->setBaudRate(baudRate);
  port->setDataBits(QSerialPort::Data8);
  port->setParity(QSerialPort::NoParity);
  port->setStopBits(QSerialPort::OneStop);
  port->setFlowControl(QSerialPort::NoFlowControl);
}

void writeControl(QSerialPort* port, const QByteArray& bytes) {
  port->write(bytes);
  port->waitForBytesWritten(80);
}

QVector<CanFrame> readFrames(QSerialPort* port, int timeoutMs) {
  QVector<CanFrame> frames;
  QByteArray buffer = port->readAll();
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
    if (remaining > 0) {
      port->waitForReadyRead(std::min(remaining, 40));
    }
    buffer.append(port->readAll());
    while (true) {
      const int end = buffer.indexOf('\r');
      if (end < 0) {
        break;
      }
      const QByteArray line = buffer.left(end);
      buffer.remove(0, end + 1);
      CanFrame frame;
      if (parseFrame(line, &frame)) {
        frames.append(frame);
      }
    }
  }
  return frames;
}

CanFrame canOpenRead(std::uint8_t nodeId, std::uint16_t index) {
  CanFrame frame;
  frame.id = 0x600U + nodeId;
  frame.data[0] = 0x40;
  frame.data[1] = static_cast<std::uint8_t>(index & 0xFFU);
  frame.data[2] = static_cast<std::uint8_t>(index >> 8U);
  return frame;
}

struct PortProbe {
  QString port;
  int serialBaudRate = 115200;
  int canBitrate = 0;
  QVector<int> servoIds;
  int clampNodeId = 0;
};

PortProbe probeCanPort(const QString& name, int serialBaudRate, int canBitrate) {
  PortProbe result;
  result.port = name;
  result.serialBaudRate = serialBaudRate;
  result.canBitrate = canBitrate;

  QSerialPort port;
  configureSerial(&port, name, serialBaudRate);
  if (!port.open(QIODevice::ReadWrite)) {
    return result;
  }

  writeControl(&port, "C\r");
  writeControl(&port, bitrateCommand(canBitrate));
  writeControl(&port, "M0\r");
  writeControl(&port, "A0\r");
  writeControl(&port, "O\r");
  port.clear(QSerialPort::AllDirections);

  QByteArray queries;
  for (int motorId = 1; motorId <= 32; ++motorId) {
    queries.append(encodeFrame(ServoProtocol::statusQuery(
        static_cast<std::uint8_t>(motorId))));
  }
  port.write(queries);
  port.waitForBytesWritten(250);
  const QVector<CanFrame> servoResponses = readFrames(&port, 300);
  for (const CanFrame& frame : servoResponses) {
    const auto feedback = ServoProtocol::parseStatus(frame);
    if (feedback.has_value() && !result.servoIds.contains(feedback->motorId)) {
      result.servoIds.append(feedback->motorId);
    }
  }

  QByteArray canOpenQueries;
  for (int nodeId = 1; nodeId <= 127; ++nodeId) {
    canOpenQueries.append(encodeFrame(canOpenRead(static_cast<std::uint8_t>(nodeId),
                                                  0x1000)));
  }
  port.write(canOpenQueries);
  port.waitForBytesWritten(500);
  const QVector<CanFrame> canOpenResponses = readFrames(&port, 500);
  for (const CanFrame& frame : canOpenResponses) {
    if (frame.id < 0x580U || frame.id > 0x5FFU ||
        frame.data[1] != 0x00 || frame.data[2] != 0x10 || frame.data[3] != 0x00) {
      continue;
    }
    if (frame.data[0] == 0x43 || frame.data[0] == 0x4B ||
        frame.data[0] == 0x4F || frame.data[0] == 0x47) {
      result.clampNodeId = static_cast<int>(frame.id - 0x580U);
      break;
    }
  }
  writeControl(&port, "C\r");
  port.close();
  return result;
}

}  // namespace

bool HardwareDiscovery::probeImuPort(const QString& portName, int baudRate, int timeoutMs) {
  QSerialPort port;
  configureSerial(&port, portName, baudRate);
  if (!port.open(QIODevice::ReadWrite)) {
    return false;
  }
  const auto command = Rim302FrameParser::continuousModeCommand(1);
  port.write(reinterpret_cast<const char*>(command.data()),
             static_cast<qint64>(command.size()));
  port.waitForBytesWritten(100);

  Rim302FrameParser parser;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
    if (remaining > 0) {
      port.waitForReadyRead(std::min(remaining, 50));
    }
    const QByteArray raw = port.readAll();
    if (!raw.isEmpty() &&
        !parser.consume(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                        static_cast<std::size_t>(raw.size())).empty()) {
      port.close();
      return true;
    }
  }
  port.close();
  return false;
}

HardwareDetectionResult HardwareDiscovery::probeCanPorts(const QString& excludedPort) {
  HardwareDetectionResult result;
  const QStringList excludedPorts = excludedPort.trimmed().split(
      QRegularExpression(QStringLiteral("[,;\\s]+")), QString::SkipEmptyParts);
  for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
    bool excluded = false;
    for (const QString& port : excludedPorts) {
      if (info.portName().compare(port, Qt::CaseInsensitive) == 0) {
        excluded = true;
        break;
      }
    }
    if (excluded) {
      continue;
    }
    bool portComplete = false;
    for (const int serialBaudRate : kSerialBauds) {
      for (const int canBitrate : kCanBitrates) {
        PortProbe probe = probeCanPort(info.portName(), serialBaudRate, canBitrate);
        if (probe.servoIds.size() < 2 && probe.clampNodeId == 0) {
          continue;
        }

        if (probe.servoIds.size() >= 2 && !result.wheelDetected) {
          result.wheelDetected = true;
          result.wheelCanPort = probe.port;
          result.wheelSerialBaudRate = probe.serialBaudRate;
          result.wheelCanBitrate = probe.canBitrate;
          std::sort(probe.servoIds.begin(), probe.servoIds.end());
          if (probe.servoIds.contains(1) && probe.servoIds.contains(2)) {
            result.wheelMotorIds = {1, 2};
          } else {
            result.wheelMotorIds = {probe.servoIds[0], probe.servoIds[1]};
          }
          result.details.append(CRAWLING_TEXT("\xE8\xBD\xAE""\xE6\xAF\x82""\xE4\xBC\xBA""\xE6\x9C\x8D""\xEF\xBC\x9A""%1\xEF\xBC\x8C""CAN %2\xEF\xBC\x8C""\xE8\x8A\x82""\xE7\x82\xB9"" %3/%4")
                                    .arg(result.wheelCanPort)
                                    .arg(result.wheelCanBitrate)
                                    .arg(result.wheelMotorIds[0])
                                    .arg(result.wheelMotorIds[1]));
        }
        if (probe.clampNodeId > 0 && !result.clampDetected) {
          result.clampDetected = true;
          result.clampCanPort = probe.port;
          result.clampSerialBaudRate = probe.serialBaudRate;
          result.clampCanBitrate = probe.canBitrate;
          result.clampNodeId = probe.clampNodeId;
          result.details.append(CRAWLING_TEXT("\xE5\xA4\xB9""\xE5\xAD\x90"" CANopen\xEF\xBC\x9A""%1\xEF\xBC\x8C""CAN %2\xEF\xBC\x8C""\xE8\x8A\x82""\xE7\x82\xB9"" %3")
                                    .arg(result.clampCanPort)
                                    .arg(result.clampCanBitrate)
                                    .arg(result.clampNodeId));
        }
        portComplete = true;
        break;
      }
      if (portComplete) {
        break;
      }
    }
    if (result.wheelDetected && result.clampDetected) {
      break;
    }
  }
  return result;
}

}  // namespace crawling
