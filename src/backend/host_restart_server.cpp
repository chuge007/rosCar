#include "host_restart_server.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QRandomGenerator>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pa1664::remote {

namespace {

// 固定长度比较，避免按字节提前返回泄漏 HMAC 前缀（与其它通道保持一致）。
bool constantTimeEquals(const QByteArray& left, const QByteArray& right) {
  if (left.size() != right.size()) return false;
  unsigned char diff = 0;
  for (int i = 0; i < left.size(); ++i)
    diff |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
  return diff == 0;
}

// 继承当前进程的完整命令行（监听地址与四个端口都沿用现场配置），只把
// “等待旧进程”参数换成当前 PID，避免重启几次后命令行无限累加。
QStringList replacementArguments(qint64 previousPid) {
  QStringList args = QCoreApplication::arguments().mid(1);
  for (int i = args.size() - 1; i >= 0; --i) {
    if (args.at(i) == QStringLiteral("--restart-after-pid")) {
      args.removeAt(i);
      if (i < args.size()) args.removeAt(i);
    } else if (args.at(i).startsWith(QStringLiteral("--restart-after-pid="))) {
      args.removeAt(i);
    }
  }
  args << QStringLiteral("--restart-after-pid") << QString::number(previousPid);
  return args;
}

bool spawnReplacement(qint64 previousPid) {
  return QProcess::startDetached(
      QCoreApplication::applicationFilePath(), replacementArguments(previousPid),
      QCoreApplication::applicationDirPath());
}

// 事件循环被卡死时 quit() 的 queued 调用永远不会被处理，这里必须有硬兜底：
// 超时直接结束本进程，新进程的等待逻辑才知道该接管了。
void armForcedExit() {
  static std::atomic<bool> armed{false};
  if (armed.exchange(true)) return;
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(kAdminGracefulStopMs));
    qWarning().noquote()
        << "HOST_RESTART_FORCE_EXIT: event loop did not quit in time, "
           "terminating this process so the replacement can take over";
#ifdef Q_OS_WIN
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
  }).detach();
}

}  // namespace

HostRestartServer::~HostRestartServer() { stop(); }

bool HostRestartServer::start(const QHostAddress& address, quint16 port,
                              QString* error) {
  if (error) error->clear();
  if (running_.load()) return true;
  address_ = address;
  port_ = port;
  stopRequested_.store(false);
  running_.store(true);

  // 监听结果是同步返回给调用方的：主服务不该在“重启通道是否可用”上等，
  // 也不能假设它一定可用，所以这里只等 listen() 本身的结果。
  auto ready = std::make_shared<std::promise<bool>>();
  std::future<bool> listening = ready->get_future();
  thread_ = std::thread([this, ready] { runLoop(ready); });

  const bool ok = listening.get();
  running_.store(ok);
  if (!ok && error) {
    *error = QStringLiteral("运维重启端口 %1 监听失败").arg(port_);
    if (thread_.joinable()) thread_.join();
  }
  return ok;
}

void HostRestartServer::runLoop(std::shared_ptr<std::promise<bool>> ready) {
  QTcpServer server;
  const bool ok = server.listen(address_, port_);
  listening_.store(ok);
  // 监听结果必须在这里就交出去：runLoop 的循环要一直跑到进程退出，
  // 放在它之后 set_value 会让 start() 永久阻塞。
  ready->set_value(ok);
  if (!ok) return;

  // 500 ms 醒一次检查停止标志：stop() 不必等来一个连接才能退出。
  while (!stopRequested_.load()) {
    if (!server.waitForNewConnection(500)) continue;
    while (!stopRequested_.load()) {
      QTcpSocket* socket = server.nextPendingConnection();
      if (!socket) break;
      handleClient(socket);
    }
  }
  server.close();
  listening_.store(false);
}

void HostRestartServer::handleClient(QTcpSocket* socket) {
  const QString peer = QStringLiteral("%1:%2")
                           .arg(socket->peerAddress().toString())
                           .arg(socket->peerPort());

  QByteArray nonce(kAdminNonceBytes, Qt::Uninitialized);
  for (int i = 0; i < kAdminNonceBytes; ++i)
    nonce[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));

  socket->write(QByteArray(kAdminRestartGreeting) + ' ' + nonce.toHex() + '\n');
  socket->flush();

  QByteArray buffered;
  QByteArray requestLine;
  const qint64 deadline =
      QDateTime::currentMSecsSinceEpoch() + kAdminHandshakeTimeoutMs;
  while (QDateTime::currentMSecsSinceEpoch() < deadline) {
    const qint64 remain = deadline - QDateTime::currentMSecsSinceEpoch();
    if (remain <= 0) break;
    if (socket->bytesAvailable() <= 0 &&
        !socket->waitForReadyRead(static_cast<int>(remain))) {
      break;
    }
    buffered += socket->read(512);
    const int end = buffered.indexOf('\n');
    if (end >= 0) {
      requestLine = buffered.left(end);
      break;
    }
    if (buffered.size() > 512) break;
  }

  bool accepted = false;
  const QList<QByteArray> parts = requestLine.trimmed().simplified().split(' ');
  if (parts.size() == 2 && parts.at(0) == kAdminRestartCommand) {
    const QByteArray expected = QMessageAuthenticationCode::hash(
        nonce.toHex(), QByteArray(kAdminRestartSecret),
        QCryptographicHash::Sha256).toHex();
    accepted = constantTimeEquals(parts.at(1).toLower(), expected);
  }

  if (!accepted) {
    qWarning().noquote() << "HOST_RESTART_REJECTED"
                         << QStringLiteral("peer=%1 reason=bad_handshake").arg(peer);
    socket->write("ERR auth\n");
    socket->flush();
    socket->waitForBytesWritten(500);
    delete socket;
    return;
  }

  const qint64 selfPid = QCoreApplication::applicationPid();
  socket->write("OK " + QByteArray::number(selfPid) + '\n');
  socket->flush();
  socket->waitForBytesWritten(500);
  delete socket;

  // 先拉起替代进程再退出：如果替代进程根本没起来，自杀等于把主机彻底弄丢，
  // 这种情况宁可留在原地让现场还能手工处理。
  const bool spawned = spawnReplacement(selfPid);
  qWarning().noquote() << "HOST_RESTART_REQUESTED"
                       << QStringLiteral("peer=%1 pid=%2 spawn=%3")
                              .arg(peer)
                              .arg(selfPid)
                              .arg(spawned ? QStringLiteral("OK")
                                           : QStringLiteral("FAILED"));
  if (!spawned) return;

  if (QCoreApplication* app = QCoreApplication::instance()) {
    QMetaObject::invokeMethod(app, [] { QCoreApplication::quit(); },
                              Qt::QueuedConnection);
  }
  armForcedExit();
}

void HostRestartServer::stop() {
  stopRequested_.store(true);
  running_.store(false);
  if (thread_.joinable()) thread_.join();
  listening_.store(false);
}

}  // namespace pa1664::remote
