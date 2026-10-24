#include "bulk_channel.h"

#include <QAbstractSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace pa1664::remote {

BulkClient::BulkClient(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this)),
      reconnectTimer_(new QTimer(this)), heartbeatTimer_(new QTimer(this)) {
  reconnectTimer_->setSingleShot(true);
  reconnectTimer_->setInterval(1000);
  heartbeatTimer_->setInterval(1000);
  connect(reconnectTimer_, &QTimer::timeout, this, &BulkClient::connectNow);
  connect(heartbeatTimer_, &QTimer::timeout, this, &BulkClient::sendHeartbeat);
  connect(socket_, &QTcpSocket::connected, this, &BulkClient::onConnected);
  connect(socket_, &QTcpSocket::readyRead, this, &BulkClient::onReadyRead);
  connect(socket_, &QTcpSocket::disconnected, this, &BulkClient::onDisconnected);
  connect(socket_, &QTcpSocket::errorOccurred, this, &BulkClient::onSocketError);
}

BulkClient::~BulkClient() { stop(); }

bool BulkClient::isConnected() const {
  return socket_->state() == QAbstractSocket::ConnectedState;
}

void BulkClient::start(const QString& host, quint16 port, quint64 sessionId,
                       const QByteArray& sessionKey) {
  stop();
  QHostAddress address;
  if (!address.setAddress(host.trimmed()) || sessionId == 0 ||
      sessionKey.size() != 32) {
    emit errorOccurred(QStringLiteral("批量通道会话参数无效"));
    return;
  }
  host_ = address.toString();
  port_ = port;
  sessionId_ = sessionId;
  sessionKey_ = sessionKey;
  decoder_.setSessionKey(sessionKey);
  nextSequence_ = 1;
  active_ = true;
  connectNow();
}

void BulkClient::stop() {
  active_ = false;
  reconnectTimer_->stop();
  heartbeatTimer_->stop();
  decoder_.reset();
  if (socket_->state() != QAbstractSocket::UnconnectedState) socket_->abort();
  sessionId_ = 0;
  sessionKey_.clear();
}

void BulkClient::connectNow() {
  if (!active_ || socket_->state() != QAbstractSocket::UnconnectedState) return;
  decoder_.reset();
  socket_->connectToHost(host_, port_);
}

void BulkClient::onConnected() {
  heartbeatTimer_->start();
  emit connected();
  sendHeartbeat();
}

void BulkClient::onReadyRead() {
  if (socket_->bytesAvailable() > 2 * kMaxBulkPayloadBytes) {
    emit errorOccurred(QStringLiteral("批量通道读取缓冲超限"));
    socket_->abort();
    return;
  }
  DecodeError error;
  const QVector<BulkFrame> frames = decoder_.append(socket_->readAll(), &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail);
    socket_->abort();
    return;
  }
  for (const BulkFrame& frame : frames) {
    if (frame.sessionId != sessionId_ || frame.kind == BulkStreamKind::Heartbeat)
      continue;
    emit messageReceived(frame.kind, frame.sequence, frame.payload);
  }
}

void BulkClient::onDisconnected() {
  heartbeatTimer_->stop();
  if (!active_) return;
  emit disconnected(QStringLiteral("批量通道连接已断开"));
  if (!reconnectTimer_->isActive()) reconnectTimer_->start();
}

void BulkClient::onSocketError() {
  if (!active_) return;
  emit errorOccurred(QStringLiteral("批量通道：%1").arg(socket_->errorString()));
  if (socket_->state() == QAbstractSocket::UnconnectedState &&
      !reconnectTimer_->isActive()) {
    reconnectTimer_->start();
  }
}

void BulkClient::sendHeartbeat() {
  if (!isConnected()) return;
  BulkFrame frame;
  frame.kind = BulkStreamKind::Heartbeat;
  frame.sessionId = sessionId_;
  frame.sequence = nextSequence_++;
  DecodeError error;
  const QByteArray bytes = BulkCodec::encode(frame, sessionKey_, &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail);
    return;
  }
  socket_->write(bytes);
}

quint64 BulkClient::send(BulkStreamKind kind, const QByteArray& payload) {
  if (!isConnected() || sessionId_ == 0 ||
      payload.size() > kMaxBulkPayloadBytes) {
    return 0;
  }
  BulkFrame frame;
  frame.kind = kind;
  frame.sessionId = sessionId_;
  frame.sequence = nextSequence_++;
  frame.payload = payload;
  DecodeError error;
  const QByteArray bytes = BulkCodec::encode(frame, sessionKey_, &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail);
    return 0;
  }
  if (socket_->write(bytes) != bytes.size()) {
    emit errorOccurred(QStringLiteral("批量通道发送失败：%1")
                           .arg(socket_->errorString()));
    return 0;
  }
  return frame.sequence;
}

BulkServer::BulkServer(QObject* parent)
    : QObject(parent), server_(new QTcpServer(this)) {
  connect(server_, &QTcpServer::newConnection, this,
          &BulkServer::acceptConnections);
}

BulkServer::~BulkServer() { stop(); }

