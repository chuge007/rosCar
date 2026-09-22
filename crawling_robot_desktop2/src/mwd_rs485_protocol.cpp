#include "mwd_rs485_protocol.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

constexpr int kFrameLength = 13;
constexpr int kFrameWithoutCrcLength = 11;
constexpr int kDataLength = 8;

std::int16_t readInt16LittleEndian(const QByteArray& data, int offset) {
  const auto low = static_cast<std::uint8_t>(data.at(offset));
  const auto high = static_cast<std::uint8_t>(data.at(offset + 1));
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(low) |
                                   (static_cast<std::uint16_t>(high) << 8U));
}

std::int32_t readInt32LittleEndian(const QByteArray& data, int offset) {
  std::uint32_t raw = 0;
  for (int index = 0; index < 4; ++index) {
    raw |= static_cast<std::uint32_t>(
               static_cast<std::uint8_t>(data.at(offset + index)))
           << (index * 8);
  }
  return static_cast<std::int32_t>(raw);
}

void appendUInt32LittleEndian(QByteArray* data, std::uint32_t value) {
  for (int index = 0; index < 4; ++index) {
    data->append(static_cast<char>((value >> (index * 8)) & 0xFFU));
  }
}

}  // namespace

QByteArray MwdRs485Protocol::command(std::uint8_t command,
                                     std::uint8_t motorId,
                                     const QByteArray& data) {
  if (motorId < 1 || motorId > 32 || data.size() > (kDataLength - 1)) {
    return {};
  }

  QByteArray frame;
  frame.reserve(kFrameLength);
  frame.append(static_cast<char>(kFrameHeader));
  frame.append(static_cast<char>(motorId));
  frame.append(static_cast<char>(kDataLength));
  frame.append(static_cast<char>(command));
  frame.append(data);
  while (frame.size() < kFrameWithoutCrcLength) {
    frame.append('\0');
  }

  // V3.8 specifies CRC16 with the low byte first but does not document the
  // polynomial or initial value. Keep the selected Modbus-compatible
  // parameters centralized here until a vendor reference frame is available.
  const std::uint16_t crc = crc16(frame);
  frame.append(static_cast<char>(crc & 0xFFU));
  frame.append(static_cast<char>((crc >> 8U) & 0xFFU));
  return frame;
}

QByteArray MwdRs485Protocol::speedCommand(std::uint8_t motorId,
                                          double speedDps) {
  const double scaled = std::clamp(
      std::round(std::isfinite(speedDps) ? speedDps * 100.0 : 0.0),
      static_cast<double>(std::numeric_limits<std::int32_t>::min()),
      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  const auto raw = static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled));

  QByteArray data;
  data.reserve(7);
  data.append('\0');
  data.append('\0');
  data.append('\0');
  appendUInt32LittleEndian(&data, raw);
  return command(kSpeedClosedLoop, motorId, data);
}

QByteArray MwdRs485Protocol::multiTurnAngleQuery(std::uint8_t motorId) {
  return command(kReadMultiTurnAngle, motorId);
}

QByteArray MwdRs485Protocol::multiTurnEncoderQuery(std::uint8_t motorId) {
  return command(kReadMultiTurnEncoder, motorId);
}

QByteArray MwdRs485Protocol::multiTurnPositionCommand(
    std::uint8_t motorId, std::int64_t angleHundredthDegree,
    double maximumSpeedDps) {
  const auto boundedAngle = std::clamp(
      angleHundredthDegree,
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()));
  const double boundedSpeed = std::clamp(
      std::round(std::isfinite(maximumSpeedDps) ? maximumSpeedDps : 0.0),
      0.0, static_cast<double>(std::numeric_limits<std::uint16_t>::max()));
  const auto rawAngle =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(boundedAngle));
  const auto rawSpeed = static_cast<std::uint16_t>(boundedSpeed);

  QByteArray data;
  data.reserve(7);
  data.append('\0');
  data.append(static_cast<char>(rawSpeed & 0xFFU));
  data.append(static_cast<char>((rawSpeed >> 8U) & 0xFFU));
  appendUInt32LittleEndian(&data, rawAngle);
  return command(kMultiTurnPositionClosedLoop, motorId, data);
}

