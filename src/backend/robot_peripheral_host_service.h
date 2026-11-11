#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "laser_correction_controller.h"
#include "protocol/protocol_types.h"
#include "rim302_protocol.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QVector3D>

class QThread;
class QTimer;

namespace crawling {
class ClampMotorController;
class LaserCorrectionController;
class RobotSensorController;
class RobotUsbCameraController;
}

namespace pa1664::remote {

class RobotHostService;

// Serialises all robot media on a dedicated thread. Inputs are mailbox-style:
// producers replace a pending frame instead of growing a queue, so a slow
// network or JPEG encoder can only drop stale media, never back-pressure the
// chassis control or sensor acquisition threads.
class RobotPeripheralMediaEncoder final : public QObject {
  Q_OBJECT
 public:
  explicit RobotPeripheralMediaEncoder(QObject* parent = nullptr);

 public slots:
  void setActive(bool active);
  void submitLaserPoints(const QVector<QVector3D>& points);
  // Complete weld observation (full single-row profile + gap detection + SDK
  // frame metadata). Kept in a separate mailbox from the decimated preview
  // point cloud so the two producers can never alternate inside one stream.
  void submitProfileObservation(const QVector<QVector3D>& points,
                                const crawling::LaserGapDetection& detection,
                                quint32 frameNumber,
                                qint64 receivedAtEpochMs);
  void submitUsbImage(int slot, const QImage& image);
  void submitImu(const crawling::ImuSample& sample);
  void shutdown();

 signals:
  void mediaReady(quint16 streamId, pa1664::remote::MediaStreamKind kind,
                  const QByteArray& payload);
  void logMessage(const QString& message);

 private:
  void schedule();
  void encodePending();
  bool intervalElapsed(qint64* lastMs, qint64 intervalMs, qint64 nowMs);

  QTimer* timer_ = nullptr;
  QVector<QVector3D> laserPoints_;
  QByteArray profileObservationPayload_;
  bool profileObservationPending_ = false;
  qint64 lastProfileObservationMs_ = -100000;
  QImage usbImages_[crawling::kUsbCameraChannelCount];
  crawling::ImuSample imuSample_;
  bool laserPointsPending_ = false;
  bool usbImagePending_[crawling::kUsbCameraChannelCount] = {false, false};
  bool imuPending_ = false;
  bool active_ = false;
  qint64 lastLaserPointsMs_ = -100000;
  qint64 lastUsbImageMs_[crawling::kUsbCameraChannelCount] = {-100000, -100000};
  qint64 lastImuMs_ = -100000;
};

// Host-side owner for all non-drive robot hardware. It validates that every
// command belongs to the single control session, owns the hardware worker
// threads, and turns controller signals into transport events/media. The
// service never renders UI and never accepts commands without an owner.
class RobotPeripheralHostService final : public QObject {
  Q_OBJECT
 public:
  explicit RobotPeripheralHostService(RobotHostService* drive,
                                      QObject* parent = nullptr);
  ~RobotPeripheralHostService() override;

  quint64 ownerSessionId() const { return ownerSessionId_; }
  QJsonObject snapshot() const;

 public slots:
  void initialize();
  void shutdown();
  bool claimSession(quint64 sessionId);
  void releaseSession(quint64 sessionId);
  void handleRequest(quint64 sessionId, quint64 requestId,
                     const QJsonObject& request);
  void processDriveTelemetry(const crawling::DriveTelemetry& telemetry);
  void mediaReceptionActive(bool active);
  void interruptAutoCorrection(const QString& reason);
  void safetyStop(quint64 sessionId, const QString& reason);
  // True while the Host-internal correction loop is running or preparing.
  // Stale operator motion-lease expiries must not interrupt it.
  bool correctionRunning() const { return correctionActive_ ||
                                          correctionStartPending_; }

 signals:
  void responseReady(quint64 sessionId, quint64 requestId, bool ok,
                     const QJsonObject& data, const QString& errorCode,
                     const QString& errorText);
  void eventReady(quint64 sessionId, const QString& eventName,
                  const QJsonObject& data);
  void mediaReady(quint16 streamId, pa1664::remote::MediaStreamKind kind,
                  const QByteArray& payload);
  void logMessage(quint64 sessionId, const QString& message);
  void driveCorrectionCommand(double linearMps, double angularRadps);

