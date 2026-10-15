#pragma once

#include "drive_settings.h"

#include <QHash>
#include <QObject>
#include <QSerialPort>
#include <cstdint>

namespace crawling {

class ClampMotorController final : public QObject {
  Q_OBJECT
 public:
  explicit ClampMotorController(QObject* parent = nullptr);
  ~ClampMotorController() override;
  void setSettings(const DriveSettings& settings);
  const DriveSettings& settings() const { return settings_; }
 public slots:
  void connectDevice();
  void disconnectDevice();
  void setMotorSpeed(int axis, int index, int percent);
  void stopMotor(int axis, int index);
  void moveXPositive(); void moveXNegative();
  void moveYPositive(); void moveYNegative();
  void moveZPositive(); void moveZNegative();
  void setXSpeed(int percent); void setYSpeed(int percent); void setZSpeed(int percent);
  void stopX(); void stopY(); void stopZ();
  void stop();
 signals:
  void settingsChanged();
  void controlsResetRequested();
  void connectionChanged(bool connected, const QString& message);
  void statusChanged(const QString& message);
 private:
  void move(int axis, int direction);
  void move(int axis, int direction, int speedPercent);
  void setSpeed(int axis, int percent);
  QSerialPort* ensureOpen(const ClampMotorSettings& motor);
  QByteArray encodeFrame(std::uint32_t id, const QByteArray& data,
                         bool extended = false) const;
  bool sendCanFrame(QSerialPort* port, std::uint32_t id, const QByteArray& data,
                    bool extended = false);
  bool sendMwdModbusFrame(QSerialPort* port, const QByteArray& frame);
  bool sendMwdModbusSpeed(QSerialPort* port, int motorId, int speed);
  bool sendMotorCommand(const ClampMotorSettings& motor, QSerialPort* port, int percent);
  void closePorts();
  QHash<QString, QSerialPort*> ports_;
  DriveSettings settings_;
  bool connectionEnabled_ = true;
};
} // namespace crawling
