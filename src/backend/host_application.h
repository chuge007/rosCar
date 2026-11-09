#pragma once

#include "protocol/protocol_types.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>

class QThread;

namespace pa1664::remote {

class BulkServer;
class ControlServer;
class UltrasoundHostService;
class MediaServer;
class RealtimeServer;
class RobotHostService;
class RobotPeripheralHostService;

class HostApplication final : public QObject {
  Q_OBJECT
 public:
  explicit HostApplication(QObject* parent = nullptr);
  ~HostApplication() override;

  bool start(const QHostAddress& address, quint16 controlPort,
             quint16 realtimePort, quint16 mediaPort,
             quint16 bulkPort, QString* error = nullptr);
  void stop();
  bool isRunning() const;
  QJsonObject runtimeStatus() const;

 signals:
  void fatalError(const QString& detail);

 private:
  void wireServices();
  void startUltrasoundService();
  void stopUltrasoundService();
  void onSessionOpened(quint64 sessionId, const QString& clientId,
                       const QString& peerAddress);
  void onSessionClosed(quint64 sessionId, const QString& reason);
  void onRequest(quint64 sessionId, quint64 requestId,
                 const QJsonObject& request);
  void onRobotTelemetry(const QJsonObject& telemetry);
  void onMotionSetpoint(quint64 sessionId, qint32 linearMilliMps,
                        qint32 angularMilliRadps, quint16 ttlMs,
                        quint64 sequence);
  void onUltrasoundFrame(const QByteArray& payload, int groupId,
                         int deviceId);
  void onUltrasoundScanBatch(const QByteArray& payload);
  void drainBulkQueue();
  void enqueueBulk(BulkStreamKind kind, const QByteArray& payload);
  // Transmission accounting. Every media payload that actually leaves the host
  // is attributed to its stream id so the link budget can be measured per
  // stream instead of only as one aggregate byte count.
  void noteMediaTx(quint16 streamId, qint64 bytes);
  void maybeLogMediaTx();
  void maybeLogBulkTx();
  void sendSessionEvent(quint64 sessionId, const QString& name,
                        const QJsonObject& data = {});

  ControlServer* control_ = nullptr;
  RealtimeServer* realtime_ = nullptr;
  MediaServer* media_ = nullptr;
  BulkServer* bulk_ = nullptr;
  RobotHostService* robot_ = nullptr;
  RobotPeripheralHostService* peripheral_ = nullptr;
  UltrasoundHostService* ultrasound_ = nullptr;
  QThread* ultrasoundThread_ = nullptr;
  QTimer* bulkDrainTimer_ = nullptr;
  QHostAddress listenAddress_;
  quint16 controlPort_ = kDefaultControlPort;
  quint16 realtimePort_ = kDefaultRealtimePort;
  quint16 mediaPort_ = kDefaultMediaPort;
  quint16 bulkPort_ = kDefaultBulkPort;
  bool started_ = false;
  struct PendingBulk {
    BulkStreamKind kind = BulkStreamKind::UltrasoundScanBatch;
    QByteArray payload;
  };
  QQueue<PendingBulk> pendingBulk_;
  qint64 pendingBulkBytes_ = 0;
  // Media transmission accounting. The mediaReady handler used to return
  // silently when the session had no validated peer, which made "no picture on
  // the console" indistinguishable from "no frames from the camera".
  quint64 mediaTxMessages_ = 0;
  quint64 mediaTxBytes_ = 0;
  quint64 mediaTxDropped_ = 0;
  quint64 mediaTxLaserImage_ = 0;
  quint64 mediaTxLaserPoints_ = 0;
  quint64 mediaTxUsb_[2] = {0, 0};
  qint64 mediaTxLastLogMs_ = 0;
  QHash<quint16, quint64> mediaTxStreamMessages_;
  QHash<quint16, quint64> mediaTxStreamBytes_;
  // Bulk channel accounting (ultrasound scan batches). Separate window because
  // batches can be megabytes each and must be visible in the link budget.
  quint64 bulkTxMessages_ = 0;
  quint64 bulkTxBytes_ = 0;
  qint64 bulkTxLastLogMs_ = 0;
};

}  // namespace pa1664::remote
