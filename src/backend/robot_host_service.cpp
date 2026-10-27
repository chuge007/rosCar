#include "robot_host_service.h"

#include "protocol/protocol_types.h"
#include "services/robot_json_codec.h"
#include "robot/synchronized_drive_controller.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonValue>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <cmath>

namespace pa1664::remote {
RobotHostService::RobotHostService(QObject* parent) : QObject(parent) {
  qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
  qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
  qRegisterMetaType<crawling::DriveState>("crawling::DriveState");
  loadPersistentSettings();
}

RobotHostService::~RobotHostService() { shutdown(); }

QJsonObject RobotHostService::snapshot() const {
  QJsonObject data;
  data.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
  data.insert(QStringLiteral("driveConnected"), driveConnected_);
  data.insert(QStringLiteral("settings"), driveSettingsToJson(settings_));
  data.insert(QStringLiteral("telemetry"), driveTelemetryToJson(lastTelemetry_));
  return data;
}

void RobotHostService::initialize() {
  if (initialized_) return;
  initialized_ = true;
  driveThread_ = new QThread(this);
  driveThread_->setObjectName(QStringLiteral("RobotDriveControl"));
  drive_ = new crawling::SynchronizedDriveController;
  drive_->moveToThread(driveThread_);
  connect(driveThread_, &QThread::started, drive_,
          &crawling::SynchronizedDriveController::startControlLoop);
  connect(driveThread_, &QThread::finished, drive_,
          &QObject::deleteLater);
  connect(drive_, &crawling::SynchronizedDriveController::telemetryChanged,
          this, [this](const crawling::DriveTelemetry& telemetry) {
            lastTelemetry_ = telemetry;
            emit driveTelemetryReady(telemetry);
            emit telemetryReady(driveTelemetryToJson(telemetry));
          }, Qt::QueuedConnection);
  connect(drive_, &crawling::SynchronizedDriveController::stateChanged,
          this, [this](crawling::DriveState state, const QString& reason) {
            const int rawState = static_cast<int>(state);
            emit stateChanged(ownerSessionId_, rawState, reason);
            emit eventReady(ownerSessionId_, QStringLiteral("robot.stateChanged"),
                            QJsonObject{{QStringLiteral("state"), rawState},
                                        {QStringLiteral("stateKey"),
                                         driveStateKey(state)},
                                        {QStringLiteral("reason"), reason}});
          }, Qt::QueuedConnection);
  connect(drive_, &crawling::SynchronizedDriveController::connectionChanged,
          this, [this](bool connected, const QString& message) {
            driveConnected_ = connected;
            emit connectionChanged(ownerSessionId_, connected, message);
            emit eventReady(ownerSessionId_,
                            QStringLiteral("robot.connectionChanged"),
                            QJsonObject{{QStringLiteral("connected"), connected},
                                        {QStringLiteral("message"), message}});
          }, Qt::QueuedConnection);
  connect(drive_, &crawling::SynchronizedDriveController::logMessage,
          this, [this](const QString& message) {
            emit logMessage(ownerSessionId_, message);
            emit eventReady(ownerSessionId_, QStringLiteral("robot.log"),
                            QJsonObject{{QStringLiteral("message"), message}});
          }, Qt::QueuedConnection);
  connect(drive_, &crawling::SynchronizedDriveController::canSettingsDetected,
          this, [this](const crawling::HardwareDetectionResult& result) {
            emit eventReady(ownerSessionId_,
                            QStringLiteral("robot.canSettingsDetected"),
                            QJsonObject{
                                {QStringLiteral("wheelDetected"),
                                 result.wheelDetected},
                                {QStringLiteral("wheelCanPort"),
                                 result.wheelCanPort},
                                {QStringLiteral("wheelCanBitrate"),
                                 result.wheelCanBitrate},
                                {QStringLiteral("details"),
                                 result.details.join(QStringLiteral(" | "))}});
          }, Qt::QueuedConnection);
  driveThread_->start(QThread::HighPriority);
  QTimer::singleShot(0, this, [this] {
    if (shuttingDown_) return;
    if (settings_.autoConnectOnStartup &&
        settings_.validationError().isEmpty()) {
      connectDrive(settings_, ownerSessionId_);
    }
  });
}

void RobotHostService::shutdown() {
  if (shuttingDown_) return;
  shuttingDown_ = true;

  if (driveThread_) {
    QThread* const driveThread = driveThread_;
    crawling::SynchronizedDriveController* const drive = drive_;
    if (drive && driveThread->isRunning()) {
      // Run both stop operations in RobotDriveControl before its event loop
      // exits. A BlockingQueuedConnection here would let a wedged hardware
      // driver hold the Host main thread without a bound.
      driveThread->requestInterruption();
      const bool queued = QMetaObject::invokeMethod(
          drive,
          [drive, driveThread] {
            drive->emergencyStop();
            drive->shutdown();
            driveThread->quit();
          },
          Qt::QueuedConnection);
      if (!queued) driveThread->quit();
    } else {
      driveThread->requestInterruption();
      driveThread->quit();
    }

    if (!driveThread->wait(3000)) {
      qWarning() << "ROBOT_DRIVE_THREAD_SHUTDOWN_TIMEOUT"
                 << driveThread->objectName();
      driveThread->terminate();
      driveThread->wait();
    }
    delete driveThread;
    driveThread_ = nullptr;
  }
  drive_ = nullptr;
}

bool RobotHostService::claimSession(quint64 sessionId) {
  if (sessionId == 0) return false;
  if (ownerSessionId_ == 0) {
    ownerSessionId_ = sessionId;
    emit eventReady(ownerSessionId_, QStringLiteral("host.sessionClaimed"),
                    snapshot());
    return true;
  }
  return ownerSessionId_ == sessionId;
}

void RobotHostService::releaseSession(quint64 sessionId) {
  if (sessionId == 0 || sessionId != ownerSessionId_) return;
  const quint64 closingSession = ownerSessionId_;
  if (drive_) {
    invokeDrive(QStringLiteral("setInputCommand"), 0.0, 0.0);
    invokeDrive(QStringLiteral("requestEnable"), false);
  }
  emit eventReady(closingSession, QStringLiteral("host.sessionReleased"),
                  QJsonObject{{QStringLiteral("reason"),
                               QStringLiteral("control_connection_closed")}});
  ownerSessionId_ = 0;
}

void RobotHostService::handleRequest(quint64 sessionId, quint64 requestId,
                                     const QJsonObject& request) {
  if (!acceptOwner(sessionId, requestId)) return;
  const QString operation =
      request.value(QStringLiteral("operation")).toString().trimmed();
  const QJsonObject data = request.value(QStringLiteral("data")).toObject();
  if (operation.isEmpty()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_operation"),
            QStringLiteral("请求缺少 operation"));
    return;
  }
  if (operation == QStringLiteral("system.snapshot") ||
      operation == QStringLiteral("robot.snapshot") ||
      operation == QStringLiteral("robot.settings.get")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  if (operation == QStringLiteral("robot.connect")) {
    crawling::DriveSettings requested = settings_;
    const QJsonObject settingsJson =
        data.contains(QStringLiteral("iniBase64")) ? data
                                                   : data.value(QStringLiteral("settings")).toObject();
    if (!settingsJson.isEmpty()) {
      QString error;
      if (!driveSettingsFromJson(settingsJson, &requested, &error)) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("invalid_settings"), error);
        return;
      }
    }
    const QString validation = requested.validationError();
    if (!validation.isEmpty()) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_settings"), validation);
      return;
    }
    settings_ = requested;
    persistSettings(settings_);
    connectDrive(settings_, sessionId);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.disconnect")) {
    invokeDrive(QStringLiteral("disconnectAdapter"));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.enable")) {
    invokeDrive(QStringLiteral("requestEnable"),
                data.value(QStringLiteral("enabled")).toBool(false));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.emergencyStop") ||
      operation == QStringLiteral("robot.estop")) {
    emergencyStopFromOperator(sessionId);
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.reset")) {
    invokeDrive(QStringLiteral("systemReset"));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.clearAlarm")) {
    invokeDrive(QStringLiteral("clearAlarm"));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.motion.stop") ||
      operation == QStringLiteral("robot.stop")) {
    invokeDrive(QStringLiteral("setInputCommand"), 0.0, 0.0);
    emit eventReady(sessionId, QStringLiteral("robot.motionStopped"),
                    QJsonObject{{QStringLiteral("reason"),
                                 QStringLiteral("operator_stop")}});
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.settings.update")) {
    crawling::DriveSettings requested = settings_;
    const QJsonObject settingsJson =
        data.contains(QStringLiteral("iniBase64"))
            ? data
            : data.value(QStringLiteral("settings")).toObject();
    QString error;
    if (!driveSettingsFromJson(settingsJson, &requested, &error)) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_settings"), error);
      return;
    }
    settings_ = requested;
    persistSettings(settings_);
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的机器人操作：%1").arg(operation));
}

