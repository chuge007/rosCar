#pragma once

#include "remote_channel_codec.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

class QSerialPort;
class QTimer;

namespace crawling {

// 串口通道读取器：按平台 profile 的帧规则轮询并解析。所有串口操作都是非阻塞
// 的（write + readyRead），因此可以直接活在 GUI 线程；不在这里做任何运动输出。
class RemoteChannelReader : public QObject {
  Q_OBJECT
 public:
  explicit RemoteChannelReader(QObject *parent = nullptr);
  ~RemoteChannelReader() override;

  bool open(const RemotePlatformProfile &profile, const QString &portName,
            QString *error);
  void close();
  bool isOpen() const;
  QString portName() const { return portName_; }
  QString lastError() const { return lastError_; }
  quint64 frameCount() const { return frameCount_; }
  const RemotePlatformProfile &profile() const;

  // 超过该时长没有有效帧即判定失联并停掉所有下游输出。
  void setStaleTimeoutMs(int timeoutMs) { staleTimeoutMs_ = qMax(60, timeoutMs); }

 signals:
  void sampleReceived(const crawling::RemoteChannelSample &sample);
  void linkStateChanged(bool connected, const QString &detail);
  void logMessage(const QString &message);

 private slots:
  void poll();
  void checkStale();
  void onReadyRead();
  void onSerialError(int error);

 private:
  void markAlive();
  void markStale(const QString &reason);

  QSerialPort *serial_ = nullptr;
  QTimer *pollTimer_ = nullptr;
  QTimer *staleTimer_ = nullptr;
  std::unique_ptr<RemoteChannelCodec> codec_;
  QByteArray buffer_;
  QElapsedTimer staleClock_;
  QString portName_;
  QString lastError_;
  quint64 frameCount_ = 0;
  int staleTimeoutMs_ = 200;
  bool connected_ = false;
};

} // namespace crawling
