#include "crawling_robot_drivers/rim302_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace crawling_robot_drivers {
namespace {

constexpr std::uint8_t kSyncByte = 0xFF;
constexpr std::uint8_t kStartByte = 0x02;
constexpr std::uint8_t kEndByte = 0x03;
constexpr std::size_t kHeaderSize = 5;
constexpr std::size_t kTrailerSize = 3;
constexpr std::size_t kMaximumPayloadSize = 504;

std::vector<std::uint8_t> buildFrame(std::uint8_t command,
                                     const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaximumPayloadSize) {
    throw std::invalid_argument("RIM302 payload exceeds the protocol maximum.");
  }

  std::vector<std::uint8_t> frame;
  frame.reserve(kHeaderSize + payload.size() + kTrailerSize);
  frame.push_back(kSyncByte);
  frame.push_back(kStartByte);
  frame.push_back(command);
  frame.push_back(static_cast<std::uint8_t>((payload.size() >> 8U) & 0xFFU));
  frame.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFU));
  frame.insert(frame.end(), payload.begin(), payload.end());

  const auto checksum = Rim302Protocol::crc16(frame.data() + 2, 3 + payload.size());
  frame.push_back(static_cast<std::uint8_t>((checksum >> 8U) & 0xFFU));
  frame.push_back(static_cast<std::uint8_t>(checksum & 0xFFU));
  frame.push_back(kEndByte);
  return frame;
}

float readFloatLe(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  std::array<std::uint8_t, sizeof(float)> raw{};
  std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(float), raw.begin());
  float value = 0.0F;
  std::memcpy(&value, raw.data(), sizeof(value));
  return value;
}

}  // namespace

std::uint16_t Rim302Protocol::crc16(const std::uint8_t* buffer, std::size_t size) {
  std::uint16_t crc = 0;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= buffer[index];
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      const bool carry = (crc & 0x0001U) != 0U;
      crc >>= 1U;
      if (carry) {
        crc ^= 0x8408U;
      }
    }
  }
  return crc;
}

std::vector<std::uint8_t> Rim302Protocol::buildSetContinuousMode(std::uint8_t divider) {
  if (divider == 0) {
    throw std::invalid_argument("RIM302 output divider cannot be zero.");
  }
  return buildFrame(kSetContinuousMode, {0x00, 0x01, divider});
}

std::vector<std::uint8_t> Rim302Protocol::buildGetDefaultOutput() {
  return buildFrame(kGetDefaultOutput, {});
}

std::optional<Rim302Sample> Rim302Protocol::parseContinuousOutput(
    const std::vector<std::uint8_t>& payload) {
  if (payload.size() != 36) {
    return std::nullopt;
  }

  Rim302Sample sample;
  sample.roll_rad = readFloatLe(payload, 0);
  sample.pitch_rad = readFloatLe(payload, 4);
  sample.yaw_rad = readFloatLe(payload, 8);
  sample.angular_velocity_x_rad_s = readFloatLe(payload, 12);
  sample.angular_velocity_y_rad_s = readFloatLe(payload, 16);
  sample.angular_velocity_z_rad_s = readFloatLe(payload, 20);
  sample.linear_acceleration_x_m_s2 = readFloatLe(payload, 24);
  sample.linear_acceleration_y_m_s2 = readFloatLe(payload, 28);
  sample.linear_acceleration_z_m_s2 = readFloatLe(payload, 32);
  return sample;
}

std::vector<Rim302Sample> Rim302FrameParser::consume(const std::uint8_t* bytes,
                                                      std::size_t size) {
  buffer_.insert(buffer_.end(), bytes, bytes + size);
  std::vector<Rim302Sample> samples;
  constexpr std::array<std::uint8_t, 2> kFramePrefix{kSyncByte, kStartByte};

  while (true) {
    const auto frame_start =
        std::search(buffer_.begin(), buffer_.end(), kFramePrefix.begin(), kFramePrefix.end());
    if (frame_start == buffer_.end()) {
      if (buffer_.size() > 1) {
        buffer_.erase(buffer_.begin(), buffer_.end() - 1);
      }
      break;
    }
    if (frame_start != buffer_.begin()) {
      buffer_.erase(buffer_.begin(), frame_start);
    }
    if (buffer_.size() < kHeaderSize) {
      break;
    }

    const std::size_t payload_size =
        (static_cast<std::size_t>(buffer_[3]) << 8U) | static_cast<std::size_t>(buffer_[4]);
    if (payload_size > kMaximumPayloadSize) {
      buffer_.erase(buffer_.begin());
      continue;
    }
    const std::size_t frame_size = kHeaderSize + payload_size + kTrailerSize;
    if (buffer_.size() < frame_size) {
      break;
    }
    if (buffer_[frame_size - 1] != kEndByte) {
      buffer_.erase(buffer_.begin());
      continue;
    }

    const auto expected_crc = static_cast<std::uint16_t>(buffer_[5 + payload_size]) << 8U |
                              static_cast<std::uint16_t>(buffer_[6 + payload_size]);
    const auto actual_crc = Rim302Protocol::crc16(buffer_.data() + 2, 3 + payload_size);
    if (actual_crc != expected_crc) {
      buffer_.erase(buffer_.begin());
      continue;
    }

    if (buffer_[2] == Rim302Protocol::kContinuousDefaultOutput ||
        buffer_[2] == Rim302Protocol::kReturnDefaultOutput) {
      std::vector<std::uint8_t> payload(buffer_.begin() + kHeaderSize,
                                        buffer_.begin() + kHeaderSize + payload_size);
      if (const auto sample = Rim302Protocol::parseContinuousOutput(payload)) {
        samples.push_back(*sample);
      }
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
  }

  return samples;
}

}  // namespace crawling_robot_drivers