void RobotHostService::setMotionFromOperator(quint64 sessionId,
                                             double linearMps,
                                             double angularRadps) {
  if (sessionId == 0 || sessionId != ownerSessionId_ || shuttingDown_) return;
  if (!std::isfinite(linearMps) || !std::isfinite(angularRadps)) return;

  // The controller enforces its own limits and command lease.  Clamp only to
  // the wire format's integer range before it reaches that safety state
  // machine so a malformed packet cannot become an enormous target.
  linearMps = qBound(-20.0, linearMps, 20.0);
  angularRadps = qBound(-20.0, angularRadps, 20.0);
  invokeDrive(QStringLiteral("setInputCommand"), linearMps, angularRadps);
}

void RobotHostService::stopFromOperator(quint64 sessionId,
                                        const QString& reason) {
  if (sessionId == 0 || sessionId != ownerSessionId_ || shuttingDown_) return;
  invokeDrive(QStringLiteral("setInputCommand"), 0.0, 0.0);
  emit eventReady(sessionId, QStringLiteral("robot.motionStopped"),
                  QJsonObject{{QStringLiteral("reason"), reason}});
}

void RobotHostService::emergencyStopFromOperator(quint64 sessionId) {
  if (sessionId == 0 || sessionId != ownerSessionId_ || shuttingDown_) return;
  invokeDrive(QStringLiteral("setInputCommand"), 0.0, 0.0);
  invokeDrive(QStringLiteral("emergencyStop"));
  emit eventReady(sessionId, QStringLiteral("robot.emergencyStop"),
                  QJsonObject{{QStringLiteral("latched"), true}});
}

