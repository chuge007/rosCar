#pragma once

#include "protocol/media_codec.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>

class QUdpSocket;
class QTimer;

namespace pa1664::remote {

// Operator-side latest-value media receiver.  Slow or missing fragments are
// discarded as soon as a newer message for the same stream arrives.
class MediaClient final : public QObject {
  Q_OBJECT
 public:
  explicit MediaClient(QObject* parent = nullptr);
  ~MediaClient() override;

  bool isActive() const { return active_; }

 public slots:
  void start(const QString& host, quint16 port, quint64 sessionId,
             const QByteArray& sessionKey);
  void stop();

 signals:
  void messageReceived(quint16 streamId, pa1664::remote::MediaStreamKind kind,
                       quint64 messageId, quint64 timestampUs,
                       const QByteArray& payload);
  void errorOccurred(const QString& detail);

 private:
  struct Assembly {
    MediaStreamKind kind = MediaStreamKind::Heartbeat;
    quint64 messageId = 0;
    quint64 timestampUs = 0;
    quint32 totalBytes = 0;
    quint32 fragmentCount = 0;
    quint32 receivedFragments = 0;
    qint64 lastFragmentMs = 0;
    QHash<quint32, QByteArray> fragments;
  };
  void processDatagrams();
  void sendHeartbeat();
  void dropExpiredAssemblies(qint64 nowMs);

  QUdpSocket* socket_ = nullptr;
  QTimer* heartbeatTimer_ = nullptr;
  QHostAddress peerAddress_;
  quint16 peerPort_ = kDefaultMediaPort;
  quint64 sessionId_ = 0;
  QByteArray sessionKey_;
  quint64 nextMessageId_ = 1;
  QHash<quint16, Assembly> assemblies_;
  bool active_ = false;
};

class MediaServer final : public QObject {
  Q_OBJECT
 public:
  explicit MediaServer(QObject* parent = nullptr);
  ~MediaServer() override;

  bool start(const QHostAddress& address = QHostAddress::AnyIPv4,
             quint16 port = kDefaultMediaPort, QString* error = nullptr);
  void stop();
  bool isListening() const;
  quint16 serverPort() const;

  void registerSession(quint64 sessionId, const QByteArray& sessionKey);
  void removeSession(quint64 sessionId);
  bool hasPeer(quint64 sessionId) const;
  void sendMessage(quint64 sessionId, quint16 streamId, MediaStreamKind kind,
                   const QByteArray& payload);

 signals:
  void packetRejected(quint64 sessionId, const QString& detail);
  void peerDiscovered(quint64 sessionId);

 private:
  struct Peer {
    QHostAddress address;
    quint16 port = 0;
    bool valid = false;
  };
  struct Session {
    QByteArray key;
    Peer peer;
    quint64 nextMessageId = 1;
    quint64 nextFragmentSequence = 1;
    qint64 lastInboundMs = 0;
  };
  void processDatagrams();

  QUdpSocket* socket_ = nullptr;
  QHash<quint64, Session> sessions_;
};

}  // namespace pa1664::remote
