#include "control_server.h"
#include "protocol/realtime_codec.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace pa1664::remote {
namespace {

quint64 nowUs() {
  return static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000u;
}

QJsonObject parseObject(const QByteArray& payload, bool* ok) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (ok) *ok = error.error == QJsonParseError::NoError && document.isObject();
  return document.object();
}

}  // namespace

ControlServer::ControlServer(QObject* parent)
    : QObject(parent),
      server_(new QTcpServer(this)),
      maintenanceTimer_(new QTimer(this)) {
  connect(server_, &QTcpServer::newConnection, this,
          &ControlServer::acceptConnections);
  capabilities_ = QStringList{
      QString::fromLatin1(kCapabilityRobotControlV1),
      QString::fromLatin1(kCapabilityRobotTelemetryV1),
      QString::fromLatin1(kCapabilityRobotSettingsV1),
  };
  maintenanceTimer_->setInterval(250);
  connect(maintenanceTimer_, &QTimer::timeout, this,
          &ControlServer::maintainSessions);
}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(const QHostAddress& address, quint16 port,
                          QString* error) {
  if (error) error->clear();
  if (server_->isListening()) return true;
  if (!server_->listen(address, port)) {
    if (error) *error = server_->errorString();
    return false;
  }
  maintenanceTimer_->start();
  return true;
}

void ControlServer::stop() {
  const auto clients = bySocket_.keys();
  for (QTcpSocket* socket : clients) {
    ClientState* client = bySocket_.value(socket, nullptr);
    if (!client) continue;
    bySocket_.remove(socket);
    sessions_.remove(client->sessionId);
    delete client;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
  sessions_.clear();
  maintenanceTimer_->stop();
  if (server_->isListening()) server_->close();
}

bool ControlServer::isListening() const { return server_->isListening(); }

quint16 ControlServer::serverPort() const { return server_->serverPort(); }

int ControlServer::sessionCount() const { return sessions_.size(); }

QByteArray ControlServer::sessionKey(quint64 sessionId) const {
  const ClientState* client = sessions_.value(sessionId, nullptr);
  return client ? client->sessionKey : QByteArray{};
}

QString ControlServer::sessionPeer(quint64 sessionId) const {
  const ClientState* client = sessions_.value(sessionId, nullptr);
  return client ? client->peerAddress : QString{};
}

QString ControlServer::sessionPeerAddress(quint64 sessionId) const {
  const ClientState* client = sessions_.value(sessionId, nullptr);
  if (!client || !client->socket) return {};
  return client->socket->peerAddress().toString();
}

void ControlServer::closeSession(quint64 sessionId, const QString& reason) {
  ClientState* client = findClient(sessionId);
  if (client) removeClient(client, reason);
}

void ControlServer::sendResponse(quint64 sessionId, quint64 requestId, bool ok,
                                 const QJsonObject& data,
                                 const QString& errorCode,
                                 const QString& errorText) {
  ClientState* client = findClient(sessionId);
  if (!client || !client->handshakeComplete) return;
  QJsonObject payload;
  payload.insert(QStringLiteral("ok"), ok);
  if (!data.isEmpty()) payload.insert(QStringLiteral("data"), data);
  if (!errorCode.isEmpty()) payload.insert(QStringLiteral("errorCode"), errorCode);
  if (!errorText.isEmpty()) payload.insert(QStringLiteral("errorText"), errorText);
  writeFrame(client, ControlMessageType::Response, payload, requestId,
             ControlFlags::ResponseToRequest);
}

void ControlServer::sendEvent(quint64 sessionId, const QString& eventName,
                              const QJsonObject& data) {
  ClientState* client = findClient(sessionId);
  if (!client || !client->handshakeComplete) return;
  QJsonObject payload = data;
  payload.insert(QStringLiteral("event"), eventName);
  writeFrame(client, ControlMessageType::Event, payload);
}

void ControlServer::broadcastEvent(const QString& eventName,
                                   const QJsonObject& data) {
  const auto ids = sessions_.keys();
  for (quint64 id : ids) sendEvent(id, eventName, data);
}

void ControlServer::acceptConnections() {
  while (server_->hasPendingConnections()) {
    QTcpSocket* socket = server_->nextPendingConnection();
    if (!socket) continue;
    if (bySocket_.size() >= 8) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    socket->setReadBufferSize(kMaxControlPayloadBytes + kControlHeaderBytes);

    auto* client = new ClientState;
    client->socket = socket;
    client->peerAddress = QStringLiteral("%1:%2")
                              .arg(socket->peerAddress().toString())
                              .arg(socket->peerPort());
    client->handshakeDeadlineMs = QDateTime::currentMSecsSinceEpoch() + 5000;
    bySocket_.insert(socket, client);

    connect(socket, &QTcpSocket::readyRead, this,
            [this, client] { readClient(client); });
    connect(socket, &QTcpSocket::disconnected, this,
            [this, client] { removeClient(client, QStringLiteral("客户端断开")); });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, client](QAbstractSocket::SocketError) {
              if (!client || !client->socket) return;
              emit protocolError(client->sessionId, client->socket->errorString());
            });
  }
}

