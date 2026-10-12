#include "rim302_protocol.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace crawling {
namespace {
constexpr std::uint8_t kSync = 0xFF;
constexpr std::uint8_t kStart = 0x02;
constexpr std::uint8_t kEnd = 0x03;

float readFloat(const std::vector<std::uint8_t>& data, std::size_t offset) {
  float value = 0.0F;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}
}  // namespace

std::uint16_t Rim302FrameParser::crc16(const std::uint8_t* bytes, std::size_t size) {
  std::uint16_t crc = 0;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      const bool carry = (crc & 1U) != 0;
      crc >>= 1U;
      if (carry) crc ^= 0x8408U;
    }
  }
  return crc;
}

std::vector<std::uint8_t> Rim302FrameParser::continuousModeCommand(std::uint8_t divider) {
  const std::uint8_t safeDivider = divider == 0 ? 1 : divider;
  std::vector<std::uint8_t> frame{kSync, kStart, 0x53, 0x00, 0x03, 0x00, 0x01, safeDivider};
  const std::uint16_t crc = crc16(frame.data() + 2, 6);
  frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
  frame.push_back(kEnd);
  return frame;
}

std::vector<ImuSample> Rim302FrameParser::consume(const std::uint8_t* bytes, std::size_t size) {
  buffer_.insert(buffer_.end(), bytes, bytes + size);
  std::vector<ImuSample> samples;
  const std::array<std::uint8_t, 2> prefix{kSync, kStart};
  while (true) {
    const auto start = std::search(buffer_.begin(), buffer_.end(), prefix.begin(), prefix.end());
    if (start == buffer_.end()) { if (buffer_.size() > 1) buffer_.erase(buffer_.begin(), buffer_.end() - 1); break; }
    if (start != buffer_.begin()) buffer_.erase(buffer_.begin(), start);
    if (buffer_.size() < 8) break;
    const std::size_t payloadSize = (std::size_t(buffer_[3]) << 8U) | buffer_[4];
    const std::size_t total = 5 + payloadSize + 3;
    if (payloadSize > 504 || buffer_.size() < total) break;
    if (buffer_[total - 1] != kEnd || crc16(buffer_.data() + 2, 3 + payloadSize) !=
        ((std::uint16_t(buffer_[5 + payloadSize]) << 8U) | buffer_[6 + payloadSize])) { buffer_.erase(buffer_.begin()); continue; }
    if ((buffer_[2] == 0x90 || buffer_[2] == 0x57) && payloadSize == 36) {
      std::vector<std::uint8_t> payload(buffer_.begin() + 5, buffer_.begin() + 41);
      ImuSample s;
      s.rollRad = readFloat(payload, 0); s.pitchRad = readFloat(payload, 4); s.yawRad = readFloat(payload, 8);
      s.gyroXRadps = readFloat(payload, 12); s.gyroYRadps = readFloat(payload, 16); s.gyroZRadps = readFloat(payload, 20);
      s.accelerationXMps2 = readFloat(payload, 24); s.accelerationYMps2 = readFloat(payload, 28); s.accelerationZMps2 = readFloat(payload, 32);
      samples.push_back(s);
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
  }
  return samples;
}
}  // namespace crawling
