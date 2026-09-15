#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace crawling_robot_drivers {

struct Rim302Sample {
  double roll_rad = 0.0;
  double pitch_rad = 0.0;
  double yaw_rad = 0.0;
  double angular_velocity_x_rad_s = 0.0;
  double angular_velocity_y_rad_s = 0.0;
  double angular_velocity_z_rad_s = 0.0;
  double linear_acceleration_x_m_s2 = 0.0;
  double linear_acceleration_y_m_s2 = 0.0;
  double linear_acceleration_z_m_s2 = 0.0;
};

class Rim302Protocol {
public:
  static constexpr std::uint8_t kContinuousDefaultOutput = 0x90;
  static constexpr std::uint8_t kSetContinuousMode = 0x53;
  static constexpr std::uint8_t kGetDefaultOutput = 0x56;
  static constexpr std::uint8_t kReturnDefaultOutput = 0x57;

  static std::uint16_t crc16(const std::uint8_t* buffer, std::size_t size);
  static std::vector<std::uint8_t> buildSetContinuousMode(std::uint8_t divider);
  static std::vector<std::uint8_t> buildGetDefaultOutput();
  static std::optional<Rim302Sample> parseContinuousOutput(
      const std::vector<std::uint8_t>& payload);
};

class Rim302FrameParser {
public:
  std::vector<Rim302Sample> consume(const std::uint8_t* bytes, std::size_t size);

private:
  std::vector<std::uint8_t> buffer_;
};

}  // namespace crawling_robot_drivers
