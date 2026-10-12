#pragma once

#include "drive_settings.h"

#include <QObject>
#include <QSerialPort>

#include <cstdint>

namespace crawling {

class ClampMotorController final : public QObject {
  Q_OBJECT
 public:
  explicit ClampMotorController(QObject* parent = nullptr);
  void setSettings(const DriveSettings& settings);
 public slots:
  void connectDevice();
  void disconnectDevice();
  void moveXPositive(); void moveXNegative();
  void moveYPositive(); void moveYNegative();
  void moveZPositive(); void moveZNegative();
  void setXSpeed(int percent); void setYSpeed(int percent); void setZSpeed(int percent);
  void stopX(); void stopY(); void stopZ();
  void stop();
 signals:
  void connectionChanged(bool connected, const QString& message);
  void statusChanged(const QString& message);
 private:
  void move(int axis, int direction);
  void move(int axis, int direction, int speedPercent);
  void setSpeed(int axis, int percent);
  bool ensureOpen();
  QByteArray encodeFrame(std::uint32_t id, const QByteArray& data) const;
  bool sendCanFrame(std::uint32_t id, const QByteArray& data);
  bool sendMwdModbusFrame(const QByteArray& frame);
  bool sendMwdModbusSpeed(int motorId, int direction, int sign);
  QSerialPort port_;
  DriveSettings settings_;
  bool connectionEnabled_ = true;
};
}
