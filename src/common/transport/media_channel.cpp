#include "media_channel.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace pa1664::remote {
namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
qint64 nowUs() { return nowMs() * 1000; }

constexpr int kDatagramPayloadBytes =
    kMaxRealtimeDatagramBytes - kMediaHeaderBytes - kMediaAuthTagBytes;

}  // namespace

MediaClient::MediaClient(QObject* parent)
    : QObject(parent), socket_(new QUdpSocket(this)),
      heartbeatTimer_(new QTimer(this)) {
  heartbeatTimer_->setInterval(kMediaHeartbeatIntervalMs);
  connect(heartbeatTimer_, &QTimer::timeout, this, &MediaClient::sendHeartbeat);
  connect(socket_, &QUdpSocket::readyRead, this, &MediaClient::processDatagrams);
}

MediaClient::~MediaClient() { stop(); }

void MediaClient::start(const QString& host, quint16 port, quint64 sessionId,
                        const QByteArray& sessionKey) {
  stop();
  QHostAddress address;
  if (!address.setAddress(host.trimmed()) || sessionId == 0 ||
      sessionKey.size() != 32) {
    emit errorOccurred(QStringLiteral("媒体通道会话参数无效"));
    return;
  }
  if (!socket_->bind(QHostAddress::AnyIPv4, 0)) {
    emit errorOccurred(QStringLiteral("媒体 UDP 绑定失败：%1")
                           .arg(socket_->errorString()));
    return;
  }
  peerAddress_ = address;
  peerPort_ = port;
  sessionId_ = sessionId;
  sessionKey_ = sessionKey;
  nextMessageId_ = 1;
  assemblies_.clear();
  active_ = true;
  heartbeatTimer_->start();
  sendHeartbeat();
}

void MediaClient::stop() {
  active_ = false;
  heartbeatTimer_->stop();
  socket_->close();
  sessionId_ = 0;
  sessionKey_.clear();
  assemblies_.clear();
}

void MediaClient::sendHeartbeat() {
  if (!active_) return;
  MediaFragmentPacket packet;
  packet.kind = MediaStreamKind::Heartbeat;
  packet.sessionId = sessionId_;
  packet.streamId = 0;
  packet.messageId = nextMessageId_++;
  packet.sequence = packet.messageId;
  packet.timestampUs = static_cast<quint64>(nowUs());
  packet.totalBytes = 0;
  packet.fragmentIndex = 0;
  packet.fragmentCount = 1;
  DecodeError error;
  const QByteArray bytes = MediaCodec::encode(packet, sessionKey_, &error);
  if (!error.ok()) {
    emit errorOccurred(error.detail);
    return;
  }
  socket_->writeDatagram(bytes, peerAddress_, peerPort_);
}

void MediaClient::processDatagrams() {
  const qint64 now = nowMs();
  dropExpiredAssemblies(now);
  while (socket_->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = socket_->receiveDatagram();
    qint64 senderPort = datagram.senderPort();
    if (senderPort != peerPort_) continue;
    quint64 packetSession = 0;
    DecodeError error;
    if (MediaCodec::peekSessionId(datagram.data(), &packetSession, &error) !=
            DecodeResult::Ok ||
        packetSession != sessionId_) {
      continue;
    }
    MediaFragmentPacket packet;
    if (MediaCodec::decode(datagram.data(), sessionKey_, &packet, &error) !=
        DecodeResult::Ok) {
      emit errorOccurred(error.detail);
      continue;
    }
    if (packet.kind == MediaStreamKind::Heartbeat ||
        packet.streamId == 0) {
      continue;
    }
    Assembly& assembly = assemblies_[packet.streamId];
    if (assembly.messageId != packet.messageId) {
      assembly = Assembly{};
      assembly.kind = packet.kind;
      assembly.messageId = packet.messageId;
      assembly.timestampUs = packet.timestampUs;
      assembly.totalBytes = packet.totalBytes;
      assembly.fragmentCount = packet.fragmentCount;
    }
    if (assembly.kind != packet.kind ||
        assembly.totalBytes != packet.totalBytes ||
        assembly.fragmentCount != packet.fragmentCount) {
      assemblies_.remove(packet.streamId);
      continue;
    }
    if (!assembly.fragments.contains(packet.fragmentIndex)) {
      assembly.fragments.insert(packet.fragmentIndex, packet.payload);
      ++assembly.receivedFragments;
    }
    assembly.lastFragmentMs = now;
    if (assembly.receivedFragments != assembly.fragmentCount) continue;

    QByteArray message;
    message.reserve(static_cast<int>(assembly.totalBytes));
    for (quint32 index = 0; index < assembly.fragmentCount; ++index) {
      const auto it = assembly.fragments.constFind(index);
      if (it == assembly.fragments.constEnd()) {
        message.clear();
        break;
      }
      message.append(it.value());
    }
    const MediaStreamKind kind = assembly.kind;
    const quint64 messageId = assembly.messageId;
    const quint64 timestampUs = assembly.timestampUs;
    assemblies_.remove(packet.streamId);
    if (message.size() == static_cast<int>(packet.totalBytes)) {
      emit messageReceived(packet.streamId, kind, messageId, timestampUs,
                           message);
    }
  }
}

