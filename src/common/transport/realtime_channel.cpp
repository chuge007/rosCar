#include "realtime_channel.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <cmath>

namespace pa1664::remote {
namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

qint64 nowUs() { return nowMs() * 1000; }

QJsonObject parseJsonObject(const QByteArray& bytes, bool* ok) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (ok) *ok = error.error == QJsonParseError::NoError && document.isObject();
  return document.object();
}

QByteArray jsonBytes(const QJsonObject& object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray fittingRealtimePayload(const QJsonObject& object) {
  const int maximumPayload =
      kMaxRealtimeDatagramBytes - kRealtimeHeaderBytes - kRealtimeAuthTagBytes;
  QByteArray bytes = jsonBytes(object);
  if (bytes.size() <= maximumPayload) return bytes;

  // Telemetry is a latest-value packet.  Preserve the numeric safety fields
  // and bound only diagnostic strings rather than dropping the whole packet
  // when a drive controller produces a long fault reason.
  QJsonObject compact = object;
  const QString reason = compact.value(QStringLiteral("reason")).toString();
  if (!reason.isEmpty()) {
    compact.insert(QStringLiteral("reason"), reason.left(160));
    bytes = jsonBytes(compact);
  }
  if (bytes.size() <= maximumPayload) return bytes;
  compact.remove(QStringLiteral("reason"));
  bytes = jsonBytes(compact);
  return bytes.size() <= maximumPayload ? bytes : QByteArray{};
}

}  // namespace

RealtimeClient::RealtimeClient(QObject* parent)
    : QObject(parent),
      socket_(new QUdpSocket(this)),
      sendTimer_(new QTimer(this)) {
  sendTimer_->setTimerType(Qt::PreciseTimer);
  sendTimer_->setInterval(kMotionSendIntervalMs);
  connect(sendTimer_, &QTimer::timeout, this, [this] {
    const qint64 now = clock_.elapsed();
    if (emergencyRepeats_ > 0) {
      sendEmergencyStop();
      --emergencyRepeats_;
    }
    if (motionActive_) sendMotion(now);
    if (lastHeartbeatMs_ < 0 ||
        now - lastHeartbeatMs_ >= kHeartbeatIntervalMs) {
      sendHeartbeat(now);
    }
  });
  connect(socket_, &QUdpSocket::readyRead, this,
          &RealtimeClient::processDatagrams);
}

RealtimeClient::~RealtimeClient() { stop(); }

void RealtimeClient::start(const QString& host, quint16 port,
                           quint64 sessionId, const QByteArray& sessionKey) {
  stop();
  QHostAddress address;
  if (!address.setAddress(host.trimmed())) {
    emit errorOccurred(QStringLiteral("实时通道需要配置 IP 地址：%1").arg(host));
    return;
  }
  if (sessionId == 0 || sessionKey.size() != 32) {
    emit errorOccurred(QStringLiteral("实时通道会话参数无效"));
    return;
  }
  if (!socket_->bind(QHostAddress::AnyIPv4, 0)) {
    emit errorOccurred(QStringLiteral("实时 UDP 绑定失败：%1")
                           .arg(socket_->errorString()));
    return;
  }
  peerAddress_ = address;
  peerPort_ = port;
  sessionId_ = sessionId;
  sessionKey_ = sessionKey;
  sequence_ = 1;
  lastInboundSequence_ = 0;
  active_ = true;
  clock_.restart();
  lastHeartbeatMs_ = -1;
  sendTimer_->start();
  sendHeartbeat(clock_.elapsed());
}

void RealtimeClient::stop() {
  active_ = false;
  sendTimer_->stop();
  if (socket_->state() != QAbstractSocket::UnconnectedState) socket_->close();
  socket_->close();
  sessionId_ = 0;
  sessionKey_.clear();
  lastInboundSequence_ = 0;
  motionActive_ = false;
  linearMilliMps_ = 0;
  angularMilliRadps_ = 0;
  emergencyRepeats_ = 0;
}

