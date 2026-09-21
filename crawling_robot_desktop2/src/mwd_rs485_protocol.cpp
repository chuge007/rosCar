#include "mwd_rs485_protocol.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

std::int16_t readInt16LittleEndian(const QByteArray& data, int offset) {
  const auto low = static_cast<std::uint8_t>(data.at(offset));
  const auto high = static_cast<std::uint8_t>(data.at(offset + 1));
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(low) |
                                   (static_cast<std::uint16_t>(high) << 8U));
}

}  // namespace

QByteArray MwdRs485Protocol::command(std::uint8_t command,
                                     std::uint8_t motorId,
                                     const QByteArray& data) {
  if (motorId < 1 || motorId > 32 || data.size() > 100) {
    return {};
  }

  QByteArray frame;
  frame.reserve(5 + data.size() + (data.isEmpty() ? 0 : 1));
  frame.append(static_cast<char>(kFrameHeader));
  frame.append(static_cast<char>(command));
  frame.append(static_cast<char>(motorId));
  frame.append(static_cast<char>(data.size()));
  frame.append(static_cast<char>(checksum(frame)));
  if (!data.isEmpty()) {
    frame.append(data);
    frame.append(static_cast<char>(checksum(data)));
  }
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
  data.reserve(4);
  for (int index = 0; index < 4; ++index) {
    data.append(static_cast<char>((raw >> (index * 8)) & 0xFFU));
  }
  return command(kSpeedClosedLoop, motorId, data);
}

QByteArray MwdRs485Protocol::multiTurnAngleQuery(std::uint8_t motorId) {
  return command(kReadMultiTurnAngle, motorId);
}

QByteArray MwdRs485Protocol::multiTurnPositionCommand(
    std::uint8_t motorId, std::int64_t angleHundredthDegree,
    double maximumSpeedDps) {
  QByteArray data;
  data.reserve(12);
  const auto rawAngle = static_cast<std::uint64_t>(angleHundredthDegree);
  for (int index = 0; index < 8; ++index) {
    data.append(static_cast<char>((rawAngle >> (index * 8)) & 0xFFU));
  }
  const double scaledSpeed = std::clamp(
      std::round(std::isfinite(maximumSpeedDps) ? maximumSpeedDps * 100.0
                                                : 0.0),
      0.0, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  const auto rawSpeed = static_cast<std::uint32_t>(scaledSpeed);
  for (int index = 0; index < 4; ++index) {
    data.append(static_cast<char>((rawSpeed >> (index * 8)) & 0xFFU));
  }
  return command(kMultiTurnPositionClosedLoop, motorId, data);
}

QByteArray MwdRs485Protocol::brakeCommand(std::uint8_t motorId, bool apply) {
  QByteArray data;
  data.append(static_cast<char>(apply ? kBrakeApplied : kBrakeReleased));
  return command(kBrakeControl, motorId, data);
}

QByteArray MwdRs485Protocol::brakeStatusQuery(std::uint8_t motorId) {
  QByteArray data;
  data.append(static_cast<char>(kBrakeRead));
  return command(kBrakeControl, motorId, data);
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
    if (receiveBuffer->size() < 5) {
      return false;
    }

    const auto motorId = static_cast<std::uint8_t>(receiveBuffer->at(2));
    const auto dataLength = static_cast<std::uint8_t>(receiveBuffer->at(3));
    const int totalLength = 5 + dataLength + (dataLength == 0 ? 0 : 1);
    if (motorId < 1 || motorId > 32 || dataLength > 100 ||
        checksum(receiveBuffer->left(4)) !=
            static_cast<std::uint8_t>(receiveBuffer->at(4))) {
      receiveBuffer->remove(0, 1);
      continue;
    }
    if (receiveBuffer->size() < totalLength) {
      return false;
    }

    const QByteArray data = receiveBuffer->mid(5, dataLength);
    if (dataLength > 0 &&
        checksum(data) !=
            static_cast<std::uint8_t>(receiveBuffer->at(totalLength - 1))) {
      receiveBuffer->remove(0, 1);
      continue;
    }

    frame->command = static_cast<std::uint8_t>(receiveBuffer->at(1));
    frame->motorId = motorId;
    frame->data = data;
    receiveBuffer->remove(0, totalLength);
    return true;
  }
}

std::optional<MwdMotorFeedback> MwdRs485Protocol::parseMotorFeedback(
    const MwdRs485Frame& frame) {
  if ((frame.command != kReadStatus2 && frame.command != kSpeedClosedLoop &&
       frame.command != kMultiTurnPositionClosedLoop) ||
      frame.motorId < 1 || frame.motorId > 32 || frame.data.size() != 7) {
    return std::nullopt;
  }

  MwdMotorFeedback feedback;
  feedback.motorId = frame.motorId;
  feedback.temperatureC = static_cast<std::int8_t>(
      static_cast<std::uint8_t>(frame.data.at(0)));
  feedback.controlValue = readInt16LittleEndian(frame.data, 1);
  feedback.speedDps = readInt16LittleEndian(frame.data, 3);
  feedback.encoder = static_cast<std::uint16_t>(
      readInt16LittleEndian(frame.data, 5));
  return feedback;
}

std::optional<std::int64_t> MwdRs485Protocol::parseMultiTurnAngle(
    const MwdRs485Frame& frame) {
  if (frame.command != kReadMultiTurnAngle || frame.motorId < 1 ||
      frame.motorId > 32 || frame.data.size() != 8) {
    return std::nullopt;
  }
  std::uint64_t raw = 0;
  for (int index = 0; index < 8; ++index) {
    raw |= static_cast<std::uint64_t>(
               static_cast<std::uint8_t>(frame.data.at(index)))
           << (index * 8);
  }
  if (raw <= static_cast<std::uint64_t>(
                 std::numeric_limits<std::int64_t>::max())) {
    return static_cast<std::int64_t>(raw);
  }
  const std::uint64_t magnitude = (~raw) + 1U;
  if (magnitude == (std::uint64_t{1} << 63U)) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return -static_cast<std::int64_t>(magnitude);
}

std::optional<bool> MwdRs485Protocol::parseBrakeApplied(
    const MwdRs485Frame& frame) {
  if (frame.command != kBrakeControl || frame.motorId < 1 ||
      frame.motorId > 32 || frame.data.size() != 1) {
    return std::nullopt;
  }
  const auto state = static_cast<std::uint8_t>(frame.data.at(0));
  if (state == kBrakeApplied) {
    return true;
  }
  if (state == kBrakeReleased) {
    return false;
  }
  return std::nullopt;
}

std::uint8_t MwdRs485Protocol::checksum(const QByteArray& bytes) {
  std::uint8_t sum = 0;
  for (const char byte : bytes) {
    sum = static_cast<std::uint8_t>(sum + static_cast<std::uint8_t>(byte));
  }
  return sum;
}

}  // namespace crawling
