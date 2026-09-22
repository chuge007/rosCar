#pragma once

#include "drive_types.h"

#include <optional>

namespace crawling {

struct ServoFeedback {
  std::uint8_t motorId = 0;
  int temperatureC = 0;
  double torqueCurrentA = 0.0;
  double outputSpeedDps = 0.0;
  double outputAngleDeg = 0.0;
};

struct ServoStatus {
  std::uint8_t motorId = 0;
  int temperatureC = 0;
  std::uint16_t errorState = 0;
};

class ServoProtocol final {
 public:
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  static constexpr std::uint8_t kStatusQuery = 0x9A;
  static constexpr std::uint8_t kMotorStop = 0x81;
  static constexpr std::uint8_t kSystemReset = 0x80;
  static constexpr std::uint32_t kCommandIdBase = 0x140;
  static constexpr std::uint32_t kFeedbackIdBase = 0x240;

  static CanFrame speedCommand(std::uint8_t motorId, double outputSpeedDps);
  static CanFrame statusQuery(std::uint8_t motorId);
  static CanFrame stopCommand(std::uint8_t motorId);
  static CanFrame systemResetCommand(std::uint8_t motorId);
  static std::optional<ServoFeedback> parseFeedback(const CanFrame& frame);
  static std::optional<ServoStatus> parseStatus(const CanFrame& frame);
};

}  // namespace crawling
