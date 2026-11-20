#include "mwd_can_protocol.h"

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

}  // namespace

CanFrame MwdCanProtocol::simple(std::uint8_t motorId, std::uint8_t command) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = command;
  return frame;
}

CanFrame MwdCanProtocol::speedCommand(std::uint8_t motorId,
                                      double outputSpeedDps) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kSpeedClosedLoop;
  const double bounded = std::clamp(
      std::round(std::isfinite(outputSpeedDps) ? outputSpeedDps * 100.0 : 0.0),
      static_cast<double>(std::numeric_limits<std::int32_t>::min()),
      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  writeInt32LittleEndian(frame.data, 4, static_cast<std::int32_t>(bounded));
  return frame;
}

CanFrame MwdCanProtocol::brakeCommand(std::uint8_t motorId, bool release) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kBrakeControl;
  frame.data[1] = release ? 0x01 : 0x00;
  return frame;
}

CanFrame MwdCanProtocol::brakeStatusQuery(std::uint8_t motorId) {
  CanFrame frame;
  frame.id = kCommandIdBase + motorId;
  frame.data[0] = kBrakeControl;
  frame.data[1] = kBrakeReadStatus;
  return frame;
}

std::optional<bool> MwdCanProtocol::parseBrakeStatus(const CanFrame& frame) {
  if (frame.data[0] != kBrakeControl || frame.data[1] > 1) {
    return std::nullopt;
  }
  return frame.data[1] != 0;
}

}  // namespace crawling
