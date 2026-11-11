#include "host_application.h"

#include "transport/media_channel.h"
#include "robot_host_service.h"
#include "robot_peripheral_host_service.h"
#include "transport/bulk_channel.h"
#include "transport/control_server.h"
#include "transport/realtime_channel.h"
#include "ultrasound_host_service.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonValue>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <exception>
#include <functional>

namespace pa1664::remote {

namespace {
// The vendor ultrasound SDK throws C++ exceptions from worker-thread callbacks
// (observed: eintik::ScopedFramework / GlobalConfig inside Client::getInstance,
// aborting the whole robot host ~5-50s after start). Qt does not catch
// exceptions that escape a slot, so the event loop itself stays exception-safe:
// log the reason, then let the thread end instead of killing the process.
class GuardedThread final : public QThread {
 public:
  using QThread::QThread;
  std::function<void(const QString&)> onException;

 protected:
  void run() override {
    try {
      exec();
    } catch (const std::exception& e) {
      if (onException)
        onException(QString::fromUtf8(e.what()));
    } catch (...) {
      if (onException)
        onException(QStringLiteral("未知类型"));
    }
  }
};
}  // namespace

HostApplication::HostApplication(QObject* parent)
    : QObject(parent),
      control_(new ControlServer(this)),
      realtime_(new RealtimeServer(this)),
      media_(new MediaServer(this)),
      bulk_(new BulkServer(this)),
      robot_(new RobotHostService(this)),
      peripheral_(new RobotPeripheralHostService(robot_, this)),
      bulkDrainTimer_(new QTimer(this)) {
  bulkDrainTimer_->setInterval(10);
  connect(bulkDrainTimer_, &QTimer::timeout, this,
          &HostApplication::drainBulkQueue);
  wireServices();
}

HostApplication::~HostApplication() { stop(); }

bool HostApplication::start(const QHostAddress& address, quint16 controlPort,
                            quint16 realtimePort, quint16 mediaPort,
                            quint16 bulkPort, QString* error) {
  if (error) error->clear();
  if (started_) return true;
  listenAddress_ = address;
  controlPort_ = controlPort;
  realtimePort_ = realtimePort;
  mediaPort_ = mediaPort;
  bulkPort_ = bulkPort;

  QString realtimeError;
  if (!realtime_->start(address, realtimePort, &realtimeError)) {
    if (error) *error = QStringLiteral("实时通道监听失败：%1").arg(realtimeError);
    return false;
  }
  QString mediaError;
  if (!media_->start(address, mediaPort, &mediaError)) {
    realtime_->stop();
    if (error) *error = QStringLiteral("媒体通道监听失败：%1").arg(mediaError);
    return false;
  }
  QString bulkError;
  if (!bulk_->start(address, bulkPort, &bulkError)) {
    media_->stop();
    realtime_->stop();
    if (error) *error = QStringLiteral("批量通道监听失败：%1").arg(bulkError);
    return false;
  }
  control_->setAdvertisedRealtimePort(realtime_->serverPort());
  control_->setAdvertisedMediaPort(media_->serverPort());
  control_->setAdvertisedBulkPort(bulk_->serverPort());
  QString controlError;
  if (!control_->start(address, controlPort, &controlError)) {
    bulk_->stop();
    media_->stop();
    realtime_->stop();
    if (error) *error = QStringLiteral("控制通道监听失败：%1").arg(controlError);
    return false;
  }
  control_->setCapabilities(QStringList{
      QString::fromLatin1(kCapabilityRobotControlV1),
      QString::fromLatin1(kCapabilityRobotTelemetryV1),
      QString::fromLatin1(kCapabilityRobotSettingsV1),
      QString::fromLatin1(kCapabilityRobotSensorsV1),
      QString::fromLatin1(kCapabilityRobotMediaV1),
      QString::fromLatin1(kCapabilityUltrasoundControlV1),
      QString::fromLatin1(kCapabilityUltrasoundParametersV1),
      QString::fromLatin1(kCapabilityUltrasoundMediaV1),
      QString::fromLatin1(kCapabilityUltrasoundBulkV1),
  });
  started_ = true;
  startUltrasoundService();
  robot_->initialize();
  peripheral_->initialize();
  return true;
}

void HostApplication::stop() {
  if (!started_) return;
  started_ = false;
  // Latch the chassis stop before potentially slow peripheral/ultrasound SDK
  // shutdown.  Transport objects remain alive until all hardware is released.
  robot_->shutdown();
  peripheral_->shutdown();
  stopUltrasoundService();
  control_->stop();
  bulk_->stop();
  media_->stop();
  realtime_->stop();
}

bool HostApplication::isRunning() const {
  return started_ && control_ && realtime_ && media_ && bulk_ &&
         control_->isListening() && realtime_->isListening() &&
         media_->isListening() && bulk_->isListening();
}

QJsonObject HostApplication::runtimeStatus() const {
  QJsonObject status;
  status.insert(QStringLiteral("running"), isRunning());
  status.insert(QStringLiteral("controlAddress"),
                listenAddress_.toString());
  status.insert(QStringLiteral("controlPort"),
                static_cast<int>(controlPort_));
  status.insert(QStringLiteral("realtimePort"),
                static_cast<int>(realtimePort_));
  status.insert(QStringLiteral("mediaPort"),
                static_cast<int>(mediaPort_));
  status.insert(QStringLiteral("bulkPort"),
                static_cast<int>(bulkPort_));
  status.insert(QStringLiteral("sessions"),
                static_cast<int>(control_->sessionCount()));
  status.insert(QStringLiteral("pendingBulkMessages"),
                static_cast<int>(pendingBulk_.size()));
  status.insert(QStringLiteral("pendingBulkBytes"),
                static_cast<double>(pendingBulkBytes_));
  status.insert(QStringLiteral("robot"), robot_->snapshot());
  status.insert(QStringLiteral("peripherals"), peripheral_->snapshot());
  status.insert(QStringLiteral("timeMs"),
                static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
  return status;
}

void HostApplication::wireServices() {
  connect(control_, &ControlServer::sessionOpened, this,
          &HostApplication::onSessionOpened);
  connect(control_, &ControlServer::sessionClosed, this,
          &HostApplication::onSessionClosed);
  connect(control_, &ControlServer::requestReceived, this,
          &HostApplication::onRequest);
  connect(control_, &ControlServer::protocolError, this,
          [](quint64 sessionId, const QString& detail) {
            qWarning().noquote() << "CONTROL_PROTOCOL" << sessionId << detail;
          });

  connect(robot_, &RobotHostService::responseReady, control_,
          &ControlServer::sendResponse);
  connect(robot_, &RobotHostService::eventReady, this,
          &HostApplication::sendSessionEvent);
  connect(robot_, &RobotHostService::telemetryReady, this,
          &HostApplication::onRobotTelemetry);
  connect(robot_, &RobotHostService::logMessage, this,
          [](quint64 sessionId, const QString& message) {
            qInfo().noquote() << "ROBOT" << sessionId << message;
          });
  connect(robot_, &RobotHostService::connectionChanged, this,
          [this](quint64 sessionId, bool connected, const QString& message) {
            qInfo().noquote() << "ROBOT_CONNECTION" << connected << message;
            sendSessionEvent(
                sessionId, QStringLiteral("robot.connectionChanged"),
                QJsonObject{{QStringLiteral("connected"), connected},
                            {QStringLiteral("message"), message}});
          });

  connect(peripheral_, &RobotPeripheralHostService::responseReady, control_,
          &ControlServer::sendResponse);
  connect(peripheral_, &RobotPeripheralHostService::eventReady, this,
          &HostApplication::sendSessionEvent);
  connect(peripheral_, &RobotPeripheralHostService::logMessage, this,
          [this](quint64 sessionId, const QString& message) {
            qInfo().noquote() << "ROBOT_PERIPHERAL" << sessionId << message;
            // Clamp/IMU/camera diagnostics used to stay in the host log file,
            // so an operator could never see which clamp motor failed to
            // answer. Forward them as an event the console can display.
            sendSessionEvent(sessionId, QStringLiteral("robot.peripheral.log"),
                             QJsonObject{{QStringLiteral("message"), message}});
          });
  connect(peripheral_, &RobotPeripheralHostService::mediaReady, this,
          [this](quint16 streamId, MediaStreamKind kind,
                 const QByteArray& payload) {
            const quint64 owner = robot_->ownerSessionId();
            const bool hasPeer = owner != 0 && media_->hasPeer(owner);
            if (!hasPeer || payload.isEmpty()) {
              ++mediaTxDropped_;
            } else {
              media_->sendMessage(owner, streamId, kind, payload);
              noteMediaTx(streamId, payload.size());
            }
            maybeLogMediaTx();
          });
  connect(peripheral_,
          &RobotPeripheralHostService::driveCorrectionCommand, robot_,
          &RobotHostService::setCorrectionCommand, Qt::QueuedConnection);
  connect(robot_, &RobotHostService::driveTelemetryReady, peripheral_,
          &RobotPeripheralHostService::processDriveTelemetry,
          Qt::QueuedConnection);

  connect(realtime_, &RealtimeServer::motionSetpointReceived, this,
          &HostApplication::onMotionSetpoint);
  connect(realtime_, &RealtimeServer::emergencyStopReceived, this,
          [this](quint64 sessionId) {
            peripheral_->safetyStop(sessionId,
                                    QStringLiteral("udp_emergency_stop"));
            robot_->emergencyStopFromOperator(sessionId);
          });
  connect(realtime_, &RealtimeServer::motionLeaseExpired, this,
          [this](quint64 sessionId) {
            // 手动控制结束后的最后一个零速 MotionSetpoint 包同样会激活
            // 200 ms 租约。纠偏由 Host 内部闭环驱动、不依赖操作端手动
            // 租约，且纠偏运行期间手动通道被禁用，此时的租约过期只是
            // 残留包到期，不得打断纠偏。真正的急停
            // （udp_emergency_stop）与会话超时走各自独立分支，不受影响。
            if (peripheral_->correctionRunning()) return;
            peripheral_->safetyStop(sessionId,
                                    QStringLiteral("motion_lease_expired"));
            robot_->stopFromOperator(sessionId,
                                     QStringLiteral("motion_lease_expired"));
            sendSessionEvent(sessionId, QStringLiteral("robot.safetyStop"),
                             QJsonObject{{QStringLiteral("reason"),
                                          QStringLiteral("motion_lease_expired")}});
          });
  connect(realtime_, &RealtimeServer::sessionTimedOut, this,
          [this](quint64 sessionId) {
            peripheral_->safetyStop(sessionId,
                                    QStringLiteral("operator_timeout"));
            robot_->stopFromOperator(sessionId,
                                     QStringLiteral("operator_timeout"));
            sendSessionEvent(sessionId, QStringLiteral("robot.safetyStop"),
                             QJsonObject{{QStringLiteral("reason"),
                                          QStringLiteral("operator_timeout")}});
          });
  connect(realtime_, &RealtimeServer::sessionRecovered, this,
          [this](quint64 sessionId) {
            sendSessionEvent(sessionId, QStringLiteral("realtime.recovered"));
          });
  connect(realtime_, &RealtimeServer::packetRejected, this,
          [](quint64 sessionId, const QString& detail) {
            qWarning().noquote() << "REALTIME_REJECT" << sessionId << detail;
          });
  connect(media_, &MediaServer::packetRejected, this,
          [](quint64 sessionId, const QString& detail) {
            qWarning().noquote() << "MEDIA_REJECT" << sessionId << detail;
          });
  connect(bulk_, &BulkServer::errorOccurred, this,
          [](quint64 sessionId, const QString& detail) {
            qWarning().noquote() << "BULK_CHANNEL" << sessionId << detail;
          });
  connect(bulk_, &BulkServer::sessionAuthenticated, this, [this](quint64) {
    if (!pendingBulk_.isEmpty()) bulkDrainTimer_->start();
  });
  connect(media_, &MediaServer::peerDiscovered, this, [this](quint64 sessionId) {
    qInfo().noquote() << "MEDIA_PEER_READY" << sessionId;
    if (sessionId == robot_->ownerSessionId()) {
      peripheral_->mediaReceptionActive(true);
    }
  });
}

void HostApplication::onSessionOpened(quint64 sessionId,
                                      const QString& clientId,
                                      const QString& peerAddress) {
  qInfo().noquote() << "CONTROL_SESSION_OPEN" << sessionId << clientId
                    << peerAddress;
  const QByteArray key = control_->sessionKey(sessionId);
  if (key.size() != 32) {
    qWarning() << "CONTROL_SESSION_KEY_INVALID" << sessionId;
    control_->closeSession(sessionId, QStringLiteral("会话密钥无效"));
    return;
  }
  const bool driveClaimed = robot_->claimSession(sessionId);
  const bool peripheralsClaimed =
      driveClaimed && peripheral_->claimSession(sessionId);
  if (!driveClaimed || !peripheralsClaimed) {
    if (peripheralsClaimed) peripheral_->releaseSession(sessionId);
    if (driveClaimed) robot_->releaseSession(sessionId);
    qWarning().noquote() << "CONTROL_SESSION_BUSY" << sessionId;
    sendSessionEvent(
        sessionId, QStringLiteral("host.busy"),
        QJsonObject{{QStringLiteral("message"),
                     QStringLiteral("另一个操作台正在控制小车")},
                    {QStringLiteral("ownerSessionId"),
                     QString::number(robot_->ownerSessionId())}});
    // Give the event a chance to leave the socket before the host closes this
    // observer/candidate session.  No realtime key is registered for it.
    QTimer::singleShot(250, control_, [this, sessionId] {
      control_->closeSession(sessionId, QStringLiteral("主机已有控制会话"));
    });
    return;
  }
  realtime_->registerSession(sessionId, key);
  media_->registerSession(sessionId, key);
  bulk_->registerSession(sessionId, key);
  if (ultrasound_)
    QMetaObject::invokeMethod(ultrasound_, "claimSession",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, sessionId));
  sendSessionEvent(
      sessionId, QStringLiteral("host.ready"),
      QJsonObject{{QStringLiteral("protocolVersion"),
                   static_cast<int>(kProtocolVersion)},
                  {QStringLiteral("sessionId"), QString::number(sessionId)},
                  {QStringLiteral("snapshot"), robot_->snapshot()},
                  {QStringLiteral("peripherals"), peripheral_->snapshot()}});
}

void HostApplication::onSessionClosed(quint64 sessionId,
                                      const QString& reason) {
  qInfo().noquote() << "CONTROL_SESSION_CLOSE" << sessionId << reason;
  // Only the owner owns the reliable bulk stream.  A rejected second
  // operator may close while the real owner is still receiving a scan; never
  // let that unrelated close discard the owner's queued data.
  const bool wasOwner = robot_->ownerSessionId() == sessionId;
  if (wasOwner) {
    pendingBulk_.clear();
    pendingBulkBytes_ = 0;
    bulkDrainTimer_->stop();
  }
  peripheral_->releaseSession(sessionId);
  robot_->releaseSession(sessionId);
  if (ultrasound_)
    QMetaObject::invokeMethod(ultrasound_, "releaseSession",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, sessionId));
  bulk_->removeSession(sessionId);
  media_->removeSession(sessionId);
  realtime_->removeSession(sessionId);
}

