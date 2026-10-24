#pragma once

#include "protocol/realtime_codec.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>

class QUdpSocket;
class QTimer;

namespace pa1664::remote {

class RealtimeClient final : public QObject {
  Q_OBJECT
 public:
  explicit RealtimeClient(QObject* parent = nullptr);
  ~RealtimeClient() override;

  bool isActive() const { return active_; }

 public slots:
  void start(const QString& host, quint16 port, quint64 sessionId,
             const QByteArray& sessionKey);
  void stop();
  void setMotionSetpoint(double linearMps, double angularRadps,
                         quint16 leaseMs = kMotionLeaseMs);
  void clearMotionSetpoint();
  void emergencyStop();

 signals:
  void telemetryReceived(const QJsonObject& telemetry);
  void acknowledgeReceived(const QJsonObject& acknowledge);
  void errorOccurred(const QString& detail);

 private:
  void sendHeartbeat(qint64 nowMs);
  void sendMotion(qint64 nowMs);
  void sendEmergencyStop();
  void sendPacket(RealtimePacketType type, qint32 linearMilliMps = 0,
                  qint32 angularMilliRadps = 0, quint16 ttlMs = 0,
                  const QByteArray& payload = {});
  void processDatagrams();

  QUdpSocket* socket_ = nullptr;
  QTimer* sendTimer_ = nullptr;
  QElapsedTimer clock_;
  qint64 lastHeartbeatMs_ = -1;
  QHostAddress peerAddress_;
  quint16 peerPort_ = kDefaultRealtimePort;
  quint64 sessionId_ = 0;
  QByteArray sessionKey_;
  quint64 sequence_ = 1;
  quint64 lastInboundSequence_ = 0;
  qint32 linearMilliMps_ = 0;
  qint32 angularMilliRadps_ = 0;
  quint16 motionLeaseMs_ = kMotionLeaseMs;
  bool motionActive_ = false;
  int emergencyRepeats_ = 0;
  bool active_ = false;
};

class RealtimeServer final : public QObject {
  Q_OBJECT
 public:
  explicit RealtimeServer(QObject* parent = nullptr);
  ~RealtimeServer() override;

  bool start(const QHostAddress& address = QHostAddress::AnyIPv4,
             quint16 port = kDefaultRealtimePort, QString* error = nullptr);
  void stop();
  bool isListening() const;
  quint16 serverPort() const;

  void registerSession(quint64 sessionId, const QByteArray& sessionKey);
  void removeSession(quint64 sessionId);
  void sendTelemetry(quint64 sessionId, const QJsonObject& telemetry);
  void sendAcknowledge(quint64 sessionId, const QJsonObject& acknowledge);

 signals:
  void motionSetpointReceived(quint64 sessionId, qint32 linearMilliMps,
                              qint32 angularMilliRadps, quint16 ttlMs,
                              quint64 sequence);
  void heartbeatReceived(quint64 sessionId);
  void emergencyStopReceived(quint64 sessionId);
  void motionLeaseExpired(quint64 sessionId);
  void sessionTimedOut(quint64 sessionId);
  void sessionRecovered(quint64 sessionId);
  void packetRejected(quint64 sessionId, const QString& detail);

 private:
  struct Peer {
    QHostAddress address;
    quint16 port = 0;
    bool valid = false;
  };
  struct Session {
    QByteArray key;
    Peer peer;
    quint64 lastInboundSequence = 0;
    quint64 outboundSequence = 0;
    qint64 lastHeartbeatMs = 0;
    qint64 motionExpiresAtMs = 0;
    bool motionLeaseActive = false;
    bool timedOut = false;
  };

  void processDatagrams();
  void checkSessionTimeouts();
  void writePacket(quint64 sessionId, Session* session, RealtimePacketType type,
                   const QJsonObject& payload);

  QUdpSocket* socket_ = nullptr;
  QTimer* watchdogTimer_ = nullptr;
  QHash<quint64, Session> sessions_;
};

}  // namespace pa1664::remote