void RealtimeClient::setMotionSetpoint(double linearMps, double angularRadps,
                                       quint16 leaseMs) {
  if (!std::isfinite(linearMps) || !std::isfinite(angularRadps)) return;
  constexpr double kMilli = 1000.0;
  linearMilliMps_ = static_cast<qint32>(std::llround(linearMps * kMilli));
  angularMilliRadps_ =
      static_cast<qint32>(std::llround(angularRadps * kMilli));
  motionLeaseMs_ = qBound<quint16>(50, leaseMs, 1000);
  motionActive_ = true;
  if (active_) sendMotion(clock_.elapsed());
}

void RealtimeClient::clearMotionSetpoint() {
  linearMilliMps_ = 0;
  angularMilliRadps_ = 0;
  motionActive_ = false;
  if (active_) sendMotion(clock_.elapsed());
}

void RealtimeClient::emergencyStop() {
  linearMilliMps_ = 0;
  angularMilliRadps_ = 0;
  motionActive_ = false;
  emergencyRepeats_ = 5;
  if (active_) sendEmergencyStop();
}

void RealtimeClient::sendHeartbeat(qint64 nowMsValue) {
  lastHeartbeatMs_ = nowMsValue;
  QJsonObject heartbeat;
  heartbeat.insert(QStringLiteral("clientTimeMs"),
                   QString::number(QDateTime::currentMSecsSinceEpoch()));
  sendPacket(RealtimePacketType::Heartbeat, 0, 0, 0,
             jsonBytes(heartbeat));
}

void RealtimeClient::sendMotion(qint64) {
  sendPacket(RealtimePacketType::MotionSetpoint, linearMilliMps_,
             angularMilliRadps_, motionLeaseMs_);
}

void RealtimeClient::sendEmergencyStop() {
  QJsonObject payload;
  payload.insert(QStringLiteral("latched"), true);
  sendPacket(RealtimePacketType::EmergencyStop, 0, 0, 0,
             jsonBytes(payload));
}

void RealtimeClient::sendPacket(RealtimePacketType type,
                                qint32 linearMilliMps,
                                qint32 angularMilliRadps, quint16 ttlMs,
                                const QByteArray& payload) {
  if (!active_) return;
  RealtimePacket packet;
  packet.type = type;
  packet.sessionId = sessionId_;
  packet.sequence = sequence_++;
  packet.timestampUs = static_cast<quint64>(nowUs());
  packet.ttlMs = ttlMs;
  packet.linearMilliMps = linearMilliMps;
  packet.angularMilliRadps = angularMilliRadps;
  packet.payload = payload;
  DecodeError error;
  const QByteArray bytes = RealtimeCodec::encode(packet, sessionKey_, &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail);
    return;
  }
  const qint64 sent = socket_->writeDatagram(bytes, peerAddress_, peerPort_);
  if (sent != bytes.size()) {
    emit errorOccurred(QStringLiteral("实时通道发送失败：%1")
                           .arg(socket_->errorString()));
  }
}

void RealtimeClient::processDatagrams() {
  while (socket_->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = socket_->receiveDatagram();
    if (datagram.senderAddress() != peerAddress_ ||
        datagram.senderPort() != peerPort_) {
      continue;
    }
    RealtimePacket packet;
    DecodeError error;
    const DecodeResult result =
        RealtimeCodec::decode(datagram.data(), sessionKey_, &packet, &error);
    if (result != DecodeResult::Ok) {
      emit errorOccurred(error.detail);
      continue;
    }
    if (packet.sessionId != sessionId_) continue;
    if (packet.sequence == 0 || packet.sequence <= lastInboundSequence_) continue;
    lastInboundSequence_ = packet.sequence;
    if (packet.type == RealtimePacketType::Telemetry) {
      bool ok = false;
      const QJsonObject telemetry = parseJsonObject(packet.payload, &ok);
      if (ok) emit telemetryReceived(telemetry);
    } else if (packet.type == RealtimePacketType::Ack) {
      bool ok = false;
      const QJsonObject ack = parseJsonObject(packet.payload, &ok);
      if (ok) emit acknowledgeReceived(ack);
    }
  }
}

RealtimeServer::RealtimeServer(QObject* parent)
    : QObject(parent),
      socket_(new QUdpSocket(this)),
      watchdogTimer_(new QTimer(this)) {
  connect(socket_, &QUdpSocket::readyRead, this,
          &RealtimeServer::processDatagrams);
  watchdogTimer_->setTimerType(Qt::PreciseTimer);
  watchdogTimer_->setInterval(25);
  connect(watchdogTimer_, &QTimer::timeout, this,
          &RealtimeServer::checkSessionTimeouts);
}

