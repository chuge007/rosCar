#include "zdt_can_protocol.h"

namespace crawling {
namespace {
void appendWord(QByteArray& data, int value) {
  data.append(char((value >> 8) & 0xFF));
  data.append(char(value & 0xFF));
}
}

QVector<ZdtCanFrame> ZdtCanProtocol::packetize(int address,
                                             const QByteArray& command) {
  if (address < 1 || address > 255 || command.size() < 2 ||
      command.size() > 1 + 7 * 256)
    return {};
  QVector<ZdtCanFrame> frames;
  for (int offset = 1, packet = 0; offset < command.size(); offset += 7, ++packet) {
    QByteArray data(1, command[0]);
    data.append(command.mid(offset, 7));
    frames.append({(static_cast<std::uint32_t>(address) << 8) |
                       static_cast<std::uint32_t>(packet), data});
  }
  return frames;
}

QVector<ZdtCanFrame> ZdtCanProtocol::enableCommand(int address) {
  return packetize(address, QByteArray::fromHex("F3AB01006B"));
}

QVector<ZdtCanFrame> ZdtCanProtocol::speedCommand(int address, ZdtFirmware firmware,
                                                int signedTenthsRpm,
                                                int acceleration) {
  if (signedTenthsRpm < -30000 || signedTenthsRpm > 30000 ||
      acceleration < 0 || acceleration > (firmware == ZdtFirmware::X ? 65535 : 255))
    return {};
  QByteArray data;
  data.append(char(0xF6));
  data.append(char(signedTenthsRpm < 0 ? 1 : 0));
  if (firmware == ZdtFirmware::X) {
    appendWord(data, acceleration);
    appendWord(data, qAbs(signedTenthsRpm));
  } else {
    appendWord(data, qAbs(signedTenthsRpm) / 10);
    data.append(char(acceleration));
  }
  data.append(char(0)); // Immediate execution.
  data.append(char(0x6B));
  return packetize(address, data);
}

QVector<ZdtCanFrame> ZdtCanProtocol::stopCommand(int address) {
  return packetize(address, QByteArray::fromHex("FE98006B"));
}

} // namespace crawling
