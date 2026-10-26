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
};

} // namespace crawling
