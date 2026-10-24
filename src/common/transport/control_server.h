#pragma once

#include "protocol/control_codec.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace pa1664::remote {

class ControlServer final : public QObject {
  Q_OBJECT
 public:
  explicit ControlServer(QObject* parent = nullptr);
  ~ControlServer() override;

  bool start(const QHostAddress& address = QHostAddress::AnyIPv4,
             quint16 port = kDefaultControlPort, QString* error = nullptr);
  void stop();
  bool isListening() const;
  quint16 serverPort() const;
  int sessionCount() const;

  void setAdvertisedRealtimePort(quint16 port) { advertisedRealtimePort_ = port; }
  void setAdvertisedMediaPort(quint16 port) { advertisedMediaPort_ = port; }
  void setAdvertisedBulkPort(quint16 port) { advertisedBulkPort_ = port; }
  void setCapabilities(const QStringList& capabilities) {
    capabilities_ = capabilities;
    capabilities_.removeDuplicates();
  }
  QByteArray sessionKey(quint64 sessionId) const;
  QString sessionPeer(quint64 sessionId) const;
  QString sessionPeerAddress(quint64 sessionId) const;

  // Closes one authenticated session without touching other clients.  Used
  // when the robot host rejects a second controller.
  void closeSession(quint64 sessionId, const QString& reason);

  void sendResponse(quint64 sessionId, quint64 requestId, bool ok,
                    const QJsonObject& data = {}, const QString& errorCode = {},
                    const QString& errorText = {});
  void sendEvent(quint64 sessionId, const QString& eventName,
                 const QJsonObject& data = {});
  void broadcastEvent(const QString& eventName, const QJsonObject& data = {});

 signals:
  void sessionOpened(quint64 sessionId, const QString& clientId,
                     const QString& peerAddress);
  void sessionClosed(quint64 sessionId, const QString& reason);
  void requestReceived(quint64 sessionId, quint64 requestId,
                       const QJsonObject& request);
  void protocolError(quint64 sessionId, const QString& detail);

 private:
  struct ClientState {
    QTcpSocket* socket = nullptr;
    ControlStreamDecoder decoder;
    quint64 sessionId = 0;
    quint64 nextOutboundSequence = 1;
    quint64 lastInboundSequence = 0;
    QByteArray sessionKey;
    QString clientId;
    QString peerAddress;
    qint64 handshakeDeadlineMs = 0;
    qint64 lastInboundMs = 0;
    bool handshakeComplete = false;
  };

  void acceptConnections();
  void readClient(ClientState* client);
  void maintainSessions();
  void removeClient(ClientState* client, const QString& reason);
  void handleFrame(ClientState* client, const ControlFrame& frame);
  void handleHello(ClientState* client, const ControlFrame& frame);
  void handleAuthenticatedFrame(ClientState* client, const ControlFrame& frame);
  void writeFrame(ClientState* client, ControlMessageType type,
                  const QJsonObject& payload, quint64 requestId = 0,
                  ControlFlags flags = ControlFlags::None);
  void writeError(ClientState* client, const QString& code, const QString& text,
                  quint64 requestId = 0);
  ClientState* findClient(quint64 sessionId) const;
  static QByteArray jsonBytes(const QJsonObject& object);

  QTcpServer* server_ = nullptr;
  QTimer* maintenanceTimer_ = nullptr;
  QHash<quint64, ClientState*> sessions_;
  QHash<QTcpSocket*, ClientState*> bySocket_;
  quint64 nextSessionId_ = 1;
  quint16 advertisedRealtimePort_ = kDefaultRealtimePort;
  quint16 advertisedMediaPort_ = kDefaultMediaPort;
  quint16 advertisedBulkPort_ = kDefaultBulkPort;
  QStringList capabilities_;
};

}  // namespace pa1664::remote