void RobotHostService::setCorrectionCommand(double linearMps,
                                           double angularRadps) {
  if (shuttingDown_ || ownerSessionId_ == 0 || !driveConnected_ ||
      lastTelemetry_.state != crawling::DriveState::Enabled) {
    return;
  }
  if (!std::isfinite(linearMps) || !std::isfinite(angularRadps)) return;
  linearMps = qBound(-20.0, linearMps, 20.0);
  angularRadps = qBound(-20.0, angularRadps, 20.0);
  invokeDrive(QStringLiteral("setInputCommand"), linearMps, angularRadps);
}

void RobotHostService::respond(quint64 sessionId, quint64 requestId, bool ok,
                               const QJsonObject& data,
                               const QString& errorCode,
                               const QString& errorTextValue) {
  emit responseReady(sessionId, requestId, ok, data, errorCode,
                     errorTextValue);
}

bool RobotHostService::acceptOwner(quint64 sessionId, quint64 requestId) {
  if (sessionId != 0 && sessionId == ownerSessionId_) return true;
  respond(sessionId, requestId, false, {},
          QStringLiteral("not_session_owner"),
          ownerSessionId_ == 0
              ? QStringLiteral("控制会话尚未建立")
              : QStringLiteral("另一个操作台正在控制小车"));
  return false;
}

void RobotHostService::loadPersistentSettings() {
  QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                       QSettings::IniFormat);
  settings_ = crawling::DriveSettings::load(persistent);
}

void RobotHostService::persistSettings(
    const crawling::DriveSettings& settings) {
  QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                       QSettings::IniFormat);
  settings.save(persistent);
  persistent.sync();
}

void RobotHostService::connectDrive(const crawling::DriveSettings& settings,
                                    quint64 sessionId) {
  if (!drive_ || !driveThread_ || !driveThread_->isRunning()) return;
  QMetaObject::invokeMethod(
      drive_, [controller = drive_, settings] {
        controller->connectAdapter(settings);
      },
      Qt::QueuedConnection);
  emit eventReady(sessionId, QStringLiteral("robot.connecting"),
                  QJsonObject{{QStringLiteral("message"),
                               QStringLiteral("正在连接底盘适配器")}});
}

void RobotHostService::invokeDrive(const QString& method, const QVariant& arg1,
                                   const QVariant& arg2) {
  if (!drive_ || !driveThread_ || !driveThread_->isRunning()) return;
  if (method == QStringLiteral("requestEnable")) {
    QMetaObject::invokeMethod(drive_, "requestEnable", Qt::QueuedConnection,
                              Q_ARG(bool, arg1.toBool()));
    return;
  }
  if (method == QStringLiteral("setInputCommand")) {
    QMetaObject::invokeMethod(drive_, "setInputCommand", Qt::QueuedConnection,
                              Q_ARG(double, arg1.toDouble()),
                              Q_ARG(double, arg2.toDouble()));
    return;
  }
  QMetaObject::invokeMethod(drive_, method.toLatin1().constData(),
                            Qt::QueuedConnection);
}

void RobotHostService::sendEvent(quint64 sessionId, const QString& name,
                                 const QJsonObject& data) {
  emit eventReady(sessionId, name, data);
}

}  // namespace pa1664::remote
