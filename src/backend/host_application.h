#pragma once

#include "protocol/protocol_types.h"

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
};

}  // namespace pa1664::remote
