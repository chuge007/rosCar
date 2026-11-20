#pragma once

#include "protocol/protocol_types.h"

#include <QHostAddress>
#include <QString>

#include <atomic>
#include <future>
#include <memory>
#include <thread>

class QTcpServer;
class QTcpSocket;

namespace pa1664::remote {

// 主机端“重启本进程”运维通道。
//
// 为什么不复用 ControlServer 里的一条 operation：ControlServer 跑在主线程的
// 事件循环上，而现场的“卡机”几乎都是主线程被驱动、相机或 USB 的同步调用堵死，
// 那时 readyRead 根本不会被派发，任何走控制通道的救命命令都发不进去。
// 这条通道因此跑在自己的 std::thread 里，只用阻塞式 QTcpSocket API，
// 不依赖任何事件循环，也不碰主线程的对象。
class HostRestartServer final {
 public:
  HostRestartServer() = default;
  ~HostRestartServer();

  HostRestartServer(const HostRestartServer&) = delete;
  HostRestartServer& operator=(const HostRestartServer&) = delete;

  // 监听失败时只返回 false，由调用方决定是否降级（重启是运维功能，
  // 端口与用户的其它端口撞车时不能连累主机主服务）。
  bool start(const QHostAddress& address, quint16 port, QString* error = nullptr);
  void stop();
  bool isListening() const { return listening_.load(); }
  quint16 port() const { return port_; }

 private:
  void runLoop(std::shared_ptr<std::promise<bool>> ready);
  void handleClient(QTcpSocket* socket);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> listening_{false};
  std::atomic<bool> stopRequested_{false};
  QHostAddress address_;
  quint16 port_ = kDefaultAdminPort;
};

}  // namespace pa1664::remote
