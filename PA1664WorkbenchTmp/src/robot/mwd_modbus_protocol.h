#pragma once

#include <QByteArray>

#include <cstdint>

namespace crawling {

class MwdModbusProtocol final {
 public:
  static constexpr std::uint8_t kDefaultSlaveId = 1;
  static constexpr std::uint16_t kStateRegister = 6006;
  static constexpr std::uint16_t kWorkModeRegister = 6007;
  static constexpr std::uint16_t kVelocityRegister = 6010;
  static constexpr std::uint16_t kVelocityMode = 2;
  static constexpr std::uint16_t kRunState = 2;
  static constexpr std::uint16_t kStopState = 0;

  static QByteArray writeSingleRegister(std::uint8_t slaveId,
                                        std::uint16_t address,
                                        std::uint16_t value);
  static QByteArray velocityCommand(std::uint8_t slaveId,
                                    std::int32_t pulsesPerSecond);
  static QByteArray velocityModeCommand(std::uint8_t slaveId);
  static QByteArray runCommand(std::uint8_t slaveId);
  static QByteArray stopCommand(std::uint8_t slaveId);
};

}  // namespace crawling
