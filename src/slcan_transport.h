#pragma once

#include "drive_settings.h"
#include "drive_types.h"

#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QTimer>
#include <QVector>

namespace crawling {

class SlcanTransport final : public QObject {
  Q_OBJECT

 public:
  explicit SlcanTransport(QObject* parent = nullptr);

  bool isOpen() const;
  void open(const DriveSettings& settings);
  void close();
  bool sendDrivePair(const CanFrame& left, const CanFrame& right, bool urgent = false);
  bool sendFrame(const CanFrame& frame, bool urgent = false);

 signals:
  void connectionChanged(bool connected, const QString& message);
  void frameReceived(const crawling::CanFrame& frame);
  void transportError(const QString& message);
  void activity(const QString& message);

 private slots:
  void readAvailable();
  void onSerialError(QSerialPort::SerialPortError error);
  void transmitNextFrame();

 private:
  static QByteArray bitrateCommand(int bitrate);
  static QByteArray encodeFrame(const CanFrame& frame);
  void writeControl(const QByteArray& bytes);
  bool writeFrames(const QVector<CanFrame>& frames, bool urgent);
  void parseLine(const QByteArray& line);

  QSerialPort port_;
  QByteArray receiveBuffer_;
  QQueue<QByteArray> transmitQueue_;
  QTimer transmitTimer_;
};

}  // namespace crawling
