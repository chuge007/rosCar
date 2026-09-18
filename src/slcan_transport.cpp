#include "slcan_transport.h"

#include <QSerialPortInfo>
#include "utf8_compat.h"

namespace crawling {
namespace {

bool parseHex(const QByteArray& text, int* result) {
  bool okay = false;
  const int value = text.toInt(&okay, 16);
  if (okay) {
    *result = value;
  }
  return okay;
}

}  // namespace

SlcanTransport::SlcanTransport(QObject* parent)
    : QObject(parent), port_(this), transmitTimer_(this) {
  connect(&port_, &QSerialPort::readyRead, this, &SlcanTransport::readAvailable);
  connect(&port_, &QSerialPort::errorOccurred, this, &SlcanTransport::onSerialError);
  transmitTimer_.setInterval(3);
  connect(&transmitTimer_, &QTimer::timeout, this, &SlcanTransport::transmitNextFrame);
}

bool SlcanTransport::isOpen() const {
  return port_.isOpen();
}

void SlcanTransport::open(const DriveSettings& settings) {
  close();
  if (settings.serialPort.trimmed().isEmpty()) {
    emit transportError(CRAWLING_TEXT("\xE8\xBF\x9E""\xE6\x8E\xA5""\xE5\x89\x8D""\xE8\xAF\xB7""\xE9\x80\x89""\xE6\x8B\xA9""\xE4\xB8\xB2""\xE5\x8F\xA3""\xE3\x80\x82"""));
    return;
  }
  const QByteArray bitrate = bitrateCommand(settings.canBitrate);
  if (bitrate.isEmpty()) {
    emit transportError(CRAWLING_TEXT("\xE6\x89\x80""\xE9\x80\x89"" CAN \xE6\xB3\xA2""\xE7\x89\xB9""\xE7\x8E\x87""\xE4\xB8\x8D""\xE5\x8F\x97"" SLCAN \xE6\x94\xAF""\xE6\x8C\x81""\xE3\x80\x82"""));
    return;
  }

  port_.setPortName(settings.serialPort.trimmed());
  port_.setBaudRate(settings.serialBaudRate);
  port_.setDataBits(QSerialPort::Data8);
  port_.setParity(QSerialPort::NoParity);
  port_.setStopBits(QSerialPort::OneStop);
  port_.setFlowControl(QSerialPort::NoFlowControl);
  if (!port_.open(QIODevice::ReadWrite)) {
    emit transportError(CRAWLING_TEXT("\xE6\x97\xA0""\xE6\xB3\x95""\xE6\x89\x93""\xE5\xBC\x80"" %1\xEF\xBC\x9A""%2")
                            .arg(settings.serialPort, port_.errorString()));
    return;
  }

  receiveBuffer_.clear();
  // M0/A0 make the adapter discard stale retry traffic. The controller sends
  // a complete left/right command pair every cycle, so replaying an old frame
  // is more dangerous than dropping it.
  writeControl("C\r");
  writeControl(bitrate);
  writeControl("M0\r");
  writeControl("A0\r");
  writeControl("O\r");
  emit activity(QStringLiteral("SLCAN opened: port=%1, serial=%2, CAN=%3")
                    .arg(settings.serialPort).arg(settings.serialBaudRate).arg(settings.canBitrate));
  emit connectionChanged(true, CRAWLING_TEXT("\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5"" %1").arg(settings.serialPort));
}

void SlcanTransport::close() {
  transmitTimer_.stop();
  // A paired safety command writes its first frame immediately and queues the
  // second one for pacing. Drain that queue before closing the serial port so
  // disconnect/reconnect cannot leave one motor without the stop frame.
  if (port_.isOpen()) {
    while (!transmitQueue_.isEmpty()) {
      transmitNextFrame();
      if (!port_.waitForBytesWritten(50)) {
        break;
      }
    }
  }
  transmitQueue_.clear();
  receiveBuffer_.clear();
  if (!port_.isOpen()) {
    return;
  }
  port_.clear(QSerialPort::AllDirections);
  port_.close();
  emit connectionChanged(false, CRAWLING_TEXT("\xE4\xB8\xB2""\xE5\x8F\xA3""\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80"""));
}

bool SlcanTransport::sendDrivePair(const CanFrame& left, const CanFrame& right, bool urgent) {
  return writeFrames({left, right}, urgent);
}

bool SlcanTransport::sendFrame(const CanFrame& frame, bool urgent) {
  return writeFrames({frame}, urgent);
}

QByteArray SlcanTransport::bitrateCommand(int bitrate) {
  switch (bitrate) {
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

QByteArray SlcanTransport::encodeFrame(const CanFrame& frame) {
  if (frame.id > 0x7FFU) {
    return {};
  }
  QByteArray command = CRAWLING_TEXT("t%1%2")
                           .arg(frame.id, 3, 16, QLatin1Char('0'))
                           .arg(8, 1, 16, QLatin1Char('0'))
                           .toUpper()
                           .toLatin1();
  command[0] = 't';
  for (std::uint8_t byte : frame.data) {
    command.append(CRAWLING_TEXT("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper().toLatin1());
  }
  command.append('\r');
  return command;
}

void SlcanTransport::writeControl(const QByteArray& bytes) {
  if (port_.write(bytes) != bytes.size()) {
    emit transportError(CRAWLING_TEXT("\xE4\xB8\xB2""\xE5\x8F\xA3""\xE9\x85\x8D""\xE7\xBD\xAE""\xE5\x86\x99""\xE5\x85\xA5""\xE5\xA4\xB1""\xE8\xB4\xA5""\xEF\xBC\x9A""%1").arg(port_.errorString()));
  }
}

bool SlcanTransport::writeFrames(const QVector<CanFrame>& frames, bool urgent) {
  if (!port_.isOpen()) {
    return false;
  }
  QVector<QByteArray> encoded;
  encoded.reserve(frames.size());
  for (const CanFrame& frame : frames) {
    const QByteArray one = encodeFrame(frame);
    if (one.isEmpty()) {
      emit transportError(CRAWLING_TEXT("\xE6\x8B\x92""\xE7\xBB\x9D""\xE5\x8F\x91""\xE9\x80\x81""\xE6\x97\xA0""\xE6\x95\x88"" CAN \xE5\xB8\xA7""\xE3\x80\x82"""));
      return false;
    }
    encoded.append(one);
  }

  // Some SLCAN firmware drops one command when multiple CR-terminated frames
  // arrive in the same USB packet. Queue each CAN frame as its own paced
  // serial write so a left/right pair is parsed as two commands. Do not clear
  // this queue for a newer speed command: a pair is enqueued left then right,
  // and clearing it between those writes can permanently drop one wheel's
  // frame. At 20 ms control periods the paced queue has enough bandwidth to
  // drain both wheels and the periodic status frames without accumulating.
  if (urgent) {
    transmitQueue_.clear();
    port_.clear(QSerialPort::Output);
  }
  for (const QByteArray& one : encoded) {
    transmitQueue_.enqueue(one);
  }
  transmitNextFrame();
  if (!transmitQueue_.isEmpty() && !transmitTimer_.isActive()) {
    transmitTimer_.start();
  }
  return true;
}

void SlcanTransport::transmitNextFrame() {
  if (!port_.isOpen()) {
    transmitTimer_.stop();
    transmitQueue_.clear();
    return;
  }
  if (transmitQueue_.isEmpty()) {
    transmitTimer_.stop();
    return;
  }

  const QByteArray encoded = transmitQueue_.dequeue();
  if (port_.write(encoded) != encoded.size()) {
    transmitQueue_.clear();
    transmitTimer_.stop();
    emit transportError(CRAWLING_TEXT("CAN \xE5\x91\xBD""\xE4\xBB\xA4""\xE5\x86\x99""\xE5\x85\xA5""\xE5\xA4\xB1""\xE8\xB4\xA5""\xEF\xBC\x9A""%1").arg(port_.errorString()));
  }
}

void SlcanTransport::readAvailable() {
  receiveBuffer_.append(port_.readAll());
  while (true) {
    const int end = receiveBuffer_.indexOf('\r');
    if (end < 0) {
      return;
    }
    const QByteArray line = receiveBuffer_.left(end);
    receiveBuffer_.remove(0, end + 1);
    parseLine(line);
  }
}

void SlcanTransport::parseLine(const QByteArray& line) {
  QByteArray normalized = line;
  while (!normalized.isEmpty() && (normalized.endsWith('\n') || normalized.endsWith('\r'))) {
    normalized.chop(1);
  }
  if (normalized.size() < 5 || (normalized.at(0) != 't' && normalized.at(0) != 'T')) {
    return;
  }
  int id = 0;
  int length = 0;
  const bool extended = normalized.at(0) == 'T';
  const int idDigits = extended ? 8 : 3;
  const int lengthOffset = 1 + idDigits;
  if (!parseHex(normalized.mid(1, idDigits), &id) ||
      !parseHex(normalized.mid(lengthOffset, 1), &length) ||
      length > 8 || normalized.size() != lengthOffset + 1 + length * 2) {
    emit activity(CRAWLING_TEXT("\xE5\xB7\xB2""\xE4\xB8\xA2""\xE5\xBC\x83""\xE6\xA0\xBC""\xE5\xBC\x8F""\xE9\x94\x99""\xE8\xAF\xAF""\xE7\x9A\x84"" SLCAN \xE8\xBE\x93""\xE5\x85\xA5""\xE3\x80\x82"""));
    return;
  }
  CanFrame frame;
  frame.id = static_cast<std::uint32_t>(id);
  for (int index = 0; index < length; ++index) {
    int value = 0;
    if (!parseHex(normalized.mid(lengthOffset + 1 + index * 2, 2), &value)) {
      emit activity(CRAWLING_TEXT("\xE5\xB7\xB2""\xE4\xB8\xA2""\xE5\xBC\x83""\xE6\x97\xA0""\xE6\xB3\x95""\xE8\xA7\xA3""\xE6\x9E\x90""\xE7\x9A\x84"" SLCAN \xE8\xBE\x93""\xE5\x85\xA5""\xE3\x80\x82"""));
      return;
    }
    frame.data[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
  }
  emit frameReceived(frame);
}

void SlcanTransport::onSerialError(QSerialPort::SerialPortError error) {
  if (error == QSerialPort::NoError) {
    return;
  }
  emit transportError(CRAWLING_TEXT("\xE4\xB8\xB2""\xE5\x8F\xA3""\xE9\x94\x99""\xE8\xAF\xAF""\xEF\xBC\x9A""%1").arg(port_.errorString()));
}

}  // namespace crawling
