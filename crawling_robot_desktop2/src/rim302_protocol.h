#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace crawling {

struct ImuSample {
  double rollRad = 0.0;
  double pitchRad = 0.0;
  double yawRad = 0.0;
  double gyroXRadps = 0.0;
  double gyroYRadps = 0.0;
  double gyroZRadps = 0.0;
  double accelerationXMps2 = 0.0;
  double accelerationYMps2 = 0.0;
  double accelerationZMps2 = 0.0;
};

class Rim302FrameParser final {
 public:
  std::vector<ImuSample> consume(const std::uint8_t* bytes, std::size_t size);
  static std::vector<std::uint8_t> continuousModeCommand(std::uint8_t divider);

 private:
  static std::uint16_t crc16(const std::uint8_t* bytes, std::size_t size);
  std::vector<std::uint8_t> buffer_;
};

}  // namespace crawling