bool MwdRs485Protocol::takeFrame(QByteArray* receiveBuffer,
                                 MwdRs485Frame* frame) {
  if (receiveBuffer == nullptr || frame == nullptr) {
    return false;
  }

  while (true) {
    const int header = receiveBuffer->indexOf(static_cast<char>(kFrameHeader));
    if (header < 0) {
      receiveBuffer->clear();
      return false;
    }
    if (header > 0) {
      receiveBuffer->remove(0, header);
    }
    if (receiveBuffer->size() < kFrameLength) {
      return false;
    }

    const auto motorId = static_cast<std::uint8_t>(receiveBuffer->at(1));
    const auto dataLength = static_cast<std::uint8_t>(receiveBuffer->at(2));
    const std::uint16_t receivedCrc =
        static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(receiveBuffer->at(11))) |
        (static_cast<std::uint16_t>(
             static_cast<std::uint8_t>(receiveBuffer->at(12)))
         << 8U);
    if (motorId < 1 || motorId > 32 || dataLength != kDataLength ||
        crc16(receiveBuffer->left(kFrameWithoutCrcLength)) != receivedCrc) {
      receiveBuffer->remove(0, 1);
      continue;
    }

    frame->data = receiveBuffer->mid(3, kDataLength);
    frame->command = static_cast<std::uint8_t>(frame->data.at(0));
    frame->motorId = motorId;
    receiveBuffer->remove(0, kFrameLength);
    return true;
  }
}

std::optional<MwdMotorFeedback> MwdRs485Protocol::parseMotorFeedback(
    const MwdRs485Frame& frame) {
  if ((frame.command != kReadStatus2 &&
       frame.command != kSpeedClosedLoop &&
       frame.command != kMultiTurnPositionClosedLoop) ||
      frame.motorId < 1 || frame.motorId > 32 ||
      frame.data.size() != kDataLength ||
      static_cast<std::uint8_t>(frame.data.at(0)) != frame.command) {
    return std::nullopt;
  }

  MwdMotorFeedback feedback;
  feedback.motorId = frame.motorId;
  feedback.temperatureC = static_cast<std::int8_t>(
      static_cast<std::uint8_t>(frame.data.at(1)));
  feedback.controlValue = readInt16LittleEndian(frame.data, 2);
  feedback.speedDps = readInt16LittleEndian(frame.data, 4);
  feedback.outputAngleDeg = readInt16LittleEndian(frame.data, 6);
  return feedback;
}

std::optional<std::int64_t> MwdRs485Protocol::parseMultiTurnAngle(
    const MwdRs485Frame& frame) {
  if (frame.command != kReadMultiTurnAngle || frame.motorId < 1 ||
      frame.motorId > 32 || frame.data.size() != kDataLength ||
      static_cast<std::uint8_t>(frame.data.at(0)) != frame.command) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(readInt32LittleEndian(frame.data, 4));
}

std::optional<std::int32_t> MwdRs485Protocol::parseMultiTurnEncoderPosition(
    const MwdRs485Frame& frame) {
  if (frame.command != kReadMultiTurnEncoder || frame.motorId < 1 ||
      frame.motorId > 32 || frame.data.size() != kDataLength ||
      static_cast<std::uint8_t>(frame.data.at(0)) != frame.command) {
    return std::nullopt;
  }
  return readInt32LittleEndian(frame.data, 4);
}

std::uint16_t MwdRs485Protocol::crc16(const QByteArray& bytes) {
  std::uint16_t crc = 0xFFFFU;
  for (const char byte : bytes) {
    crc ^= static_cast<std::uint8_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = static_cast<std::uint16_t>((crc >> 1U) ^ 0xA001U);
      } else {
        crc = static_cast<std::uint16_t>(crc >> 1U);
      }
    }
  }
  return crc;
}

}  // namespace crawling
