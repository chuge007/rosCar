#pragma once

#include "drive_settings.h"

#include <QHash>
#include <QObject>
#include <QSerialPort>
#include <QSet>
#include <cstdint>

namespace crawling {

class CandleCanAdapter;
class PcanCanAdapter;

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
  bool ensureOpen(const ClampMotorSettings& motor);
  QByteArray encodeFrame(std::uint32_t id, const QByteArray& data,
                         bool extended = false) const;
  bool sendCanFrame(const ClampMotorSettings& motor, std::uint32_t id,
                    const QByteArray& data,
                    bool extended = false,
                    const QString& context = {});
  bool sendMwdModbusFrame(QSerialPort* port, const QByteArray& frame,
                          const QString& context = {});
  bool sendMwdModbusSpeed(QSerialPort* port, int motorId, int speed,
                          const QString& context = {});
  bool setMwdBrake(const ClampMotorSettings& motor, bool release,
                   int axis, int index);
  bool sendMotorCommand(const ClampMotorSettings& motor, int percent,
                        int axis = -1, int index = -1);
  QString motorContext(const ClampMotorSettings& motor, int axis,
                       int index) const;
  QString zdtMotorKey(const ClampMotorSettings& motor, int axis,
                      int index) const;
  void onCanFrame(const QString& port, std::uint32_t id,
                  const QByteArray& data, bool extended);
  void logSpeedStatus(const QString& key, const QString& message);
  void closePorts();
  QHash<QString, QSerialPort*> ports_;
  QHash<QString, CandleCanAdapter*> candlePorts_;
  QHash<QString, PcanCanAdapter*> pcanPorts_;
  QSet<QString> zdtEnabledMotors_;
  QSet<QString> zdtEnablePending_;
  QSet<QString> zdtProbePending_;
  QHash<QString, int> zdtRequestedPercent_;
  QHash<QString, int> zdtSpeedTickets_;
  QHash<QString, bool> mwdBrakeReleased_;
  QHash<QString, bool> mwdBrakeConfirmed_;
  QHash<QString, int> mwdBrakeQueryTickets_;
  QHash<QString, qint64> speedLogTime_;
  quint64 zdtSession_ = 0;
  int nextSpeedTicket_ = 0;
  int nextMwdBrakeQueryTicket_ = 0;
  DriveSettings settings_;
  bool connectionEnabled_ = true;
};
} // namespace crawling
