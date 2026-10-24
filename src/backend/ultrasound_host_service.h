#pragma once

#include "framedecoderworker.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class DeviceController;
class QThread;
class QTimer;

namespace pa1664::remote {

class UltrasoundUplinkEncoder;

// Owns every ultrasound SDK and device-controller call on the robot PC.  The
// object is moved to a dedicated QThread by HostApplication; no network/UI
// thread ever waits for the vendor SDK.
class UltrasoundHostService final : public QObject {
  Q_OBJECT
 public:
  explicit UltrasoundHostService(QObject* parent = nullptr);
  ~UltrasoundHostService() override;

 public slots:
  void initialize();
  void shutdown();
  void abortAcquisition(const QString& reason);
  void claimSession(quint64 sessionId);
  void releaseSession(quint64 sessionId);
  void handleRequest(quint64 sessionId, quint64 requestId,
                     const QJsonObject& request);

 signals:
  void responseReady(quint64 sessionId, quint64 requestId, bool ok,
                     const QJsonObject& data, const QString& errorCode,
                     const QString& errorText);
  void eventReady(quint64 sessionId, const QString& name,
                  const QJsonObject& data);
  void frameEncoded(const QByteArray& payload, int groupId, int deviceId);
  void scanBatchEncoded(const QByteArray& payload);
  void logMessage(quint64 sessionId, const QString& message);
  void shutdownComplete();

 private:
  QJsonObject statusSnapshot() const;
  void respond(quint64 sessionId, quint64 requestId, bool ok,
               const QJsonObject& data = {}, const QString& errorCode = {},
               const QString& errorText = {});
  void sendEvent(const QString& name, const QJsonObject& data = {});
  void sendStatusEvent();
  bool acceptOwner(quint64 sessionId, quint64 requestId);
  void handleParameterSnapshot(quint64 sessionId, quint64 requestId);
  void handleParameterGetMany(quint64 sessionId, quint64 requestId,
                              const QJsonObject& data);
  void handleParameterSet(quint64 sessionId, quint64 requestId,
                          const QJsonObject& data);
  void handleParameterApply(quint64 sessionId, quint64 requestId,
                            const QJsonObject& data);
  void handlePresetRequest(quint64 sessionId, quint64 requestId,
                           const QString& operation, const QJsonObject& data);
  bool readConfigBase64(QByteArray* bytes) const;

  DeviceController* device_ = nullptr;
  FrameDecoderWorker* decoder_ = nullptr;
  QThread* decoderThread_ = nullptr;
  UltrasoundUplinkEncoder* encoder_ = nullptr;
  QThread* encoderThread_ = nullptr;
  QTimer* statusTimer_ = nullptr;
  quint64 ownerSessionId_ = 0;
  bool initialized_ = false;
  bool lastHardwareOnline_ = false;
  bool lastCaptureReady_ = false;
  bool shuttingDown_ = false;
};

}  // namespace pa1664::remote

