#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace crawling_robot_drivers {

struct CanFrame {
  std::uint32_t id = 0;
  std::array<std::uint8_t, 8> data{};
};

struct ServoV38Feedback {
  std::uint8_t motor_id = 0;
  std::int8_t temperature_c = 0;
  double torque_current_a = 0.0;
  double output_speed_dps = 0.0;
  double output_angle_deg = 0.0;
};

class ServoV38 {
public:
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  static constexpr std::uint8_t kReadMultiTurnEncoder = 0x60;
  static constexpr std::uint8_t kMotorStop = 0x81;
  static constexpr std::uint32_t kCommandIdBase = 0x140;
  static constexpr std::uint32_t kFeedbackIdBase = 0x240;

  static CanFrame speedCommand(std::uint8_t motor_id, double output_speed_dps);
  static CanFrame stopCommand(std::uint8_t motor_id);
  static CanFrame readMultiTurnEncoderCommand(std::uint8_t motor_id);
  static std::optional<ServoV38Feedback> parseFeedback(const CanFrame& frame);

private:
  static void writeInt32Le(std::array<std::uint8_t, 8>& data, std::size_t offset,
                           std::int32_t value);
  static std::int16_t readInt16Le(const std::array<std::uint8_t, 8>& data,
                                  std::size_t offset);
};

}  // namespace crawling_robot_drivers
