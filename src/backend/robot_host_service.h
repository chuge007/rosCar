#pragma once

#include "drive_settings.h"
#include "drive_types.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

class QThread;

namespace crawling {
class SynchronizedDriveController;
}

namespace pa1664::remote {

class RobotHostService final : public QObject {
  Q_OBJECT
 public:
  explicit RobotHostService(QObject* parent = nullptr);
  ~RobotHostService() override;

  quint64 ownerSessionId() const { return ownerSessionId_; }
  bool isDriveConnected() const { return driveConnected_; }
  crawling::DriveState driveState() const { return lastTelemetry_.state; }
  QJsonObject snapshot() const;

 public slots:
  void initialize();
  void shutdown();
  bool claimSession(quint64 sessionId);
  void releaseSession(quint64 sessionId);
  void handleRequest(quint64 sessionId, quint64 requestId,
                     const QJsonObject& request);
  void setMotionFromOperator(quint64 sessionId, double linearMps,
                             double angularRadps);
  void stopFromOperator(quint64 sessionId, const QString& reason);
  void emergencyStopFromOperator(quint64 sessionId);
  // Host-internal auto-correction command. It is deliberately not an operator
  // RPC and is ignored unless the chassis is connected and enabled.
  void setCorrectionCommand(double linearMps, double angularRadps);

 signals:
  void responseReady(quint64 sessionId, quint64 requestId, bool ok,
                     const QJsonObject& data, const QString& errorCode,
                     const QString& errorText);
  void telemetryReady(const QJsonObject& telemetry);
  void driveTelemetryReady(const crawling::DriveTelemetry& telemetry);
  void connectionChanged(quint64 sessionId, bool connected,
                         const QString& message);
  void stateChanged(quint64 sessionId, int state, const QString& reason);
  void logMessage(quint64 sessionId, const QString& message);
  void eventReady(quint64 sessionId, const QString& eventName,
                  const QJsonObject& data);

 private:
  void respond(quint64 sessionId, quint64 requestId, bool ok,
               const QJsonObject& data = {}, const QString& errorCode = {},
               const QString& errorText = {});
  bool acceptOwner(quint64 sessionId, quint64 requestId);
  void loadPersistentSettings();
  void persistSettings(const crawling::DriveSettings& settings);
  void connectDrive(const crawling::DriveSettings& settings,
                    quint64 sessionId);
  void invokeDrive(const QString& method, const QVariant& arg1 = {},
                   const QVariant& arg2 = {});
  void sendEvent(quint64 sessionId, const QString& name,
                 const QJsonObject& data = {});

  QThread* driveThread_ = nullptr;
  crawling::SynchronizedDriveController* drive_ = nullptr;
  crawling::DriveSettings settings_;
  crawling::DriveTelemetry lastTelemetry_;
  quint64 ownerSessionId_ = 0;
  bool driveConnected_ = false;
  bool initialized_ = false;
  bool shuttingDown_ = false;
};

}  // namespace pa1664::remote
