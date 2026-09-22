#include "servo_protocol.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

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

}  // namespace

CanFrame ServoProtocol::speedCommand(std::uint8_t motorId, double outputSpeedDps) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kSpeedClosedLoop;
  const double bounded = std::clamp(std::round(std::isfinite(outputSpeedDps)
                                                    ? outputSpeedDps * 100.0
                                                    : 0.0),
                                      static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                                      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  writeInt32LittleEndian(frame.data, 4, static_cast<std::int32_t>(bounded));
  return frame;
}

CanFrame ServoProtocol::stopCommand(std::uint8_t motorId) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kMotorStop;
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

std::optional<ServoFeedback> ServoProtocol::parseFeedback(const CanFrame& frame) {
  if (frame.id <= kFeedbackIdBase || frame.id > kFeedbackIdBase + 32 ||
      (frame.data[0] != 0x81 && frame.data[0] != 0x9C && frame.data[0] != 0xA1 &&
       frame.data[0] != 0xA2 && frame.data[0] != 0xA4)) {
    return std::nullopt;
  }
  ServoFeedback feedback;
  feedback.motorId = static_cast<std::uint8_t>(frame.id - kFeedbackIdBase);
  feedback.temperatureC = static_cast<std::int8_t>(frame.data[1]);
  feedback.torqueCurrentA = readInt16LittleEndian(frame.data, 2) * 0.01;
  feedback.outputSpeedDps = readInt16LittleEndian(frame.data, 4);
  feedback.outputAngleDeg = readInt16LittleEndian(frame.data, 6);
  return feedback;
}

std::optional<ServoStatus> ServoProtocol::parseStatus(const CanFrame& frame) {
  if (frame.id <= kFeedbackIdBase || frame.id > kFeedbackIdBase + 32 ||
      frame.data[0] != kStatusQuery) {
    return std::nullopt;
  }
  ServoStatus status;
  status.motorId = static_cast<std::uint8_t>(frame.id - kFeedbackIdBase);
  status.temperatureC = static_cast<std::int8_t>(frame.data[1]);
  status.errorState = static_cast<std::uint16_t>(frame.data[6]) |
                      (static_cast<std::uint16_t>(frame.data[7]) << 8U);
  return status;
}

}  // namespace crawling
