#pragma once

#include "protocol/protocol_types.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace pa1664::remote {

// 操作端侧的“重启小车端程序”请求。
//
// 握手走主机独立线程上的运维端口（control + kAdminPortOffset），不经过
// 控制通道：小车端主线程卡死时控制通道已经没人应答，这里仍能连上。
// 全程异步，界面不会被这次尝试卡住；超时/出错都会以 finished(false) 返回。
class HostRestartClient final : public QObject {
  Q_OBJECT
 public:
  explicit HostRestartClient(QObject* parent = nullptr);

  // 一个实例只发一次请求，结束（成功或失败）后 emits finished()。
  void request(const QString& host, quint16 port);

 signals:
  void finished(bool accepted, const QString& detail);

 private:
  void armTimeout();
  void onReadyRead();
  void handleGreeting(const QByteArray& line);
  void fail(const QString& detail);
  void settle(bool accepted, const QString& detail);

  QTcpSocket* socket_ = nullptr;
  QTimer* timeout_ = nullptr;
  QByteArray buffer_;
  bool settled_ = false;
};

}  // namespace pa1664::remote