void ControlServer::maintainSessions() {
  if (bySocket_.isEmpty()) return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const auto sockets = bySocket_.keys();
  for (QTcpSocket* socket : sockets) {
    ClientState* client = bySocket_.value(socket, nullptr);
    if (!client) continue;
    if (!client->handshakeComplete && client->handshakeDeadlineMs > 0 &&
        now > client->handshakeDeadlineMs) {
      removeClient(client, QStringLiteral("会话握手超时"));
      continue;
    }
    if (client->handshakeComplete && client->lastInboundMs > 0 &&
        now - client->lastInboundMs > 2 * kSessionTimeoutMs) {
      removeClient(client, QStringLiteral("控制会话心跳超时"));
      continue;
    }
    if (socket->bytesToWrite() > 4 * kMaxControlPayloadBytes) {
      removeClient(client, QStringLiteral("控制发送缓冲持续积压"));
    }
  }
}

void ControlServer::readClient(ClientState* client) {
  if (!client || !client->socket) return;
  QTcpSocket* socket = client->socket;
  if (socket->bytesAvailable() > kMaxControlPayloadBytes + kControlHeaderBytes) {
    removeClient(client, QStringLiteral("控制消息读取缓冲超限"));
    return;
  }

  DecodeError error;
  const QVector<ControlFrame> frames = client->decoder.append(socket->readAll(), &error);
  if (!error.ok()) {
    emit protocolError(client->sessionId, error.detail);
    removeClient(client, error.detail);
    return;
  }
  for (const ControlFrame& frame : frames) handleFrame(client, frame);
}

void ControlServer::handleFrame(ClientState* client, const ControlFrame& frame) {
  if (!client) return;
  client->lastInboundMs = QDateTime::currentMSecsSinceEpoch();
  if (frame.sequence <= client->lastInboundSequence) {
    removeClient(client, QStringLiteral("控制消息序号回退"));
    return;
  }
  client->lastInboundSequence = frame.sequence;

  if (!client->handshakeComplete) {
    if (frame.type != ControlMessageType::Hello || frame.sessionId != 0) {
      writeError(client, QStringLiteral("handshake_required"),
                 QStringLiteral("必须先完成会话握手"));
      removeClient(client, QStringLiteral("握手前发送业务消息"));
      return;
    }
    handleHello(client, frame);
    return;
  }
  handleAuthenticatedFrame(client, frame);
}

