#pragma once

#include "protocol/control_codec.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QTcpSocket;
class QTimer;

namespace pa1664::remote {

enum class ConnectionState {
  Disconnected,
  Connecting,
  Handshaking,
  Ready,
  Reconnecting,
};

QString connectionStateText(ConnectionState state);

class ControlClient final : public QObject {
  Q_OBJECT
 public:
  explicit ControlClient(QObject* parent = nullptr);
  ~ControlClient() override;

  ConnectionState state() const { return state_; }
  bool isReady() const { return state_ == ConnectionState::Ready; }
  quint64 sessionId() const { return sessionId_; }
  QByteArray sessionKey() const { return sessionKey_; }
  quint16 realtimePort() const { return realtimePort_; }
  quint16 mediaPort() const { return mediaPort_; }
  quint16 bulkPort() const { return bulkPort_; }
  QStringList capabilities() const { return capabilities_; }

 public slots:
  void start(const QString& host, quint16 controlPort = kDefaultControlPort);
  void stop();
  quint64 sendRequest(const QString& operation, const QJsonObject& data = {});

 signals:
  void connectionStateChanged(pa1664::remote::ConnectionState state,
                              const QString& detail);
  void sessionReady(quint64 sessionId, const QByteArray& sessionKey,
                    quint16 realtimePort, quint16 mediaPort,
                    quint16 bulkPort, int heartbeatIntervalMs);
  void responseReceived(quint64 requestId, const QJsonObject& response);
  void requestTimedOut(quint64 requestId, const QString& operation);
  void eventReceived(const QString& eventName, const QJsonObject& data);
  void errorOccurred(const QString& detail, bool recoverable);

 private:
  void connectSocket();
  void scheduleReconnect(const QString& reason);
  void setState(ConnectionState state, const QString& detail);
  void sendHello();
  void sendPing();
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onSocketError();
  void handleFrame(const ControlFrame& frame);
  void handleHelloAck(const ControlFrame& frame);
  void failProtocol(const QString& detail);
  void expireRequests(qint64 nowMs);
  void clearPendingRequests(const QString& reason);
  int writeFrame(ControlMessageType type, const QJsonObject& payload,
                 quint64 requestId = 0);
  static QJsonObject objectFromPayload(const QByteArray& payload, bool* ok);

  QTcpSocket* socket_ = nullptr;
  QTimer* reconnectTimer_ = nullptr;
  QTimer* handshakeTimer_ = nullptr;
  QTimer* heartbeatTimer_ = nullptr;
  ControlStreamDecoder decoder_;
  ConnectionState state_ = ConnectionState::Disconnected;
  QString host_;
  quint16 controlPort_ = kDefaultControlPort;
  quint64 sessionId_ = 0;
  QByteArray sessionKey_;
  QStringList capabilities_;
  quint16 realtimePort_ = kDefaultRealtimePort;
  quint16 mediaPort_ = kDefaultMediaPort;
  quint16 bulkPort_ = kDefaultBulkPort;
  quint64 nextSequence_ = 1;
  quint64 nextRequestId_ = 1;
  quint64 helloRequestId_ = 0;
  quint64 lastInboundSequence_ = 0;
  qint64 lastInboundMs_ = 0;
  struct PendingRequest {
    QString operation;
    qint64 sentAtMs = 0;
  };
  QHash<quint64, PendingRequest> pendingRequests_;
  static constexpr int kRequestTimeoutMs = 5000;
  static constexpr int kMaximumPendingRequests = 2048;
  int reconnectDelayMs_ = 500;
  bool stopRequested_ = true;
  bool suppressReconnect_ = false;
};

}  // namespace pa1664::remote

Q_DECLARE_METATYPE(pa1664::remote::ConnectionState)