void HostApplication::onRequest(quint64 sessionId, quint64 requestId,
                                const QJsonObject& request) {
  const QString operation =
      request.value(QStringLiteral("operation")).toString();
  const bool peripheralOperation =
      operation.startsWith(QStringLiteral("robot.imu.")) ||
      operation.startsWith(QStringLiteral("robot.laser.")) ||
      operation.startsWith(QStringLiteral("robot.camera.")) ||
      operation.startsWith(QStringLiteral("robot.clamp.")) ||
      operation.startsWith(QStringLiteral("robot.autoCorrection.")) ||
      operation.startsWith(QStringLiteral("robot.peripherals.")) ||
      operation.startsWith(QStringLiteral("robot.sensors."));
  if (peripheralOperation) {
    peripheral_->handleRequest(sessionId, requestId, request);
    return;
  }
  if (operation == QStringLiteral("robot.emergencyStop") ||
      operation == QStringLiteral("robot.estop") ||
      operation == QStringLiteral("robot.motion.stop") ||
      operation == QStringLiteral("robot.stop")) {
    peripheral_->safetyStop(sessionId, operation);
  }
  if (operation.startsWith(QStringLiteral("robot."))) {
    robot_->handleRequest(sessionId, requestId, request);
    return;
  }
  if (operation.startsWith(QStringLiteral("ultrasound.")) && ultrasound_) {
    QMetaObject::invokeMethod(
        ultrasound_,
        [service = ultrasound_, sessionId, requestId, request] {
          service->handleRequest(sessionId, requestId, request);
        },
        Qt::QueuedConnection);
    return;
  }
  control_->sendResponse(sessionId, requestId, false, {},
                         QStringLiteral("unsupported_operation"),
                         QStringLiteral("主机不支持操作：%1").arg(operation));
}

