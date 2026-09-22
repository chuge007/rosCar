#include "clamp_motor_controller.h"
#include "utf8_compat.h"
#include <QtEndian>

namespace crawling {
ClampMotorController::ClampMotorController(QObject* parent) : QObject(parent) {}
void ClampMotorController::setSettings(const DriveSettings& settings) { settings_ = settings; }
QByteArray ClampMotorController::encodeFrame(std::uint32_t id, const QByteArray& data) const {
  QByteArray out = CRAWLING_TEXT("t%1%2").arg(id,3,16,QLatin1Char('0')).arg(8,1,16,QLatin1Char('0')).toUpper().toLatin1();
  for (char c : data) out += CRAWLING_TEXT("%1").arg(static_cast<unsigned char>(c),2,16,QLatin1Char('0')).toUpper().toLatin1();
  out += '\r'; return out;
}
bool ClampMotorController::ensureOpen() {
  if (port_.isOpen()) return true;
  port_.setPortName(settings_.clampSerialPort); port_.setBaudRate(settings_.clampSerialBaudRate);
  if (!port_.open(QIODevice::ReadWrite)) { emit statusChanged(CRAWLING_TEXT("夹子电机串口打开失败")); return false; }
  const QByteArray bitrate = settings_.clampCanBitrate == 125000 ? "S4\r" :
      settings_.clampCanBitrate == 250000 ? "S5\r" :
      settings_.clampCanBitrate == 500000 ? "S6\r" :
      settings_.clampCanBitrate == 800000 ? "S7\r" : "S8\r";
  port_.write("C\r"); port_.write(bitrate); port_.write("O\r");
  return true;
}
void ClampMotorController::move(int axis, int direction) {
  if (!ensureOpen()) return;
  const int ids[] = {settings_.clampXMotorId, settings_.clampYMotorId, settings_.clampZMotorId};
  const int signs[] = {settings_.clampXMotorSign, settings_.clampYMotorSign, settings_.clampZMotorSign};
  QByteArray d(8, '\0'); d[0]=char(0x23); d[1]=char(0xff); d[2]=char(0x60); d[3]=0;
  qToLittleEndian<qint32>(direction * signs[axis] * 1000, reinterpret_cast<uchar*>(d.data()+4));
  port_.write(encodeFrame(0x600 + ids[axis], d)); port_.waitForBytesWritten(100);
  emit statusChanged(CRAWLING_TEXT("夹子 %1 轴 %2").arg(axis == 0 ? "X" : axis == 1 ? "Y" : "Z").arg(direction > 0 ? "+" : "-"));
}
void ClampMotorController::stop() { if (port_.isOpen()) { for (int i=0;i<3;++i) move(i,0); } }
void ClampMotorController::moveXPositive(){move(0,1);} void ClampMotorController::moveXNegative(){move(0,-1);}
void ClampMotorController::moveYPositive(){move(1,1);} void ClampMotorController::moveYNegative(){move(1,-1);}
void ClampMotorController::moveZPositive(){move(2,1);} void ClampMotorController::moveZNegative(){move(2,-1);}
}