RealtimeServer::~RealtimeServer() { stop(); }

bool RealtimeServer::start(const QHostAddress& address, quint16 port,
                           QString* error) {
  if (error) error->clear();
  if (socket_->state() != QAbstractSocket::UnconnectedState) return true;
  if (!socket_->bind(address, port)) {
    if (error) *error = socket_->errorString();
    return false;
  }
  watchdogTimer_->start();
  return true;
}

void RealtimeServer::stop() {
  watchdogTimer_->stop();
  socket_->close();
  const auto ids = sessions_.keys();
  for (quint64 id : ids) {
    Session& session = sessions_[id];
    if (session.motionLeaseActive) {
      session.motionLeaseActive = false;
      emit motionLeaseExpired(id);
    }
  }
  sessions_.clear();
}

bool RealtimeServer::isListening() const {
  return socket_->state() == QAbstractSocket::BoundState;
}

quint16 RealtimeServer::serverPort() const {
  return isListening() ? socket_->localPort() : 0;
}

void RealtimeServer::registerSession(quint64 sessionId,
                                     const QByteArray& sessionKey) {
  if (sessionId == 0 || sessionKey.size() != 32) return;
  Session session;
  session.key = sessionKey;
  session.lastHeartbeatMs = nowMs();
  sessions_.insert(sessionId, session);
}

void RealtimeServer::removeSession(quint64 sessionId) {
  if (!sessions_.contains(sessionId)) return;
  Session& session = sessions_[sessionId];
  if (session.motionLeaseActive) {
    session.motionLeaseActive = false;
    emit motionLeaseExpired(sessionId);
  }
  sessions_.remove(sessionId);
}

void RealtimeServer::sendTelemetry(quint64 sessionId,
                                   const QJsonObject& telemetry) {
  Session* session = sessions_.contains(sessionId) ? &sessions_[sessionId] : nullptr;
  if (!session) return;
  writePacket(sessionId, session, RealtimePacketType::Telemetry, telemetry);
}

void RealtimeServer::sendAcknowledge(quint64 sessionId,
                                     const QJsonObject& acknowledge) {
  Session* session = sessions_.contains(sessionId) ? &sessions_[sessionId] : nullptr;
  if (!session) return;
  writePacket(sessionId, session, RealtimePacketType::Ack, acknowledge);
}