void MediaClient::dropExpiredAssemblies(qint64 now) {
  for (auto it = assemblies_.begin(); it != assemblies_.end();) {
    if (it.value().lastFragmentMs > 0 &&
        now - it.value().lastFragmentMs > kMediaAssemblyTimeoutMs) {
      it = assemblies_.erase(it);
    } else {
      ++it;
    }
  }
}

MediaServer::MediaServer(QObject* parent)
    : QObject(parent), socket_(new QUdpSocket(this)) {
  connect(socket_, &QUdpSocket::readyRead, this, &MediaServer::processDatagrams);
}

MediaServer::~MediaServer() { stop(); }

bool MediaServer::start(const QHostAddress& address, quint16 port,
                        QString* error) {
  stop();
  if (!socket_->bind(address, port)) {
    if (error) *error = socket_->errorString();
    return false;
  }
  return true;
}

void MediaServer::stop() {
  socket_->close();
  sessions_.clear();
}

bool MediaServer::isListening() const {
  return socket_->state() == QAbstractSocket::BoundState;
}

quint16 MediaServer::serverPort() const { return socket_->localPort(); }

void MediaServer::registerSession(quint64 sessionId,
                                  const QByteArray& sessionKey) {
  if (sessionId == 0 || sessionKey.size() != 32) return;
  Session session;
  session.key = sessionKey;
  sessions_.insert(sessionId, session);
}

void MediaServer::removeSession(quint64 sessionId) {
  sessions_.remove(sessionId);
}

bool MediaServer::hasPeer(quint64 sessionId) const {
  const auto it = sessions_.constFind(sessionId);
  return it != sessions_.constEnd() && it.value().peer.valid;
}

void MediaServer::sendMessage(quint64 sessionId, quint16 streamId,
                              MediaStreamKind kind,
                              const QByteArray& payload) {
  auto it = sessions_.find(sessionId);
  if (it == sessions_.end() || !it.value().peer.valid) return;
  if (payload.size() > kMaxMediaMessageBytes) {
    emit packetRejected(sessionId, QStringLiteral("媒体消息超过上限，已丢弃"));
    return;
  }
  Session& session = it.value();
  const quint32 totalBytes = static_cast<quint32>(payload.size());
  const quint32 fragmentCount = static_cast<quint32>(
      std::max<qint64>(1, (payload.size() + kDatagramPayloadBytes - 1) /
                             kDatagramPayloadBytes));
  if (fragmentCount > static_cast<quint32>(kMaxMediaFragments)) {
    emit packetRejected(sessionId, QStringLiteral("媒体消息分片数量超限"));
    return;
  }
  const quint64 messageId = session.nextMessageId++;
  const quint64 timestampUs = static_cast<quint64>(nowUs());
  for (quint32 index = 0; index < fragmentCount; ++index) {
    MediaFragmentPacket packet;
    packet.kind = kind;
    packet.sessionId = sessionId;
    packet.streamId = streamId;
    packet.messageId = messageId;
    packet.sequence = session.nextFragmentSequence++;
    packet.timestampUs = timestampUs;
    packet.totalBytes = totalBytes;
    packet.fragmentIndex = index;
    packet.fragmentCount = fragmentCount;
    const int offset = static_cast<int>(index) * kDatagramPayloadBytes;
    packet.payload = payload.mid(offset, kDatagramPayloadBytes);
    DecodeError error;
    const QByteArray bytes = MediaCodec::encode(packet, session.key, &error);
    if (!error.ok()) {
      emit packetRejected(sessionId, error.detail);
      return;
    }
    const qint64 written = socket_->writeDatagram(
        bytes, session.peer.address, session.peer.port);
    if (written != bytes.size()) {
      emit packetRejected(sessionId,
                          QStringLiteral("媒体数据报发送失败：%1")
                              .arg(socket_->errorString()));
      return;
    }
  }
}

void MediaServer::processDatagrams() {
  while (socket_->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = socket_->receiveDatagram();
    quint64 sessionId = 0;
    DecodeError error;
    if (MediaCodec::peekSessionId(datagram.data(), &sessionId, &error) !=
        DecodeResult::Ok) {
      continue;
    }
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
      emit packetRejected(sessionId, QStringLiteral("媒体会话未注册"));
      continue;
    }
    Session& session = it.value();
    const qint64 senderPort = datagram.senderPort();

    // Authenticate before learning or changing the return address.  Otherwise
    // an unauthenticated datagram could claim the media peer and redirect
    // subsequent robot media away from the real operator.
    MediaFragmentPacket packet;
    if (MediaCodec::decode(datagram.data(), session.key, &packet, &error) !=
        DecodeResult::Ok) {
      emit packetRejected(sessionId, error.detail);
      continue;
    }
    if (!session.peer.valid) {
      session.peer.address = datagram.senderAddress();
      session.peer.port = static_cast<quint16>(senderPort);
      session.peer.valid = true;
      emit peerDiscovered(sessionId);
    } else if (session.peer.address != datagram.senderAddress() ||
               session.peer.port != senderPort) {
      emit packetRejected(sessionId, QStringLiteral("媒体会话发送方地址变化"));
      continue;
    }
    session.lastInboundMs = nowMs();
  }
}

}  // namespace pa1664::remote
