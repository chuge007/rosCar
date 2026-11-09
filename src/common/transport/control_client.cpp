#include "control_client.h"

#include "robot_app_logger.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>

namespace pa1664::remote {
namespace {

quint64 nowUs() {
  return static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000u;
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

}  // namespace

QString connectionStateText(ConnectionState state) {
  switch (state) {
    case ConnectionState::Disconnected:
      return QStringLiteral("未连接");
    case ConnectionState::Connecting:
      return QStringLiteral("正在连接");
    case ConnectionState::Handshaking:
      return QStringLiteral("正在握手");
    case ConnectionState::Ready:
      return QStringLiteral("已连接");
    case ConnectionState::Reconnecting:
      return QStringLiteral("正在重连");
  }
  return QStringLiteral("未知");
}

ControlClient::ControlClient(QObject* parent)
    : QObject(parent),
      socket_(new QTcpSocket(this)),
      reconnectTimer_(new QTimer(this)),
      handshakeTimer_(new QTimer(this)),
      heartbeatTimer_(new QTimer(this)) {
  reconnectTimer_->setSingleShot(true);
  handshakeTimer_->setSingleShot(true);
  handshakeTimer_->setInterval(3000);
  heartbeatTimer_->setInterval(kHeartbeatIntervalMs);

  socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
  socket_->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
  socket_->setReadBufferSize(kMaxControlPayloadBytes + kControlHeaderBytes);

  connect(socket_, &QTcpSocket::connected, this, &ControlClient::onConnected);
  connect(socket_, &QTcpSocket::readyRead, this, &ControlClient::onReadyRead);
  connect(socket_, &QTcpSocket::disconnected, this,
          &ControlClient::onDisconnected);
  connect(socket_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) { onSocketError(); });
  connect(reconnectTimer_, &QTimer::timeout, this,
          [this] { connectSocket(); });
  connect(handshakeTimer_, &QTimer::timeout, this, [this] {
    failProtocol(QStringLiteral("会话握手超时"));
    scheduleReconnect(QStringLiteral("会话握手超时"));
  });
  connect(heartbeatTimer_, &QTimer::timeout, this, [this] {
    if (state_ != ConnectionState::Ready) return;
    const qint64 now = nowMs();
    expireRequests(now);
    if (lastInboundMs_ > 0 && now - lastInboundMs_ > kSessionTimeoutMs) {
      logLink(QStringLiteral("event=session_timeout reason=inbound_stale %1")
                  .arg(linkSnapshot()));
      scheduleReconnect(QStringLiteral("控制会话心跳超时"));
      return;
    }
    if (lastPingSentMs_ > 0 && lastPongMs_ < lastPingSentMs_) {
      ++missedPongs_;
      logLink(QStringLiteral("event=pong_missing missed_total=%1 %2")
                  .arg(missedPongs_).arg(linkSnapshot()));
    }
    sendPing();
    // A steady status line makes "control stopped working" measurable: you can
    // see whether the socket is up, how stale inbound traffic is and how many
    // requests are still unanswered.
    if (lastStatusLogMs_ == 0 || now - lastStatusLogMs_ >= 5000) {
      lastStatusLogMs_ = now;
      logLink(QStringLiteral("event=link_status %1").arg(linkSnapshot()));
    }
  });
}

ControlClient::~ControlClient() { stop(); }

QString ControlClient::linkSnapshot() const {
  const qint64 now = nowMs();
  return QStringLiteral(
             "state=%1 target=%2:%3 session=%4 tcp_connected=%5 "
             "inbound_age_ms=%6 rtt_ms=%7 missed_pongs=%8 pending=%9 "
             "tx_frames=%10 rx_frames=%11 timeouts=%12 bytes_to_write=%13 "
             "seq=%14 attempts=%15")
      .arg(connectionStateText(state_))
      .arg(host_)
      .arg(controlPort_)
      .arg(sessionId_)
      .arg(socket_ != nullptr &&
                   socket_->state() == QAbstractSocket::ConnectedState
               ? 1
               : 0)
      .arg(lastInboundMs_ > 0 ? now - lastInboundMs_ : -1)
      .arg(lastRttMs_)
      .arg(missedPongs_)
      .arg(pendingRequests_.size())
      .arg(diagTxFrames_)
      .arg(diagRxFrames_)
      .arg(diagTimeouts_)
      .arg(socket_ != nullptr ? socket_->bytesToWrite() : -1)
      .arg(lastInboundSequence_)
      .arg(connectAttempts_);
}

void ControlClient::logLink(const QString& message) {
  crawling::AppLogger::write(QStringLiteral("LINK.CONTROL"), message);
}

void ControlClient::logRequest(const QString& message) {
  crawling::AppLogger::write(QStringLiteral("COMM.CONTROL"), message);
}

void ControlClient::start(const QString& host, quint16 controlPort) {
  host_ = host.trimmed();
  controlPort_ = controlPort;
  stopRequested_ = false;
  reconnectDelayMs_ = 500;
  reconnectTimer_->stop();
  if (host_.isEmpty()) {
    setState(ConnectionState::Disconnected, QStringLiteral("主机地址为空"));
    emit errorOccurred(QStringLiteral("未配置远程主机地址"), false);
    return;
  }
  connectSocket();
}

void ControlClient::stop() {
  stopRequested_ = true;
  reconnectTimer_->stop();
  handshakeTimer_->stop();
  heartbeatTimer_->stop();
  decoder_.reset();
  clearPendingRequests(QStringLiteral("控制客户端已停止"));
  sessionId_ = 0;
  sessionKey_.clear();
  capabilities_.clear();
  lastInboundSequence_ = 0;
  if (socket_->state() != QAbstractSocket::UnconnectedState) {
    socket_->abort();
  }
  setState(ConnectionState::Disconnected, QStringLiteral("已停止"));
}

quint64 ControlClient::sendRequest(const QString& operation,
                                   const QJsonObject& data) {
  if (state_ != ConnectionState::Ready || operation.trimmed().isEmpty())
    return 0;
  if (pendingRequests_.size() >= kMaximumPendingRequests) {
    emit errorOccurred(QStringLiteral("待响应控制请求过多，已拒绝新请求"), true);
    return 0;
  }
  if (socket_->bytesToWrite() > kMaxControlPayloadBytes * 2) {
    emit errorOccurred(QStringLiteral("控制发送缓冲已满，已拒绝新请求"), true);
    return 0;
  }
  const quint64 requestId = nextRequestId_++;
  QJsonObject request;
  request.insert(QStringLiteral("operation"), operation);
  if (!data.isEmpty()) request.insert(QStringLiteral("data"), data);
  const int writtenBytes = writeFrame(ControlMessageType::Request, request, requestId);
  if (writtenBytes <= 0) {
    logLink(QStringLiteral("event=request_write_failed operation=%1 request_id=%2 %3")
                .arg(operation)
                .arg(requestId)
                .arg(linkSnapshot()));
    return 0;
  }
  ++diagTxFrames_;
  pendingRequests_.insert(requestId, PendingRequest{operation, nowMs()});
  logRequest(QStringLiteral("event=request_sent operation=%1 request_id=%2 bytes=%3 pending=%4")
                 .arg(operation)
                 .arg(requestId)
                 .arg(writtenBytes)
                 .arg(pendingRequests_.size()));
  return requestId;
}

void ControlClient::connectSocket() {
  if (stopRequested_ || suppressReconnect_) return;
  reconnectTimer_->stop();
  if (socket_->state() != QAbstractSocket::UnconnectedState) {
    suppressReconnect_ = true;
    socket_->abort();
    suppressReconnect_ = false;
  }
  decoder_.reset();
  sessionId_ = 0;
  sessionKey_.clear();
  capabilities_.clear();
  lastInboundSequence_ = 0;
  setState(state_ == ConnectionState::Reconnecting
              ? ConnectionState::Reconnecting
              : ConnectionState::Connecting,
          QStringLiteral("连接 %1:%2").arg(host_).arg(controlPort_));
  // A process-wide system proxy (vendor SDK / OS setting) would wrap this raw
  // TCP socket in an HTTP proxy, which QTcpSocket cannot do; the connection
  // then fails with "The proxy type is invalid for this operation" before any
  // byte reaches the robot host. Our control link is always direct.
  socket_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
  ++connectAttempts_;
  logLink(QStringLiteral("event=connect_start target=%1:%2 attempt=%3")
              .arg(host_)
              .arg(controlPort_)
              .arg(connectAttempts_));
  socket_->connectToHost(host_, controlPort_);
}

void ControlClient::scheduleReconnect(const QString& reason) {
  if (stopRequested_ || suppressReconnect_ || reconnectTimer_->isActive()) return;
  reconnectTimer_->stop();
  heartbeatTimer_->stop();
  handshakeTimer_->stop();
  decoder_.reset();
  clearPendingRequests(reason);
  sessionId_ = 0;
  sessionKey_.clear();
  capabilities_.clear();
  lastInboundSequence_ = 0;
  if (socket_->state() != QAbstractSocket::UnconnectedState) {
    suppressReconnect_ = true;
    socket_->abort();
    suppressReconnect_ = false;
  }
  setState(ConnectionState::Reconnecting,
           QStringLiteral("%1，%2 ms 后重连")
               .arg(reason)
               .arg(reconnectDelayMs_));
  logLink(QStringLiteral("event=reconnect_scheduled reason=\"%1\" delay_ms=%2 %3")
              .arg(reason)
              .arg(reconnectDelayMs_)
              .arg(linkSnapshot()));
  emit errorOccurred(reason, true);
  reconnectTimer_->start(reconnectDelayMs_);
  reconnectDelayMs_ = qMin(reconnectDelayMs_ * 2, 5000);
}

void ControlClient::setState(ConnectionState state, const QString& detail) {
  if (state_ == state) return;
  const ConnectionState previous = state_;
  state_ = state;
  logLink(QStringLiteral("event=state_change from=%1 to=%2 detail=\"%3\" %4")
              .arg(connectionStateText(previous),
                   connectionStateText(state), detail, linkSnapshot()));
  emit connectionStateChanged(state_, detail);
}

void ControlClient::onConnected() {
  if (stopRequested_) {
    suppressReconnect_ = true;
    socket_->abort();
    suppressReconnect_ = false;
    return;
  }
  lastInboundMs_ = nowMs();
  reconnectDelayMs_ = 500;
  logLink(QStringLiteral("event=tcp_connected local=%1 peer=%2")
              .arg(socket_->localAddress().toString())
              .arg(socket_->peerAddress().toString()));
  setState(ConnectionState::Handshaking, QStringLiteral("TCP 已连接，正在握手"));
  handshakeTimer_->start();
  sendHello();
}

void ControlClient::sendHello() {
  QJsonObject hello;
  hello.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
  hello.insert(QStringLiteral("clientId"), QHostInfo::localHostName());
  hello.insert(QStringLiteral("clientName"), QStringLiteral("PA1664Workbench"));
  hello.insert(QStringLiteral("capabilities"),
               QJsonArray{QStringLiteral("control.v1"), QStringLiteral("ui.preserve")});
  helloRequestId_ = nextRequestId_++;
  writeFrame(ControlMessageType::Hello, hello, helloRequestId_);
}

void ControlClient::sendPing() {
  QJsonObject ping;
  ping.insert(QStringLiteral("clientTimeMs"), QString::number(nowMs()));
  const quint64 pingId = nextRequestId_++;
  lastPingSentMs_ = nowMs();
  writeFrame(ControlMessageType::Ping, ping, pingId);
}

void ControlClient::onReadyRead() {
  lastInboundMs_ = nowMs();
  if (socket_->bytesAvailable() > kMaxControlPayloadBytes + kControlHeaderBytes) {
    failProtocol(QStringLiteral("控制消息读取缓冲超限"));
    scheduleReconnect(QStringLiteral("控制消息读取缓冲超限"));
    return;
  }
  DecodeError error;
  const QVector<ControlFrame> frames = decoder_.append(socket_->readAll(), &error);
  if (!error.ok()) {
    failProtocol(error.detail);
    scheduleReconnect(error.detail);
    return;
  }
  for (const ControlFrame& frame : frames) handleFrame(frame);
}

void ControlClient::onDisconnected() {
  heartbeatTimer_->stop();
  handshakeTimer_->stop();
  logLink(QStringLiteral("event=tcp_disconnected stop_requested=%1 %2")
              .arg(stopRequested_ ? 1 : 0)
              .arg(linkSnapshot()));
  if (!stopRequested_) {
    scheduleReconnect(QStringLiteral("主机连接已断开"));
  } else {
    setState(ConnectionState::Disconnected, QStringLiteral("已断开"));
  }
}

void ControlClient::onSocketError() {
  if (stopRequested_ || suppressReconnect_ || reconnectTimer_->isActive()) return;
  const QString detail = socket_->errorString();
  logLink(QStringLiteral("event=socket_error code=%1 text=\"%2\" %3")
              .arg(int(socket_->error()))
              .arg(detail, linkSnapshot()));
  emit errorOccurred(detail, true);
  if (state_ != ConnectionState::Reconnecting) scheduleReconnect(detail);
}

void ControlClient::handleFrame(const ControlFrame& frame) {
  ++diagRxFrames_;
  if (frame.type == ControlMessageType::HelloAck) {
    if (lastInboundSequence_ != 0) {
      failProtocol(QStringLiteral("重复的握手响应"));
      scheduleReconnect(QStringLiteral("重复握手响应"));
      return;
    }
    if (frame.sequence == 0 || frame.sequence <= lastInboundSequence_) {
      failProtocol(QStringLiteral("握手响应序号无效"));
      scheduleReconnect(QStringLiteral("握手响应序号无效"));
      return;
    }
    lastInboundSequence_ = frame.sequence;
    handleHelloAck(frame);
    return;
  }
  if (state_ != ConnectionState::Ready) {
    failProtocol(QStringLiteral("握手完成前收到业务消息"));
    scheduleReconnect(QStringLiteral("握手状态异常"));
    return;
  }
  if (frame.sessionId != sessionId_) {
    failProtocol(QStringLiteral("控制帧会话号不匹配"));
    scheduleReconnect(QStringLiteral("控制帧会话号不匹配"));
    return;
  }
  if (frame.sequence == 0 || frame.sequence <= lastInboundSequence_) {
    failProtocol(QStringLiteral("控制帧序号回退"));
    scheduleReconnect(QStringLiteral("控制帧序号回退"));
    return;
  }
  lastInboundSequence_ = frame.sequence;
  if (frame.type == ControlMessageType::Response) {
    bool ok = false;
    const QJsonObject response = objectFromPayload(frame.payload, &ok);
    if (!ok) {
      failProtocol(QStringLiteral("响应载荷不是 JSON 对象"));
      scheduleReconnect(QStringLiteral("控制响应格式错误"));
      return;
    }
    pendingRequests_.remove(frame.requestId);
    emit responseReceived(frame.requestId, response);
    return;
  }
  if (frame.type == ControlMessageType::Event) {
    bool ok = false;
    const QJsonObject event = objectFromPayload(frame.payload, &ok);
    if (!ok) {
      emit errorOccurred(QStringLiteral("事件载荷不是 JSON 对象"), true);
      return;
    }
    const QString name = event.value(QStringLiteral("event")).toString();
    emit eventReceived(name, event);
    return;
  }
  if (frame.type == ControlMessageType::Pong) {
    lastPongMs_ = nowMs();
    if (lastPingSentMs_ > 0) {
      lastRttMs_ = int(lastPongMs_ - lastPingSentMs_);
      lastPingSentMs_ = 0;
    }
    return;
  }
  if (frame.type == ControlMessageType::Error) {
    bool ok = false;
    const QJsonObject error = objectFromPayload(frame.payload, &ok);
    emit errorOccurred(error.value(QStringLiteral("errorText")).toString(
                           QStringLiteral("主机返回协议错误")), false);
    return;
  }
  emit errorOccurred(QStringLiteral("收到未处理的控制消息类型：%1")
                         .arg(controlMessageTypeText(frame.type)),
                     true);
}

void ControlClient::handleHelloAck(const ControlFrame& frame) {
  if (frame.requestId != helloRequestId_) {
    failProtocol(QStringLiteral("握手响应 requestId 不匹配"));
    scheduleReconnect(QStringLiteral("握手响应失配"));
    return;
  }
  bool ok = false;
  const QJsonObject ack = objectFromPayload(frame.payload, &ok);
  if (!ok) {
    failProtocol(QStringLiteral("握手响应载荷不是 JSON 对象"));
    scheduleReconnect(QStringLiteral("握手响应格式错误"));
    return;
  }
  bool idOk = false;
  const quint64 sessionId =
      ack.value(QStringLiteral("sessionId")).toString().toULongLong(&idOk);
  const QByteArray key = QByteArray::fromBase64(
      ack.value(QStringLiteral("sessionToken")).toString().toLatin1());
  const int version = ack.value(QStringLiteral("protocolVersion")).toInt();
  if (!idOk || sessionId == 0 || key.size() != 32 || version != kProtocolVersion) {
    failProtocol(QStringLiteral("握手响应字段无效"));
    scheduleReconnect(QStringLiteral("握手响应无效"));
    return;
  }

  if (frame.sessionId != sessionId) {
    failProtocol(QStringLiteral("握手响应会话号不一致"));
    scheduleReconnect(QStringLiteral("握手响应会话号不一致"));
    return;
  }
  handshakeTimer_->stop();
  sessionId_ = sessionId;
  sessionKey_ = key;
  realtimePort_ = static_cast<quint16>(
      ack.value(QStringLiteral("realtimePort")).toInt(kDefaultRealtimePort));
  mediaPort_ = static_cast<quint16>(
      ack.value(QStringLiteral("mediaPort")).toInt(kDefaultMediaPort));
  bulkPort_ = static_cast<quint16>(
      ack.value(QStringLiteral("bulkPort")).toInt(kDefaultBulkPort));
  capabilities_.clear();
  const QJsonArray capabilityArray =
      ack.value(QStringLiteral("capabilities")).toArray();
  for (const QJsonValue& value : capabilityArray) {
    const QString capability = value.toString().trimmed();
    if (!capability.isEmpty()) capabilities_.append(capability);
  }
  capabilities_.removeDuplicates();
  const int heartbeatMs = ack.value(QStringLiteral("heartbeatIntervalMs"))
                              .toInt(kHeartbeatIntervalMs);
  heartbeatTimer_->setInterval(qBound(100, heartbeatMs, 2000));
  heartbeatTimer_->start();
  lastInboundMs_ = nowMs();
  lastPingSentMs_ = 0;
  lastPongMs_ = 0;
  missedPongs_ = 0;
  logLink(QStringLiteral("event=handshake_ok session=%1 heartbeat_ms=%2 "
                         "ports rt=%3 media=%4 bulk=%5 caps=%6")
              .arg(sessionId_)
              .arg(heartbeatTimer_->interval())
              .arg(realtimePort_)
              .arg(mediaPort_)
              .arg(bulkPort_)
              .arg(capabilities_.join(QLatin1Char(','))));
  setState(ConnectionState::Ready,
           QStringLiteral("会话 %1 已就绪").arg(sessionId_));
  emit sessionReady(sessionId_, sessionKey_, realtimePort_, mediaPort_,
                    bulkPort_, heartbeatTimer_->interval());
}

void ControlClient::failProtocol(const QString& detail) {
  logLink(QStringLiteral("event=protocol_error detail=\"%1\" %2")
              .arg(detail, linkSnapshot()));
  emit errorOccurred(detail, true);
}

void ControlClient::expireRequests(qint64 now) {
  if (pendingRequests_.isEmpty()) return;
  const auto ids = pendingRequests_.keys();
  for (quint64 id : ids) {
    const auto it = pendingRequests_.constFind(id);
    if (it == pendingRequests_.constEnd()) continue;
    if (now - it.value().sentAtMs <= kRequestTimeoutMs) continue;
    const QString operation = it.value().operation;
    const qint64 elapsed = now - it.value().sentAtMs;
    pendingRequests_.remove(id);
    ++diagTimeouts_;
    logLink(QStringLiteral("event=request_timeout operation=%1 request_id=%2 "
                           "elapsed_ms=%3 %4")
                .arg(operation)
                .arg(id)
                .arg(elapsed)
                .arg(linkSnapshot()));
    emit requestTimedOut(id, operation);
  }
}

void ControlClient::clearPendingRequests(const QString& reason) {
  if (pendingRequests_.isEmpty()) return;
  const auto pending = pendingRequests_;
  pendingRequests_.clear();
  if (!pending.isEmpty()) {
    logLink(QStringLiteral("event=pending_dropped count=%1 reason=\"%2\" %3")
                .arg(pending.size())
                .arg(reason, linkSnapshot()));
  }
  for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
    emit requestTimedOut(it.key(), it.value().operation);
  }
}

int ControlClient::writeFrame(ControlMessageType type,
                              const QJsonObject& payload,
                              quint64 requestId) {
  if (socket_->state() != QAbstractSocket::ConnectedState) return 0;
  ControlFrame frame;
  frame.type = type;
  frame.sessionId = sessionId_;
  frame.sequence = nextSequence_++;
  frame.requestId = requestId;
  frame.timestampUs = nowUs();
  frame.payload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  DecodeError error;
  const QByteArray bytes = ControlCodec::encode(frame, &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail, false);
    return 0;
  }
  return static_cast<int>(socket_->write(bytes));
}

QJsonObject ControlClient::objectFromPayload(const QByteArray& payload, bool* ok) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (ok) *ok = error.error == QJsonParseError::NoError && document.isObject();
  return document.object();
}

}  // namespace pa1664::remote

