#include "mwd_modbus_protocol.h"

#include <cstdint>

namespace crawling {
namespace {

void appendUInt16BigEndian(QByteArray* bytes, std::uint16_t value) {
  bytes->append(static_cast<char>((value >> 8U) & 0xFFU));
  bytes->append(static_cast<char>(value & 0xFFU));
}

void appendInt32AsModbusWords(QByteArray* bytes, std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  appendUInt16BigEndian(bytes, static_cast<std::uint16_t>(raw >> 16U));
  appendUInt16BigEndian(bytes, static_cast<std::uint16_t>(raw & 0xFFFFU));
}

std::uint16_t crc16(const QByteArray& bytes) {
  std::uint16_t crc = 0xFFFFU;
  for (const char byte : bytes) {
    crc ^= static_cast<std::uint8_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) != 0U
                ? static_cast<std::uint16_t>((crc >> 1U) ^ 0xA001U)
                : static_cast<std::uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

QByteArray withCrc(QByteArray frame) {
  const std::uint16_t crcValue = crc16(frame);
  // MWD V5.0 specifies the CRC high byte before the low byte.
  frame.append(static_cast<char>((crcValue >> 8U) & 0xFFU));
  frame.append(static_cast<char>(crcValue & 0xFFU));
  return frame;
}

}  // namespace

QByteArray MwdModbusProtocol::writeSingleRegister(std::uint8_t slaveId,
                                                  std::uint16_t address,
                                                  std::uint16_t value) {
  if (slaveId < 1 || slaveId > 32) {
    return {};
  }
  QByteArray frame;
  frame.reserve(8);
  frame.append(static_cast<char>(slaveId));
  frame.append(static_cast<char>(0x06));
  appendUInt16BigEndian(&frame, address);
  appendUInt16BigEndian(&frame, value);
  return withCrc(frame);
}

QByteArray MwdModbusProtocol::velocityCommand(std::uint8_t slaveId,
                                              std::int32_t pulsesPerSecond) {
  if (slaveId < 1 || slaveId > 32) {
    return {};
  }
  QByteArray frame;
  frame.reserve(13);
  frame.append(static_cast<char>(slaveId));
  frame.append(static_cast<char>(0x10));
  appendUInt16BigEndian(&frame, kVelocityRegister);
  appendUInt16BigEndian(&frame, 2);
  frame.append(static_cast<char>(4));
  appendInt32AsModbusWords(&frame, pulsesPerSecond);
  return withCrc(frame);
}

QByteArray MwdModbusProtocol::velocityModeCommand(std::uint8_t slaveId) {
  return writeSingleRegister(slaveId, kWorkModeRegister, kVelocityMode);
}

QByteArray MwdModbusProtocol::runCommand(std::uint8_t slaveId) {
  return writeSingleRegister(slaveId, kStateRegister, kRunState);
}

QByteArray MwdModbusProtocol::stopCommand(std::uint8_t slaveId) {
  return writeSingleRegister(slaveId, kStateRegister, kStopState);
}

}  // namespace crawling
