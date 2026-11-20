#pragma once

#include "drive_settings.h"

#include <QByteArray>
#include <QVector>
#include <cstdint>

namespace crawling {

struct ZdtCanFrame {
  std::uint32_t id = 0; // Always a 29-bit extended CAN identifier.
  QByteArray data;
};

// ZDT X/Emm free protocol, fixed 0x6B checksum (not Modbus RTU).
class ZdtCanProtocol {
 public:
  static QVector<ZdtCanFrame> packetize(int address, const QByteArray& command);
  static QVector<ZdtCanFrame> enableCommand(int address);
  static QVector<ZdtCanFrame> speedCommand(int address, ZdtFirmware firmware,
                                          int signedTenthsRpm, int acceleration);
  static QVector<ZdtCanFrame> stopCommand(int address);
  // 解除堵转保护（0x0E，数据 0x01），用于清除堵转锁死后的驱动板错误态。
  // 固件不支持该命令时驱动板回错误码，不会产生运动。
  static QVector<ZdtCanFrame> releaseStallCommand(int address);
};

} // namespace crawling