 private:
  void respond(quint64 sessionId, quint64 requestId, bool ok,
               const QJsonObject& data = {}, const QString& errorCode = {},
               const QString& errorText = {});
  bool acceptOwner(quint64 sessionId, quint64 requestId);
  void sendEvent(quint64 sessionId, const QString& name,
                 const QJsonObject& data = {});
  void handleImuRequest(quint64 sessionId, quint64 requestId,
                        const QString& operation, const QJsonObject& data);
  void handleLaserRequest(quint64 sessionId, quint64 requestId,
                          const QString& operation, const QJsonObject& data);
  void handleCameraRequest(quint64 sessionId, quint64 requestId,
                           const QString& operation,
                           const QJsonObject& data);
  void handleClampRequest(quint64 sessionId, quint64 requestId,
                          const QString& operation, const QJsonObject& data);
  void handleCorrectionRequest(quint64 sessionId, quint64 requestId,
                               const QString& operation,
                               const QJsonObject& data);
  void startAutoCorrection(quint64 sessionId, quint64 requestId,
                           const QJsonObject& data);
  void stopAutoCorrection(const QString& reason, bool notify);
  void setAutoCorrectionPaused(bool paused);
  void publishCorrectionStatus();
  void applyClampSettings(const crawling::DriveSettings& settings,
                          bool persist);
  void stopClampHardware();
  void startPortEnumerationThread();
  void stopPortEnumerationThread();
  void startSensorThreads();
  void stopSensorThreads();
  QString clampStatusText() const;
  void appendLog(const QString& message);
  // A stalled preview is otherwise indistinguishable from a dead camera: count
  // every stage of the frame handshake (frameChanged -> submitUsbImage ->
  // frameConsumed) and emit one aggregated CAMERA.PIPELINE line per 2 s.
  void maybeLogCameraPipeline(qint64 nowMs);

  RobotHostService* drive_ = nullptr;
  QThread* sensorThread_ = nullptr;
  QThread* cameraThread_ = nullptr;
  QThread* clampThread_ = nullptr;
  QThread* correctionThread_ = nullptr;
  QThread* encoderThread_ = nullptr;
  QThread* portEnumerationThread_ = nullptr;
  struct PortEnumerationRequest {
    quint64 sessionId = 0;
    quint64 requestId = 0;
  };
  QObject* portEnumerationWorker_ = nullptr;
  crawling::RobotSensorController* sensor_ = nullptr;
  crawling::RobotUsbCameraController* camera_ = nullptr;
  crawling::ClampMotorController* clamp_ = nullptr;
  crawling::LaserCorrectionController* correction_ = nullptr;
  RobotPeripheralMediaEncoder* encoder_ = nullptr;

  crawling::DriveSettings settings_;
  crawling::DriveTelemetry lastTelemetry_;
  crawling::LaserCorrectionStatus lastCorrectionStatus_;
  QHash<int, double> clampPositions_;
  QStringList laserDevices_;
  QStringList usbDevices_;
  QString clampStatus_;
  quint64 ownerSessionId_ = 0;
  quint64 laserFrameSequence_ = 0;
  int lastLaserImageWidth_ = 0;
  int lastLaserImageHeight_ = 0;
  int lastLaserPointCount_ = 0;
  quint64 cameraFrameSequence_[crawling::kUsbCameraChannelCount] = {0, 0};
  // Per-slot camera frame counters for the 2 s CAMERA.PIPELINE window.
  quint64 cameraFramesIn_[crawling::kUsbCameraChannelCount] = {0, 0};
  quint64 cameraFramesSubmitted_[crawling::kUsbCameraChannelCount] = {0, 0};
  quint64 cameraFramesAcked_[crawling::kUsbCameraChannelCount] = {0, 0};
  quint64 cameraFramesDropped_[crawling::kUsbCameraChannelCount] = {0, 0};
  qint64 lastCameraPipelineLogMs_ = -100000;
  qint64 lastLaserEventMs_ = -100000;
  qint64 lastCorrectionStatusMs_ = -100000;
  bool initialized_ = false;
  bool shuttingDown_ = false;
  bool portEnumerationPending_ = false;
  quint64 portEnumerationGeneration_ = 0;
  QVector<PortEnumerationRequest> portEnumerationRequests_;
  bool mediaReceptionActive_ = false;
  bool driveConnected_ = false;
  bool imuConnected_ = false;
  bool laserConnected_ = false;
  bool usbConnected_[crawling::kUsbCameraChannelCount] = {false, false};
  bool clampConnected_ = false;
  bool correctionStartPending_ = false;
  bool correctionActive_ = false;
  bool correctionPaused_ = false;
};

}  // namespace pa1664::remote