void HostApplication::onRobotTelemetry(const QJsonObject& telemetry) {
  const quint64 owner = robot_->ownerSessionId();
  if (owner == 0) return;
  QJsonObject envelope = telemetry;
  envelope.insert(QStringLiteral("kind"), QStringLiteral("driveTelemetry"));
  envelope.insert(QStringLiteral("hostTimeMs"),
                  static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
  realtime_->sendTelemetry(owner, envelope);
}

void HostApplication::onMotionSetpoint(quint64 sessionId,
                                       qint32 linearMilliMps,
                                       qint32 angularMilliRadps,
                                       quint16, quint64) {
  robot_->setMotionFromOperator(sessionId, linearMilliMps / 1000.0,
                                angularMilliRadps / 1000.0);
}

void HostApplication::startUltrasoundService() {
  if (ultrasoundThread_) return;
  auto* ultrasoundThread = new GuardedThread(this);
  ultrasoundThread->setObjectName(QStringLiteral("UltrasoundHostControl"));
  ultrasoundThread->onException =
      [this](const QString& reason) {
        qCritical().noquote() << "ULTRASOUND_THREAD_EXCEPTION" << reason;
        sendSessionEvent(robot_->ownerSessionId(),
                         QStringLiteral("ultrasound.threadException"),
                         QJsonObject{{QStringLiteral("reason"), reason}});
        // The thread is gone now; drop the dangling pointer so a later stop
        // does not wait on a finished thread twice.
        ultrasound_ = nullptr;
      };
  ultrasoundThread_ = ultrasoundThread;
  ultrasound_ = new UltrasoundHostService;
  ultrasound_->moveToThread(ultrasoundThread_);

  connect(ultrasoundThread_, &QThread::started, ultrasound_,
          &UltrasoundHostService::initialize, Qt::QueuedConnection);
  connect(ultrasoundThread_, &QThread::finished, ultrasound_,
          &QObject::deleteLater);
  connect(ultrasound_, &UltrasoundHostService::shutdownComplete,
          ultrasoundThread_, &QThread::quit, Qt::DirectConnection);
  connect(ultrasound_, &UltrasoundHostService::responseReady, control_,
          &ControlServer::sendResponse, Qt::QueuedConnection);
  connect(ultrasound_, &UltrasoundHostService::eventReady, this,
          &HostApplication::sendSessionEvent, Qt::QueuedConnection);
  connect(ultrasound_, &UltrasoundHostService::frameEncoded, this,
          &HostApplication::onUltrasoundFrame, Qt::QueuedConnection);
  connect(ultrasound_, &UltrasoundHostService::scanBatchEncoded, this,
          &HostApplication::onUltrasoundScanBatch, Qt::QueuedConnection);
  connect(ultrasound_, &UltrasoundHostService::logMessage, this,
          [](quint64 sessionId, const QString& message) {
            qInfo().noquote() << "ULTRASOUND" << sessionId << message;
          }, Qt::QueuedConnection);
  ultrasoundThread_->start();
}

void HostApplication::stopUltrasoundService() {
  if (!ultrasoundThread_) return;

  // Never synchronously wait for the vendor SDK on the Host main thread.  The
  // queued shutdown runs in UltrasoundHostControl and quits that thread only
  // after every bounded sub-step has completed.  If the SDK itself is stuck,
  // the outer wait still gives the process a bounded last-resort exit path.
  ultrasoundThread_->requestInterruption();
  bool shutdownQueued = false;
  if (ultrasound_ && ultrasoundThread_->isRunning()) {
    shutdownQueued = QMetaObject::invokeMethod(
        ultrasound_, "shutdown", Qt::QueuedConnection);
  }
  if (!shutdownQueued) ultrasoundThread_->quit();

  // The service may spend up to 5 s on each internal worker thread plus the
  // device stop/disconnect.  20 s avoids cutting off a healthy shutdown while
  // still bounding a hung vendor SDK call.
  if (!ultrasoundThread_->wait(20000)) {
    qWarning() << "ULTRASOUND_SHUTDOWN_TIMEOUT";
    ultrasoundThread_->terminate();
    ultrasoundThread_->wait();
  }
  ultrasound_ = nullptr;
  delete ultrasoundThread_;
  ultrasoundThread_ = nullptr;
  pendingBulk_.clear();
  pendingBulkBytes_ = 0;
  bulkDrainTimer_->stop();
}

void HostApplication::onUltrasoundFrame(const QByteArray& payload, int groupId,
                                        int deviceId) {
  Q_UNUSED(deviceId)
  const quint64 owner = robot_->ownerSessionId();
  if (owner == 0 || payload.isEmpty() || !media_->hasPeer(owner)) return;
  if (groupId < 0 || groupId > 255) return;
  const quint16 streamId =
      static_cast<quint16>(kMediaStreamUltrasoundLiveBase + groupId);
  media_->sendMessage(owner, streamId, MediaStreamKind::UltrasoundFrame, payload);
  noteMediaTx(streamId, payload.size());
  maybeLogMediaTx();
}

void HostApplication::onUltrasoundScanBatch(const QByteArray& payload) {
  if (payload.isEmpty()) return;
  enqueueBulk(BulkStreamKind::UltrasoundScanBatch, payload);
}

void HostApplication::enqueueBulk(BulkStreamKind kind,
                                  const QByteArray& payload) {
  constexpr qint64 kMaximumPendingBulkBytes = 256ll * 1024 * 1024;
  if (payload.size() > kMaxBulkPayloadBytes ||
      pendingBulkBytes_ + payload.size() > kMaximumPendingBulkBytes) {
    qCritical().noquote()
        << "BULK_BACKLOG_OVERFLOW" << pendingBulkBytes_ << payload.size();
    if (ultrasound_) {
      const quint64 owner = robot_->ownerSessionId();
      if (owner)
        sendSessionEvent(
            owner, QStringLiteral("ultrasound.bulkOverflow"),
            QJsonObject{{QStringLiteral("pendingBytes"),
                         static_cast<double>(pendingBulkBytes_)},
                        {QStringLiteral("message"),
                         QStringLiteral("主机可靠批量队列已满，停止采集")}});
      QMetaObject::invokeMethod(
          ultrasound_,
          [service = ultrasound_] {
            service->abortAcquisition(
                QStringLiteral("主机可靠批量队列已满，采集已停止"));
          },
          Qt::QueuedConnection);
    }
    return;
  }
  pendingBulk_.enqueue({kind, payload});
  pendingBulkBytes_ += payload.size();
  if (!bulkDrainTimer_->isActive()) bulkDrainTimer_->start();
}

void HostApplication::drainBulkQueue() {
  const quint64 owner = robot_->ownerSessionId();
  if (owner == 0) {
    bulkDrainTimer_->stop();
    return;
  }
  while (!pendingBulk_.isEmpty()) {
    const PendingBulk& item = pendingBulk_.head();
    if (!bulk_->sendMessage(owner, item.kind, item.payload)) {
      maybeLogBulkTx();
      return;
    }
    ++bulkTxMessages_;
    bulkTxBytes_ += static_cast<quint64>(item.payload.size());
    pendingBulkBytes_ = qMax<qint64>(0, pendingBulkBytes_ - item.payload.size());
    pendingBulk_.dequeue();
  }
  maybeLogBulkTx();
  bulkDrainTimer_->stop();
}

void HostApplication::noteMediaTx(quint16 streamId, qint64 bytes) {
  if (bytes <= 0) return;
  const quint64 size = static_cast<quint64>(bytes);
  ++mediaTxMessages_;
  mediaTxBytes_ += size;
  ++mediaTxStreamMessages_[streamId];
  mediaTxStreamBytes_[streamId] += size;
  if (streamId == kMediaStreamLaserImage) {
    ++mediaTxLaserImage_;
  } else if (streamId == kMediaStreamLaserPointCloud) {
    ++mediaTxLaserPoints_;
  } else if (streamId >= kMediaStreamUsbCameraBase) {
    const int slot = int(streamId - kMediaStreamUsbCameraBase);
    if (slot >= 0 && slot < 2) ++mediaTxUsb_[slot];
  }
}

void HostApplication::maybeLogMediaTx() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (mediaTxLastLogMs_ == 0) {
    mediaTxLastLogMs_ = now;
    return;
  }
  if (now - mediaTxLastLogMs_ < 2000) return;
  // Every 2 s report what actually left the host, plus the server counters
  // (dropped no-peer / oversize / send failures) and a per-stream breakdown so
  // the link budget can be attributed to a producer instead of guessed.
  const MediaServerStats stats = media_->stats();
  const quint64 owner = robot_->ownerSessionId();
  const bool hasPeer = owner != 0 && media_->hasPeer(owner);
  QList<quint16> streamIds = mediaTxStreamBytes_.keys();
  std::sort(streamIds.begin(), streamIds.end());
  QStringList streams;
  streams.reserve(streamIds.size());
  for (quint16 streamId : streamIds) {
    streams << QStringLiteral("0x%1:%2/%3")
                   .arg(streamId, 4, 16, QLatin1Char('0'))
                   .arg(mediaTxStreamMessages_.value(streamId))
                   .arg(mediaTxStreamBytes_.value(streamId));
  }
  qInfo().noquote() << QStringLiteral(
      "MEDIA.TX window_ms=%1 owner=%2 has_peer=%3 "
      "sent=%4 bytes=%5 dropped=%6 laser_img=%7 laser_pts=%8 "
      "usb0=%9 usb1=%10 srv_sent=%11 srv_frag=%12 srv_bytes=%13 "
      "srv_nopeer=%14 srv_oversize=%15 srv_fail=%16 streams=%17")
      .arg(now - mediaTxLastLogMs_)
      .arg(owner)
      .arg(hasPeer ? 1 : 0)
      .arg(mediaTxMessages_)
      .arg(mediaTxBytes_)
      .arg(mediaTxDropped_)
      .arg(mediaTxLaserImage_)
      .arg(mediaTxLaserPoints_)
      .arg(mediaTxUsb_[0])
      .arg(mediaTxUsb_[1])
      .arg(stats.messagesSent)
      .arg(stats.fragmentsSent)
      .arg(stats.bytesSent)
      .arg(stats.droppedNoPeer)
      .arg(stats.droppedOversize)
      .arg(stats.sendFailures)
      .arg(streams.join(QLatin1Char(',')));
  mediaTxMessages_ = 0;
  mediaTxBytes_ = 0;
  mediaTxDropped_ = 0;
  mediaTxLaserImage_ = 0;
  mediaTxLaserPoints_ = 0;
  mediaTxUsb_[0] = 0;
  mediaTxUsb_[1] = 0;
  mediaTxStreamMessages_.clear();
  mediaTxStreamBytes_.clear();
  mediaTxLastLogMs_ = now;
}

void HostApplication::maybeLogBulkTx() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (bulkTxLastLogMs_ == 0) {
    bulkTxLastLogMs_ = now;
    return;
  }
  if (now - bulkTxLastLogMs_ < 2000) return;
  qInfo().noquote() << QStringLiteral(
      "BULK.TX window_ms=%1 sent=%2 bytes=%3 pending_msgs=%4 "
      "pending_bytes=%5")
      .arg(now - bulkTxLastLogMs_)
      .arg(bulkTxMessages_)
      .arg(bulkTxBytes_)
      .arg(pendingBulk_.size())
      .arg(pendingBulkBytes_);
  bulkTxMessages_ = 0;
  bulkTxBytes_ = 0;
  bulkTxLastLogMs_ = now;
}

void HostApplication::sendSessionEvent(quint64 sessionId,
                                       const QString& name,
                                       const QJsonObject& data) {
  if (sessionId == 0) return;
  control_->sendEvent(sessionId, name, data);
}

}  // namespace pa1664::remote

