#include "host_restart_client.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QMessageAuthenticationCode>
#include <QTcpSocket>
#include <QTimer>

namespace pa1664::remote {

HostRestartClient::HostRestartClient(QObject* parent) : QObject(parent) {
  socket_ = new QTcpSocket(this);
  timeout_ = new QTimer(this);
  timeout_->setSingleShot(true);
  timeout_->setInterval(kAdminClientTimeoutMs);

  connect(timeout_, &QTimer::timeout, this, [this] {
    fail(QStringLiteral("主机没有响应重启请求（%1 秒超时）")
             .arg(kAdminClientTimeoutMs / 1000));
  });
  // 连接建立后才开始计时，避免地址解析慢时把预算耗在连线上。
  connect(socket_, &QTcpSocket::connected, this,
          [this] { armTimeout(); });
  connect(socket_, &QTcpSocket::readyRead, this,
          &HostRestartClient::onReadyRead);
  connect(socket_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) {
            fail(QStringLiteral("连不上主机运维端口：%1")
                     .arg(socket_->errorString()));
          });
}

void HostRestartClient::request(const QString& host, quint16 port) {
  if (settled_) return;
  armTimeout();
  qInfo().noquote() << "HOST_RESTART_CLIENT: connecting"
                    << QStringLiteral("%1:%2").arg(host).arg(port);
  socket_->connectToHost(host, port);
}

void HostRestartClient::armTimeout() {
  if (!timeout_->isActive()) timeout_->start();
}

void HostRestartClient::onReadyRead() {
  buffer_ += socket_->readAll();
  for (int end = buffer_.indexOf('\n'); end >= 0 && !settled_;
       end = buffer_.indexOf('\n')) {
    const QByteArray line = buffer_.left(end).trimmed();
    buffer_.remove(0, end + 1);
    if (line.startsWith(QByteArray(kAdminRestartGreeting) + ' ')) {
      handleGreeting(line);
    } else if (line.startsWith("OK")) {
      const QByteArray pid = line.mid(2).trimmed();
      settle(true, QStringLiteral("主机已接受重启指令（旧进程 pid=%1），"
                                  "正在换进程，控制通道稍后自动重连")
                       .arg(QString::fromLatin1(pid)));
    } else if (line.startsWith("ERR")) {
      fail(QStringLiteral("主机拒绝了重启请求：%1")
               .arg(QString::fromLatin1(line.mid(3).trimmed())));
    } else {
      fail(QStringLiteral("主机返回了无法识别的应答：%1")
               .arg(QString::fromLatin1(line.left(64))));
    }
  }
  if (buffer_.size() > 1024) {
    fail(QStringLiteral("主机运维端口应答异常（单行过长）"));
  }
}

void HostRestartClient::handleGreeting(const QByteArray& line) {
  // greeting 固定为 "PA1664-RESTART <nonce-hex>"，主机用同一个 nonce 校验回包。
  const QByteArray prefix = QByteArray(kAdminRestartGreeting) + ' ';
  const QByteArray nonceHex = line.mid(prefix.size()).trimmed();
  if (!line.startsWith(prefix) || nonceHex.isEmpty()) {
    fail(QStringLiteral("主机运维端口握手格式异常"));
    return;
  }
  const QByteArray mac = QMessageAuthenticationCode::hash(
                             nonceHex, QByteArray(kAdminRestartSecret),
                             QCryptographicHash::Sha256)
                             .toHex();
  socket_->write(QByteArray(kAdminRestartCommand) + ' ' + mac + '\n');
  socket_->flush();
}

void HostRestartClient::fail(const QString& detail) { settle(false, detail); }

void HostRestartClient::settle(bool accepted, const QString& detail) {
  if (settled_) return;
  settled_ = true;
  timeout_->stop();
  qInfo().noquote() << "HOST_RESTART_CLIENT:"
                    << (accepted ? "ACCEPTED" : "FAILED") << detail;
  // 主机接受后会主动结束连接，这里不等待对端关闭。
  socket_->abort();
  emit finished(accepted, detail);
}

}  // namespace pa1664::remote
