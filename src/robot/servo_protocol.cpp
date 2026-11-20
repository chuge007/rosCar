#include "servo_protocol.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

bool isServoReplyId(std::uint32_t id) {
  return (id > ServoProtocol::kCommandIdBase &&
          id <= ServoProtocol::kCommandIdBase + 32) ||
      (id > ServoProtocol::kFeedbackIdBase &&
       id <= ServoProtocol::kFeedbackIdBase + 32);
}

std::uint8_t servoReplyMotorId(std::uint32_t id) {
  return static_cast<std::uint8_t>(
      id > ServoProtocol::kFeedbackIdBase
          ? id - ServoProtocol::kFeedbackIdBase
          : id - ServoProtocol::kCommandIdBase);
}

void writeInt32LittleEndian(std::array<std::uint8_t, 8>& data, int offset,
                            std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  for (int index = 0; index < 4; ++index) {
    data[static_cast<std::size_t>(offset + index)] =
        static_cast<std::uint8_t>((raw >> (8 * index)) & 0xFFU);
  }
}

std::int16_t readInt16LittleEndian(const std::array<std::uint8_t, 8>& data,
                                   int offset) {
  const auto raw = static_cast<std::uint16_t>(data[static_cast<std::size_t>(offset)]) |
                   (static_cast<std::uint16_t>(data[static_cast<std::size_t>(offset + 1)]) << 8U);
  return static_cast<std::int16_t>(raw);
}

std::int32_t readInt32LittleEndian(const std::array<std::uint8_t, 8>& data,
                                   int offset) {
  std::uint32_t raw = 0;
  for (int index = 0; index < 4; ++index) {
    raw |= static_cast<std::uint32_t>(
               data[static_cast<std::size_t>(offset + index)])
           << (8 * index);
  }
  return static_cast<std::int32_t>(raw);
}

}  // namespace

CanFrame ServoProtocol::speedCommand(std::uint8_t motorId, double outputSpeedDps) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kSpeedClosedLoop;
  // The 0xA2 payload stores a closed-loop output speed in 0.01 deg/s units.
  const double bounded = std::clamp(std::round(std::isfinite(outputSpeedDps)
                                                    ? outputSpeedDps * 100.0
                                                    : 0.0),
                                      static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                                      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  // Bytes 4..7 are the whole int32 speed. Never touch data[5] (an earlier
  // "acceleration tier" write corrupted every speed command) or data[3]: the
  // integrated build leaves all reserved bytes zero for both motors.
  writeInt32LittleEndian(frame.data, 4, static_cast<std::int32_t>(bounded));
  return frame;
}

CanFrame ServoProtocol::stopCommand(std::uint8_t motorId) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kMotorStop;
  return frame;
}

// 官方协议 2.27/2.28：0x77 抱闸释放、0x78 抱闸锁死，数据域全 0，
// 驱动器把收到的帧原样回复。早期用 0x8C 是错的（不在命令表里，只回显不执行）。
CanFrame ServoProtocol::brakeCommand(std::uint8_t motorId, bool release) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = release ? kBrakeRelease : kBrakeLock;
  return frame;
}

CanFrame ServoProtocol::systemResetCommand(std::uint8_t motorId) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kSystemReset;
  return frame;
}

CanFrame ServoProtocol::statusQuery(std::uint8_t motorId) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kStatusQuery;
  return frame;
}

CanFrame ServoProtocol::brakeStatusQuery(std::uint8_t motorId) {
  // 抱闸状态在 0x9A 状态1 的 Data[3]（RlyCtrlRslt），没有独立的查询命令。
  return statusQuery(motorId);
}

std::optional<ServoFeedback> ServoProtocol::parseFeedback(const CanFrame& frame) {
  if (!isServoReplyId(frame.id) ||
      (frame.data[0] != 0x81 && frame.data[0] != 0x9C && frame.data[0] != 0xA1 &&
       frame.data[0] != 0xA2 && frame.data[0] != 0xA4)) {
    return std::nullopt;
  }
  ServoFeedback feedback;
  feedback.motorId = servoReplyMotorId(frame.id);
  feedback.temperatureC = static_cast<std::int8_t>(frame.data[1]);
  feedback.torqueCurrentA = readInt16LittleEndian(frame.data, 2) * 0.01;
  feedback.outputSpeedDps = readInt16LittleEndian(frame.data, 4);
  feedback.outputAngleDeg = readInt16LittleEndian(frame.data, 6);
  return feedback;
}

std::optional<std::int32_t> ServoProtocol::parseMultiTurnEncoderPosition(
    const CanFrame& frame) {
  if (!isServoReplyId(frame.id) ||
      frame.data[0] != 0x60) {
    return std::nullopt;
  }
  return readInt32LittleEndian(frame.data, 4);
}

std::optional<std::int32_t> ServoProtocol::parseMultiTurnAngle(
    const CanFrame& frame) {
  if (!isServoReplyId(frame.id) ||
      frame.data[0] != 0x92) {
    return std::nullopt;
  }
  return readInt32LittleEndian(frame.data, 4);
}

std::optional<ServoStatus> ServoProtocol::parseStatus(const CanFrame& frame) {
  if (!isServoReplyId(frame.id) ||
      frame.data[0] != kStatusQuery) {
    return std::nullopt;
  }
  ServoStatus status;
  status.motorId = servoReplyMotorId(frame.id);
  status.temperatureC = static_cast<std::int8_t>(frame.data[1]);
  status.brakeReleased = frame.data[3] != 0;
  status.errorState = static_cast<std::uint16_t>(frame.data[6]) |
                      (static_cast<std::uint16_t>(frame.data[7]) << 8U);
  return status;
}

std::optional<bool> ServoProtocol::parseBrakeStatus(const CanFrame& frame) {
  if (!isServoReplyId(frame.id)) {
    return std::nullopt;
  }
  // 0x77/0x78 的回复就是发送帧的回显，代表指令已下发；
  // 0x9A 的 Data[3] 才是驱动器实际执行的抱闸状态。
  if (frame.data[0] == kBrakeRelease) {
    return true;
  }
  if (frame.data[0] == kBrakeLock) {
    return false;
  }
  if (frame.data[0] == kStatusQuery) {
    return frame.data[3] != 0;
  }
  return std::nullopt;
}

}  // namespace crawling