void ControlServer::handleHello(ClientState* client, const ControlFrame& frame) {
  if (client->handshakeComplete) {
    removeClient(client, QStringLiteral("重复发送会话握手"));
    return;
  }
  bool ok = false;
  const QJsonObject hello = parseObject(frame.payload, &ok);
  if (!ok) {
    writeError(client, QStringLiteral("invalid_hello"),
               QStringLiteral("握手载荷不是 JSON 对象"));
    removeClient(client, QStringLiteral("非法握手载荷"));
    return;
  }
  const int protocolVersion = hello.value(QStringLiteral("protocolVersion")).toInt();
  if (protocolVersion != kProtocolVersion) {
    writeError(client, QStringLiteral("protocol_version"),
               QStringLiteral("协议版本不兼容，主机版本=%1，客户端版本=%2")
                   .arg(kProtocolVersion)
                   .arg(protocolVersion));
    removeClient(client, QStringLiteral("协议版本不兼容"));
    return;
  }

  client->sessionId = nextSessionId_++;
  client->sessionKey = RealtimeCodec::makeSessionKey();
  client->clientId = hello.value(QStringLiteral("clientId")).toString();
  client->handshakeDeadlineMs = 0;
  client->lastInboundMs = QDateTime::currentMSecsSinceEpoch();
  client->handshakeComplete = true;
  sessions_.insert(client->sessionId, client);

  QJsonObject ack;
  ack.insert(QStringLiteral("sessionId"), QString::number(client->sessionId));
  ack.insert(QStringLiteral("sessionToken"),
             QString::fromLatin1(client->sessionKey.toBase64()));
  ack.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
  ack.insert(QStringLiteral("realtimePort"), advertisedRealtimePort_);
  ack.insert(QStringLiteral("mediaPort"), advertisedMediaPort_);
  ack.insert(QStringLiteral("bulkPort"), advertisedBulkPort_);
  ack.insert(QStringLiteral("heartbeatIntervalMs"), kHeartbeatIntervalMs);
  ack.insert(QStringLiteral("motionSendIntervalMs"), kMotionSendIntervalMs);
  ack.insert(QStringLiteral("motionLeaseMs"), kMotionLeaseMs);
  QJsonArray capabilityArray;
  for (const QString& capability : capabilities_)
    capabilityArray.append(capability);
  ack.insert(QStringLiteral("capabilities"), capabilityArray);
  ack.insert(QStringLiteral("hostName"), QHostInfo::localHostName());
  writeFrame(client, ControlMessageType::HelloAck, ack, frame.requestId);
  emit sessionOpened(client->sessionId, client->clientId, client->peerAddress);
}

void ControlServer::handleAuthenticatedFrame(ClientState* client,
                                             const ControlFrame& frame) {
  if (frame.sessionId != client->sessionId) {
    removeClient(client, QStringLiteral("控制帧会话号不匹配"));
    return;
  }
  if (frame.type == ControlMessageType::Ping) {
    writeFrame(client, ControlMessageType::Pong, {}, frame.requestId);
    return;
  }
  if (frame.type != ControlMessageType::Request) {
    writeError(client, QStringLiteral("unsupported_message"),
               QStringLiteral("当前只接受 request/ping 控制消息"));
    return;
  }

  bool ok = false;
  const QJsonObject request = parseObject(frame.payload, &ok);
  if (!ok) {
    sendResponse(client->sessionId, frame.requestId, false, {},
                 QStringLiteral("invalid_request"),
                 QStringLiteral("请求载荷不是 JSON 对象"));
    return;
  }
  emit requestReceived(client->sessionId, frame.requestId, request);
}

void ControlServer::writeFrame(ClientState* client, ControlMessageType type,
                               const QJsonObject& payload, quint64 requestId,
                               ControlFlags flags) {
  if (!client || !client->socket) return;
  ControlFrame frame;
  frame.type = type;
  frame.flags = flags;
  frame.sessionId = client->sessionId;
  frame.sequence = client->nextOutboundSequence++;
  frame.requestId = requestId;
  frame.timestampUs = nowUs();
  frame.payload = jsonBytes(payload);
  DecodeError error;
  const QByteArray bytes = ControlCodec::encode(frame, &error);
  if (!error.ok()) {
    emit protocolError(client->sessionId, error.detail);
    return;
  }
  if (client->socket->bytesToWrite() > 2 * kMaxControlPayloadBytes) {
    removeClient(client, QStringLiteral("控制发送缓冲超限"));
    return;
  }
  client->socket->write(bytes);
}

void ControlServer::writeError(ClientState* client, const QString& code,
                               const QString& text, quint64 requestId) {
  if (!client || !client->socket) return;
  QJsonObject payload;
  payload.insert(QStringLiteral("ok"), false);
  payload.insert(QStringLiteral("errorCode"), code);
  payload.insert(QStringLiteral("errorText"), text);
  writeFrame(client, ControlMessageType::Error, payload, requestId);
}

void ControlServer::removeClient(ClientState* client, const QString& reason) {
  if (!client) return;
  QTcpSocket* socket = client->socket;
  const quint64 sessionId = client->sessionId;
  if (socket) {
    bySocket_.remove(socket);
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
  if (sessionId != 0) sessions_.remove(sessionId);
  delete client;
  if (sessionId != 0) emit sessionClosed(sessionId, reason);
}

ControlServer::ClientState* ControlServer::findClient(quint64 sessionId) const {
  return sessions_.value(sessionId, nullptr);
}

QByteArray ControlServer::jsonBytes(const QJsonObject& object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

}  // namespace pa1664::remote

