#pragma once

#include "drive_settings.h"
#include <QObject>
#include <QSerialPort>

namespace crawling {

class ClampMotorController final : public QObject {
  Q_OBJECT
 public:
  explicit ClampMotorController(QObject* parent = nullptr);
  void setSettings(const DriveSettings& settings);
 public slots:
  void moveXPositive(); void moveXNegative();
  void moveYPositive(); void moveYNegative();
  void moveZPositive(); void moveZNegative();
  void stop();
 signals:
  void statusChanged(const QString& message);
 private:
  void move(int axis, int direction);
  bool ensureOpen();
  QByteArray encodeFrame(std::uint32_t id, const QByteArray& data) const;
  QSerialPort port_;
  DriveSettings settings_;
};
}
