#pragma once

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace crawling {

struct MwdRs485Frame {
  std::uint8_t command = 0;
  std::uint8_t motorId = 0;
  QByteArray data;
};

struct MwdMotorFeedback {
  std::uint8_t motorId = 0;
  int temperatureC = 0;
  std::int16_t controlValue = 0;
  double speedDps = 0.0;
  std::int16_t outputAngleDeg = 0;
};

class MwdRs485Protocol final {
 public:
  static constexpr std::uint8_t kFrameHeader = 0x3E;
  static constexpr std::uint8_t kReadStatus2 = 0x9C;
  static constexpr std::uint8_t kMotorOff = 0x80;
  static constexpr std::uint8_t kMotorStop = 0x81;
  static constexpr std::uint8_t kSystemReset = 0x76;
  static constexpr std::uint8_t kReadMultiTurnEncoder = 0x60;
  static constexpr std::uint8_t kReadMultiTurnAngle = 0x92;
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  static constexpr std::uint8_t kMultiTurnPositionClosedLoop = 0xA4;

  static QByteArray command(std::uint8_t command, std::uint8_t motorId,
                            const QByteArray& data = {});
  static QByteArray speedCommand(std::uint8_t motorId, double speedDps);
  static QByteArray multiTurnAngleQuery(std::uint8_t motorId);
  static QByteArray multiTurnEncoderQuery(std::uint8_t motorId);
  static QByteArray multiTurnPositionCommand(std::uint8_t motorId,
                                             std::int64_t angleHundredthDegree,
                                             double maximumSpeedDps);

  // Removes and returns the next complete, CRC-valid V3.8 frame. Corrupt
  // bytes are discarded until the next 0x3E header is found.
  static bool takeFrame(QByteArray* receiveBuffer, MwdRs485Frame* frame);
  static std::optional<MwdMotorFeedback> parseMotorFeedback(
      const MwdRs485Frame& frame);
  static std::optional<std::int64_t> parseMultiTurnAngle(
      const MwdRs485Frame& frame);
  static std::optional<std::int32_t> parseMultiTurnEncoderPosition(
      const MwdRs485Frame& frame);

 private:
  static std::uint16_t crc16(const QByteArray& bytes);
};

}  // namespace crawling
