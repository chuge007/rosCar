#pragma once

#include "framedecoderworker.h"
#include "services/remote_workbench_session.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace pa1664::remote {

// Operator-side adapter for the headless ultrasound host.  It owns no vendor
// SDK or hardware object: live frames arrive on the media channel, ordered scan
// batches arrive on the bulk channel, and all commands use control RPC.
class RemoteUltrasoundSource final : public QObject {
  Q_OBJECT
 public:
  explicit RemoteUltrasoundSource(QObject* parent = nullptr);

  void bindRemoteSession(RemoteWorkbenchSession* session);
  RemoteWorkbenchSession* session() const { return remote_; }
  bool isReady() const;
  QJsonObject status() const { return status_; }

 public slots:
  void requestStatus();
  void connectDevice(const QString& address = QStringLiteral("127.0.0.1"),
                     int deviceId = 0);
  void disconnectDevice();
  void startAcquisition();
  void stopAcquisition();
  void resetEncoder();
  void refreshGeometry();
  void updateDecoderSettings(QVector<GroupScanSettings> groups,
                             int displayGroupId);

 signals:
  void frameDecoded(DecodedFrame frame, int groupId, int deviceId);
  void groupScanBatchDecoded(GroupScanFrameBatch frames, int deviceId);
  void statusChanged(const QJsonObject& status);
  void decoderSettingsApplied();
  void backlogOverflow(qint64 bytes, const QString& message);
  void bulkOverflow(qint64 pendingBytes, const QString& message);
  void message(const QString& text, bool error);
  void readyChanged(bool ready);
  void parametersInvalidated(const QJsonObject& status);

 private slots:
  void onSessionStateChanged(WorkbenchConnectionState state,
                             const QString& detail);
  void onResponseReceived(const QString& operation, quint64 requestId, bool ok,
                          const QJsonObject& data, const QString& errorCode,
                          const QString& errorText);
  void onEventReceived(const QString& eventName, const QJsonObject& data);
  void onMediaMessage(quint16 streamId, MediaStreamKind kind, quint64 messageId,
                      quint64 timestampUs, const QByteArray& payload);
  void onBulkMessage(BulkStreamKind kind, quint64 sequence,
                     const QByteArray& payload);

 private:
  void applyStatus(const QJsonObject& status);
  void reportFailure(const QString& operation, const QString& errorCode,
                     const QString& errorText);

  RemoteWorkbenchSession* remote_ = nullptr;
  QJsonObject status_;
  bool ready_ = false;
};

}  // namespace pa1664::remote
