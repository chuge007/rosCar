#pragma once

#include "protocol/protocol_types.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace pa1664::remote {

class ControlClient;
class RealtimeClient;
class MediaClient;
class BulkClient;
class QThread;

enum class WorkbenchConnectionState {
  Offline,
  Connecting,
  Online,
  Degraded,
};

QString workbenchConnectionStateText(WorkbenchConnectionState state);

// Owns the control, realtime, media and bulk operator-side transports.
// UI/service adapters use
// this class instead of touching sockets directly.
class RemoteWorkbenchSession final : public QObject {
  Q_OBJECT
 public:
  explicit RemoteWorkbenchSession(QObject* parent = nullptr);
  ~RemoteWorkbenchSession() override;

  WorkbenchConnectionState state() const { return state_; }
  bool isReady() const { return state_ == WorkbenchConnectionState::Online; }
  QString host() const { return host_; }
  quint64 sessionId() const;
  QString statusText() const { return statusText_; }
  QStringList capabilities() const;
  quint16 mediaPort() const { return mediaPort_; }
  quint16 bulkPort() const { return bulkPort_; }
  bool mediaReady() const;
  bool bulkReady() const;
  bool hasCapability(const QString& capability) const;

 public slots:
  void start(const QString& host, quint16 controlPort = kDefaultControlPort);
  void stop();

  quint64 request(const QString& operation, const QJsonObject& data = {});
  void setMotion(double linearMps, double angularRadps,
                 quint16 leaseMs = kMotionLeaseMs);
  void clearMotion();
  void emergencyStop();
  quint64 sendBulk(pa1664::remote::BulkStreamKind kind,
                  const QByteArray& payload);

 signals:
  void stateChanged(pa1664::remote::WorkbenchConnectionState state,
                    const QString& detail);
  void online();
  void offline(const QString& reason);
  void responseReceived(const QString& operation, quint64 requestId, bool ok,
                        const QJsonObject& data, const QString& errorCode,
                        const QString& errorText);
  void eventReceived(const QString& eventName, const QJsonObject& data);
  void telemetryReceived(const QJsonObject& telemetry);
  void mediaMessageReceived(quint16 streamId,
                            pa1664::remote::MediaStreamKind kind,
                            quint64 messageId, quint64 timestampUs,
                            const QByteArray& payload);
  void bulkMessageReceived(pa1664::remote::BulkStreamKind kind,
                           quint64 sequence, const QByteArray& payload);
  void mediaReadyChanged(bool ready);
  void bulkReadyChanged(bool ready);
  void protocolError(const QString& detail);

 private:
  void onControlStateChanged(ConnectionState state, const QString& detail);
  void onSessionReady(quint64 sessionId, const QByteArray& sessionKey,
                      quint16 realtimePort, quint16 mediaPort, quint16 bulkPort, int heartbeatIntervalMs);
  void onControlResponse(quint64 requestId, const QJsonObject& response);
  void onRequestTimedOut(quint64 requestId, const QString& operation);
  void setState(WorkbenchConnectionState state, const QString& detail);
  void stopRealtimeAsync();

  ControlClient* control_ = nullptr;
  RealtimeClient* realtime_ = nullptr;
  QPointer<QObject> realtimeGuard_;
  QThread* realtimeThread_ = nullptr;
  MediaClient* media_ = nullptr;
  BulkClient* bulk_ = nullptr;
  QHash<quint64, QString> pendingOperations_;
  WorkbenchConnectionState state_ = WorkbenchConnectionState::Offline;
  QString host_;
  QString statusText_;
  QStringList capabilities_;
  quint16 realtimePort_ = kDefaultRealtimePort;
  quint16 mediaPort_ = kDefaultMediaPort;
  quint16 bulkPort_ = kDefaultBulkPort;
  bool started_ = false;
};

}  // namespace pa1664::remote

Q_DECLARE_METATYPE(pa1664::remote::WorkbenchConnectionState)