void RealtimeServer::processDatagrams() {
  while (socket_->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = socket_->receiveDatagram();
    const QByteArray bytes = datagram.data();
    if (bytes.size() < kRealtimeHeaderBytes ||
        bytes.size() > kMaxRealtimeDatagramBytes) {
      emit packetRejected(0, QStringLiteral("实时数据报长度无效"));
      continue;
    }

    // Read the session id without trusting any other field; the key is only
    // used after the id maps to an authenticated control session.
    quint64 sessionId = 0;
    DecodeError probeError;
    const DecodeResult probeResult =
        RealtimeCodec::peekSessionId(bytes, &sessionId, &probeError);
    if (probeResult != DecodeResult::Ok) {
      emit packetRejected(0, probeError.detail);
      continue;
    }
    if (sessionId == 0 || !sessions_.contains(sessionId)) {
      emit packetRejected(sessionId, QStringLiteral("未注册的实时会话"));
      continue;
    }

    Session& session = sessions_[sessionId];
    RealtimePacket packet;
    DecodeError error;
    const DecodeResult result =
        RealtimeCodec::decode(bytes, session.key, &packet, &error);
    if (result != DecodeResult::Ok) {
      emit packetRejected(sessionId, error.detail);
      continue;
    }
    if (packet.sessionId != sessionId) {
      emit packetRejected(sessionId, QStringLiteral("实时会话号不匹配"));
      continue;
    }
    if (datagram.senderAddress().isNull() || datagram.senderPort() <= 0) {
      emit packetRejected(sessionId, QStringLiteral("实时数据报缺少发送方地址"));
      continue;
    }
    const quint16 senderPort = static_cast<quint16>(datagram.senderPort());
    if (!session.peer.valid) {
      session.peer.address = datagram.senderAddress();
      session.peer.port = senderPort;
      session.peer.valid = true;
    } else if (session.peer.address != datagram.senderAddress() ||
               session.peer.port != senderPort) {
      emit packetRejected(sessionId,
                          QStringLiteral("实时会话发送方地址发生变化"));
      continue;
    }

    if (packet.sequence <= session.lastInboundSequence) {
      emit packetRejected(sessionId, QStringLiteral("实时包序号回退"));
      continue;
    }
    session.lastInboundSequence = packet.sequence;

    if (packet.type == RealtimePacketType::Heartbeat) {
      session.lastHeartbeatMs = nowMs();
      if (session.timedOut) {
        session.timedOut = false;
        emit sessionRecovered(sessionId);
      }
      emit heartbeatReceived(sessionId);
      continue;
    }
    if (packet.type == RealtimePacketType::MotionSetpoint) {
      if (packet.ttlMs < 50 || packet.ttlMs > kSessionTimeoutMs) {
        emit packetRejected(sessionId, QStringLiteral("运动命令租约时间无效"));
        continue;
      }
      const qint64 now = nowMs();
      session.lastHeartbeatMs = now;
      session.motionExpiresAtMs = now + packet.ttlMs;
      session.motionLeaseActive = true;
      if (session.timedOut) {
        session.timedOut = false;
        emit sessionRecovered(sessionId);
      }
      emit motionSetpointReceived(sessionId, packet.linearMilliMps,
                                  packet.angularMilliRadps, packet.ttlMs,
                                  packet.sequence);
      continue;
    }
    if (packet.type == RealtimePacketType::EmergencyStop) {
      session.lastHeartbeatMs = nowMs();
      session.motionLeaseActive = false;
      session.motionExpiresAtMs = 0;
      if (session.timedOut) {
        session.timedOut = false;
        emit sessionRecovered(sessionId);
      }
      emit emergencyStopReceived(sessionId);
      continue;
    }
  }
}

void RealtimeServer::checkSessionTimeouts() {
  const qint64 now = nowMs();
  for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
    const quint64 sessionId = it.key();
    Session& session = it.value();
    if (session.motionLeaseActive && now >= session.motionExpiresAtMs) {
      session.motionLeaseActive = false;
      session.motionExpiresAtMs = 0;
      emit motionLeaseExpired(sessionId);
    }
    if (!session.timedOut &&
        session.lastHeartbeatMs > 0 &&
        now - session.lastHeartbeatMs > kSessionTimeoutMs) {
      session.timedOut = true;
      if (session.motionLeaseActive) {
        session.motionLeaseActive = false;
        session.motionExpiresAtMs = 0;
        emit motionLeaseExpired(sessionId);
      }
      emit sessionTimedOut(sessionId);
    } else if (session.timedOut && session.lastHeartbeatMs > 0 &&
               now - session.lastHeartbeatMs <= kHeartbeatIntervalMs * 2) {
      session.timedOut = false;
      emit sessionRecovered(sessionId);
    }
  }
}

void RealtimeServer::writePacket(quint64 sessionId, Session* session,
                                 RealtimePacketType type,
                                 const QJsonObject& payload) {
  if (!session || !session->peer.valid) return;
  RealtimePacket packet;
  packet.type = type;
  packet.sessionId = sessionId;
  packet.sequence = ++session->outboundSequence;
  packet.timestampUs = static_cast<quint64>(nowUs());
  packet.payload = fittingRealtimePayload(payload);
  if (packet.payload.isEmpty()) {
    emit packetRejected(sessionId,
                        QStringLiteral("实时载荷超过单数据报上限，已丢弃"));
    return;
  }
  DecodeError error;
  const QByteArray bytes = RealtimeCodec::encode(packet, session->key, &error);
  if (!error.ok()) {
    emit packetRejected(packet.sessionId, error.detail);
    return;
  }
  const qint64 written = socket_->writeDatagram(bytes, session->peer.address,
                                                session->peer.port);
  if (written != bytes.size()) {
    emit packetRejected(packet.sessionId,
                        QStringLiteral("实时遥测发送失败：%1")
                            .arg(socket_->errorString()));
  }
}
}  // namespace pa1664::remote