bool BulkServer::start(const QHostAddress& address, quint16 port,
                       QString* error) {
  stop();
  if (!server_->listen(address, port)) {
    if (error) *error = server_->errorString();
    return false;
  }
  return true;
}

void BulkServer::stop() {
  const auto clients = clients_.values();
  for (Client* client : clients) closeClient(client, QStringLiteral("主机停止"));
  server_->close();
  sessionKeys_.clear();
}

bool BulkServer::isListening() const { return server_->isListening(); }
quint16 BulkServer::serverPort() const { return server_->serverPort(); }

void BulkServer::registerSession(quint64 sessionId,
                                 const QByteArray& sessionKey) {
  if (sessionId == 0 || sessionKey.size() != 32) return;
  sessionKeys_.insert(sessionId, sessionKey);
}

void BulkServer::removeSession(quint64 sessionId) {
  sessionKeys_.remove(sessionId);
  Client* client = bySession_.value(sessionId, nullptr);
  if (client) closeClient(client, QStringLiteral("控制会话结束"));
}

bool BulkServer::sendMessage(quint64 sessionId, BulkStreamKind kind,
                             const QByteArray& payload) {
  Client* client = bySession_.value(sessionId, nullptr);
  if (!client || !client->authenticated || !client->socket ||
      payload.size() > kMaxBulkPayloadBytes ||
      client->socket->bytesToWrite() > kBulkWriteHighWaterBytes) {
    return false;
  }
  BulkFrame frame;
  frame.kind = kind;
  frame.sessionId = sessionId;
  frame.sequence = client->nextOutboundSequence++;
  frame.payload = payload;
  DecodeError error;
  const QByteArray bytes =
      BulkCodec::encode(frame, sessionKeys_.value(sessionId), &error);
  if (!error.ok()) {
    emit errorOccurred(sessionId, error.detail);
    return false;
  }
  return client->socket->write(bytes) == bytes.size();
}

void BulkServer::acceptConnections() {
  while (server_->hasPendingConnections()) {
    QTcpSocket* socket = server_->nextPendingConnection();
    auto* client = new Client;
    client->socket = socket;
    clients_.insert(socket, client);
    socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    connect(socket, &QTcpSocket::readyRead, this,
            [this, client] { readClient(client); });
    connect(socket, &QTcpSocket::disconnected, this, [this, client] {
      closeClient(client, QStringLiteral("批量通道客户端断开"));
    });
  }
}

void BulkServer::readClient(Client* client) {
  if (!client || !client->socket) return;
  QTcpSocket* socket = client->socket;
  if (socket->bytesAvailable() > 2 * kMaxBulkPayloadBytes) {
    closeClient(client, QStringLiteral("批量通道读取缓冲超限"));
    return;
  }
  QByteArray bytes = client->authBuffer;
  client->authBuffer.clear();
  bytes.append(socket->readAll());
  if (!client->authenticated && client->sessionId == 0) {
    if (bytes.size() < kBulkHeaderBytes) {
      client->authBuffer = bytes;
      return;
    }
    quint64 sessionId = 0;
    DecodeError error;
    const DecodeResult peek = BulkCodec::peekSessionId(bytes, &sessionId, &error);
    if (peek != DecodeResult::Ok) {
      closeClient(client, error.detail.isEmpty()
                              ? QStringLiteral("批量通道会话头无效")
                              : error.detail);
      return;
    }
    const QByteArray key = sessionKeys_.value(sessionId);
    if (key.size() != 32) {
      closeClient(client, QStringLiteral("批量通道会话未授权"));
      return;
    }
    if (bySession_.contains(sessionId)) {
      closeClient(client, QStringLiteral("重复的批量通道会话"));
      return;
    }
    client->sessionId = sessionId;
    client->decoder.setSessionKey(key);
  }
  DecodeError error;
  const QVector<BulkFrame> frames = client->decoder.append(bytes, &error);
  if (!error.ok()) {
    closeClient(client, error.detail);
    return;
  }
  if (!client->authenticated) {
    if (frames.isEmpty())
      return;
    client->authenticated = true;
    bySession_.insert(client->sessionId, client);
    emit sessionAuthenticated(client->sessionId);
  }
  for (const BulkFrame& frame : frames) {
    if (frame.sessionId != client->sessionId ||
        frame.sequence <= client->lastInboundSequence) {
      closeClient(client, QStringLiteral("批量通道序号或会话号无效"));
      return;
    }
    client->lastInboundSequence = frame.sequence;
    if (frame.kind != BulkStreamKind::Heartbeat) {
      emit messageReceived(client->sessionId, frame.kind, frame.sequence,
                           frame.payload);
    }
  }
}

void BulkServer::closeClient(Client* client, const QString& reason) {
  if (!client) return;
  QTcpSocket* socket = client->socket;
  clients_.remove(socket);
  if (client->sessionId != 0) bySession_.remove(client->sessionId);
  if (socket) {
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
  const quint64 sessionId = client->sessionId;
  delete client;
  if (sessionId != 0) emit errorOccurred(sessionId, reason);
}

}  // namespace pa1664::remote
