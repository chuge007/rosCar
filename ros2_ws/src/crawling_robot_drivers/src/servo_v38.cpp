#include "crawling_robot_drivers/servo_v38.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace crawling_robot_drivers {
namespace {

std::int32_t toSpeedControlValue(double output_speed_dps) {
  if (!std::isfinite(output_speed_dps)) {
    throw std::invalid_argument("Servo speed must be finite.");
  }

  constexpr double kScale = 100.0;
  const double scaled = std::round(output_speed_dps * kScale);
  const double bounded = std::clamp(
      scaled, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  return static_cast<std::int32_t>(bounded);
}

}  // namespace

CanFrame ServoV38::speedCommand(std::uint8_t motor_id, double output_speed_dps) {
  CanFrame frame;
  frame.id = kCommandIdBase + motor_id;
  frame.data[0] = kSpeedClosedLoop;
  writeInt32Le(frame.data, 4, toSpeedControlValue(output_speed_dps));
  return frame;
}

CanFrame ServoV38::stopCommand(std::uint8_t motor_id) {
  CanFrame frame;
  frame.id = kCommandIdBase + motor_id;
  frame.data[0] = kMotorStop;
  return frame;
}

CanFrame ServoV38::readMultiTurnEncoderCommand(std::uint8_t motor_id) {
  CanFrame frame;
  frame.id = kCommandIdBase + motor_id;
  frame.data[0] = kReadMultiTurnEncoder;
  return frame;
}

std::optional<ServoV38Feedback> ServoV38::parseFeedback(const CanFrame& frame) {
  if (frame.id <= kFeedbackIdBase || frame.id > kFeedbackIdBase + 32 ||
      frame.data[0] != kSpeedClosedLoop) {
    return std::nullopt;
  }

  ServoV38Feedback feedback;
  feedback.motor_id = static_cast<std::uint8_t>(frame.id - kFeedbackIdBase);
  feedback.temperature_c = static_cast<std::int8_t>(frame.data[1]);
  feedback.torque_current_a = static_cast<double>(readInt16Le(frame.data, 2)) * 0.01;
  feedback.output_speed_dps = static_cast<double>(readInt16Le(frame.data, 4));
  feedback.output_angle_deg = static_cast<double>(readInt16Le(frame.data, 6));
  return feedback;
}

void ServoV38::writeInt32Le(std::array<std::uint8_t, 8>& data, std::size_t offset,
                             std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  for (std::size_t index = 0; index < 4; ++index) {
    data[offset + index] = static_cast<std::uint8_t>((raw >> (index * 8U)) & 0xFFU);
  }
}

std::int16_t ServoV38::readInt16Le(const std::array<std::uint8_t, 8>& data,
                                    std::size_t offset) {
  const auto raw = static_cast<std::uint16_t>(data[offset]) |
                   (static_cast<std::uint16_t>(data[offset + 1]) << 8U);
  return static_cast<std::int16_t>(raw);
}

}  // namespace crawling_robot_drivers
