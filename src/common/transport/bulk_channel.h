#pragma once

#include "protocol/bulk_codec.h"

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace pa1664::remote {

class BulkClient final : public QObject {
  Q_OBJECT
 public:
  explicit BulkClient(QObject* parent = nullptr);
  ~BulkClient() override;

  bool isConnected() const;

 public slots:
  void start(const QString& host, quint16 port, quint64 sessionId,
             const QByteArray& sessionKey);
  void stop();
  quint64 send(BulkStreamKind kind, const QByteArray& payload);

 signals:
  void connected();
  void disconnected(const QString& reason);
  void messageReceived(pa1664::remote::BulkStreamKind kind, quint64 sequence,
                       const QByteArray& payload);
  void errorOccurred(const QString& detail);

 private:
  void connectNow();
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onSocketError();
  void sendHeartbeat();

  QTcpSocket* socket_ = nullptr;
  QTimer* reconnectTimer_ = nullptr;
  QTimer* heartbeatTimer_ = nullptr;
  BulkStreamDecoder decoder_;
  QString host_;
  quint16 port_ = kDefaultBulkPort;
  quint64 sessionId_ = 0;
  QByteArray sessionKey_;
  quint64 nextSequence_ = 1;
  bool active_ = false;
};

class BulkServer final : public QObject {
  Q_OBJECT
 public:
  explicit BulkServer(QObject* parent = nullptr);
  ~BulkServer() override;

  bool start(const QHostAddress& address = QHostAddress::AnyIPv4,
             quint16 port = kDefaultBulkPort, QString* error = nullptr);
  void stop();
  bool isListening() const;
  quint16 serverPort() const;

  void registerSession(quint64 sessionId, const QByteArray& sessionKey);
  void removeSession(quint64 sessionId);
  bool sendMessage(quint64 sessionId, BulkStreamKind kind,
                   const QByteArray& payload);

 signals:
  void sessionAuthenticated(quint64 sessionId);
  void messageReceived(quint64 sessionId, pa1664::remote::BulkStreamKind kind,
                       quint64 sequence, const QByteArray& payload);
  void errorOccurred(quint64 sessionId, const QString& detail);

 private:
  struct Client {
    QTcpSocket* socket = nullptr;
    BulkStreamDecoder decoder;
    QByteArray authBuffer;
    quint64 sessionId = 0;
    quint64 lastInboundSequence = 0;
    quint64 nextOutboundSequence = 1;
    bool authenticated = false;
  };
  void acceptConnections();
  void readClient(Client* client);
  void closeClient(Client* client, const QString& reason);

  QTcpServer* server_ = nullptr;
  QHash<quint64, QByteArray> sessionKeys_;
  QHash<QTcpSocket*, Client*> clients_;
  QHash<quint64, Client*> bySession_;
};

}  // namespace pa1664::remote
