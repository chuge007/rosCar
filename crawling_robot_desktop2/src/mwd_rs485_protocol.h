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
  std::uint16_t encoder = 0;
};

class MwdRs485Protocol final {
 public:
  static constexpr std::uint8_t kFrameHeader = 0x3E;
  static constexpr std::uint8_t kReadStatus2 = 0x9C;
  static constexpr std::uint8_t kClearError = 0x9B;
  static constexpr std::uint8_t kMotorOff = 0x80;
  static constexpr std::uint8_t kMotorStop = 0x81;
  static constexpr std::uint8_t kBrakeControl = 0x8C;
  static constexpr std::uint8_t kMotorRun = 0x88;
  static constexpr std::uint8_t kReadMultiTurnAngle = 0x92;
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  static constexpr std::uint8_t kMultiTurnPositionClosedLoop = 0xA4;
  static constexpr std::uint8_t kBrakeApplied = 0x00;
  static constexpr std::uint8_t kBrakeReleased = 0x01;
  static constexpr std::uint8_t kBrakeRead = 0x10;

  static QByteArray command(std::uint8_t command, std::uint8_t motorId,
                            const QByteArray& data = {});
  static QByteArray speedCommand(std::uint8_t motorId, double speedDps);
  static QByteArray multiTurnAngleQuery(std::uint8_t motorId);
  static QByteArray multiTurnPositionCommand(std::uint8_t motorId,
                                             std::int64_t angleHundredthDegree,
                                             double maximumSpeedDps);
  static QByteArray brakeCommand(std::uint8_t motorId, bool apply);
  static QByteArray brakeStatusQuery(std::uint8_t motorId);

  // Removes and returns the next complete, checksum-valid frame. Corrupt
  // bytes are discarded until the next 0x3E header is found.
  static bool takeFrame(QByteArray* receiveBuffer, MwdRs485Frame* frame);
  static std::optional<MwdMotorFeedback> parseMotorFeedback(
      const MwdRs485Frame& frame);
  static std::optional<std::int64_t> parseMultiTurnAngle(
      const MwdRs485Frame& frame);
  static std::optional<bool> parseBrakeApplied(const MwdRs485Frame& frame);

 private:
  static std::uint8_t checksum(const QByteArray& bytes);
};

}  // namespace crawling
