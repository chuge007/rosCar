#include "remote_workbench_session.h"

#include "transport/bulk_channel.h"
#include "transport/control_client.h"
#include "transport/media_channel.h"
#include "transport/realtime_channel.h"

#include <QDebug>
#include <QJsonValue>
#include <QThread>

namespace pa1664::remote {

QString workbenchConnectionStateText(WorkbenchConnectionState state) {
  switch (state) {
    case WorkbenchConnectionState::Offline:
      return QStringLiteral("上位机：未连接");
    case WorkbenchConnectionState::Connecting:
      return QStringLiteral("上位机：正在连接");
    case WorkbenchConnectionState::Online:
      return QStringLiteral("上位机：在线");
    case WorkbenchConnectionState::Degraded:
      return QStringLiteral("上位机：链路降级");
  }
  return QStringLiteral("上位机：未知");
}

RemoteWorkbenchSession::RemoteWorkbenchSession(QObject* parent)
    : QObject(parent),
      control_(new ControlClient(this)),
      realtime_(new RealtimeClient),
      realtimeGuard_(realtime_),
      media_(new MediaClient(this)),
      bulk_(new BulkClient(this)) {
  qRegisterMetaType<WorkbenchConnectionState>(
      "pa1664::remote::WorkbenchConnectionState");
  qRegisterMetaType<MediaStreamKind>("pa1664::remote::MediaStreamKind");
  qRegisterMetaType<BulkStreamKind>("pa1664::remote::BulkStreamKind");

  // The operator-side motion/heartbeat clock must remain independent of GUI
  // repaint and media decode work.  The client owns no UI and is driven by its
  // own precise timer in this thread.
  realtimeThread_ = new QThread(this);
  realtimeThread_->setObjectName(QStringLiteral("WorkbenchRealtimeUdp"));
  realtime_->moveToThread(realtimeThread_);
  connect(realtimeThread_, &QThread::finished, realtime_,
          &QObject::deleteLater);
  realtimeThread_->start(QThread::HighPriority);

  connect(control_, &ControlClient::connectionStateChanged, this,
          &RemoteWorkbenchSession::onControlStateChanged);
  connect(control_, &ControlClient::sessionReady, this,
          &RemoteWorkbenchSession::onSessionReady);
  connect(control_, &ControlClient::responseReceived, this,
          &RemoteWorkbenchSession::onControlResponse);
  connect(control_, &ControlClient::requestTimedOut, this,
          &RemoteWorkbenchSession::onRequestTimedOut);
  connect(control_, &ControlClient::eventReceived, this,
          &RemoteWorkbenchSession::eventReceived);
  connect(control_, &ControlClient::errorOccurred, this,
          [this](const QString& detail, bool recoverable) {
            if (!recoverable) emit protocolError(detail);
          });
  connect(realtime_, &RealtimeClient::telemetryReceived, this,
          &RemoteWorkbenchSession::telemetryReceived);
  connect(realtime_, &RealtimeClient::errorOccurred, this,
          &RemoteWorkbenchSession::protocolError);
  connect(realtime_, &RealtimeClient::acknowledgeReceived, this,
          [this](const QJsonObject& ack) {
            const QString event = ack.value(QStringLiteral("event")).toString();
            if (!event.isEmpty()) emit eventReceived(event, ack);
          });
  connect(media_, &MediaClient::messageReceived, this,
          &RemoteWorkbenchSession::mediaMessageReceived);
  connect(media_, &MediaClient::errorOccurred, this,
          &RemoteWorkbenchSession::protocolError);
  connect(bulk_, &BulkClient::messageReceived, this,
          &RemoteWorkbenchSession::bulkMessageReceived);
  connect(bulk_, &BulkClient::errorOccurred, this,
          &RemoteWorkbenchSession::protocolError);
  connect(bulk_, &BulkClient::connected, this,
          [this] { emit bulkReadyChanged(true); });
  connect(bulk_, &BulkClient::disconnected, this,
          [this](const QString&) { emit bulkReadyChanged(false); });
}

RemoteWorkbenchSession::~RemoteWorkbenchSession() {
  stop();

  if (!realtimeThread_) {
    if (realtimeGuard_) delete realtimeGuard_.data();
    realtime_ = nullptr;
    return;
  }

  if (realtimeThread_->isRunning()) {
    realtimeThread_->quit();
    if (!realtimeThread_->wait(5000)) {
      qCritical() << "WORKBENCH_REALTIME_THREAD_SHUTDOWN_TIMEOUT";
      // This is a shutdown-only last resort.  Keeping a running QThread as
      // the child of a dying session would abort the process later.
      realtimeThread_->terminate();
      realtimeThread_->wait();
    }
  }

  // The finished -> deleteLater connection normally clears this guard.  If
  // shutdown had to be forced, delete the worker only after its thread has
  // stopped, and never destroy a QThread while it is still running.
  if (realtimeGuard_) delete realtimeGuard_.data();
  realtime_ = nullptr;
}

quint64 RemoteWorkbenchSession::sessionId() const {
  return control_ ? control_->sessionId() : 0;
}

QStringList RemoteWorkbenchSession::capabilities() const {
  return capabilities_;
}

bool RemoteWorkbenchSession::hasCapability(const QString& capability) const {
  return capabilities_.contains(capability);
}

bool RemoteWorkbenchSession::mediaReady() const {
  return media_ && media_->isActive();
}

bool RemoteWorkbenchSession::bulkReady() const {
  return bulk_ && bulk_->isConnected();
}

void RemoteWorkbenchSession::start(const QString& host, quint16 controlPort) {
  host_ = host.trimmed();
  started_ = true;
  pendingOperations_.clear();
  capabilities_.clear();
  stopRealtimeAsync();
  media_->stop();
  bulk_->stop();
  emit mediaReadyChanged(false);
  emit bulkReadyChanged(false);
  if (host_.isEmpty()) {
    setState(WorkbenchConnectionState::Offline,
             QStringLiteral("未配置小车主机地址"));
    emit protocolError(QStringLiteral("未配置远程主机地址"));
    return;
  }
  setState(WorkbenchConnectionState::Connecting,
           QStringLiteral("连接小车主机 %1:%2").arg(host_).arg(controlPort));
  control_->start(host_, controlPort);
}

void RemoteWorkbenchSession::stop() {
  started_ = false;
  stopRealtimeAsync();
  media_->stop();
  bulk_->stop();
  control_->stop();
  emit mediaReadyChanged(false);
  emit bulkReadyChanged(false);
  pendingOperations_.clear();
  capabilities_.clear();
  setState(WorkbenchConnectionState::Offline, QStringLiteral("已断开"));
}

quint64 RemoteWorkbenchSession::request(const QString& operation,
                                        const QJsonObject& data) {
  if (!control_ || !control_->isReady()) {
    emit responseReceived(operation, 0, false, {},
                          QStringLiteral("not_connected"),
                          QStringLiteral("小车主机尚未连接"));
    return 0;
  }
  const quint64 requestId = control_->sendRequest(operation, data);
  if (requestId == 0) {
    emit responseReceived(operation, 0, false, {},
                          QStringLiteral("request_rejected"),
                          QStringLiteral("控制请求被拒绝或发送缓冲已满"));
    return 0;
  }
  pendingOperations_.insert(requestId, operation);
  return requestId;
}

void RemoteWorkbenchSession::setMotion(double linearMps, double angularRadps,
                                       quint16 leaseMs) {
  if (!realtime_ || !realtimeThread_ || !realtimeThread_->isRunning()) return;
  QMetaObject::invokeMethod(
      realtime_,
      [client = realtime_, linearMps, angularRadps, leaseMs] {
        client->setMotionSetpoint(linearMps, angularRadps, leaseMs);
      },
      Qt::QueuedConnection);
}

void RemoteWorkbenchSession::clearMotion() {
  if (!realtime_ || !realtimeThread_ || !realtimeThread_->isRunning()) return;
  QMetaObject::invokeMethod(
      realtime_, [client = realtime_] { client->clearMotionSetpoint(); },
      Qt::QueuedConnection);
}

void RemoteWorkbenchSession::emergencyStop() {
  if (realtime_ && realtimeThread_ && realtimeThread_->isRunning()) {
    QMetaObject::invokeMethod(
        realtime_, [client = realtime_] { client->emergencyStop(); },
        Qt::QueuedConnection);
  }
  request(QStringLiteral("robot.emergencyStop"),
          QJsonObject{{QStringLiteral("latched"), true}});
}

quint64 RemoteWorkbenchSession::sendBulk(BulkStreamKind kind,
                                         const QByteArray& payload) {
  if (!bulk_ || !bulk_->isConnected()) return 0;
  return bulk_->send(kind, payload);
}

void RemoteWorkbenchSession::onControlStateChanged(ConnectionState state,
                                                   const QString& detail) {
  switch (state) {
    case ConnectionState::Disconnected:
      stopRealtimeAsync();
      media_->stop();
      bulk_->stop();
      emit mediaReadyChanged(false);
      emit bulkReadyChanged(false);
      capabilities_.clear();
      setState(WorkbenchConnectionState::Offline, detail);
      if (started_) emit offline(detail);
      break;
    case ConnectionState::Connecting:
    case ConnectionState::Handshaking:
      stopRealtimeAsync();
      media_->stop();
      bulk_->stop();
      emit mediaReadyChanged(false);
      emit bulkReadyChanged(false);
      setState(WorkbenchConnectionState::Connecting, detail);
      break;
    case ConnectionState::Reconnecting:
      stopRealtimeAsync();
      media_->stop();
      bulk_->stop();
      emit mediaReadyChanged(false);
      emit bulkReadyChanged(false);
      capabilities_.clear();
      setState(WorkbenchConnectionState::Degraded, detail);
      emit offline(detail);
      break;
    case ConnectionState::Ready:
      break;
  }
}

void RemoteWorkbenchSession::onSessionReady(
    quint64, const QByteArray& sessionKey, quint16 realtimePort,
    quint16 mediaPort, quint16 bulkPort, int) {
  realtimePort_ = realtimePort == 0 ? kDefaultRealtimePort : realtimePort;
  mediaPort_ = mediaPort == 0 ? kDefaultMediaPort : mediaPort;
  bulkPort_ = bulkPort == 0 ? kDefaultBulkPort : bulkPort;
  capabilities_ = control_->capabilities();
  if (realtime_ && realtimeThread_ && realtimeThread_->isRunning()) {
    const QString host = host_;
    const quint64 sessionId = control_->sessionId();
    const quint16 port = realtimePort_;
    QMetaObject::invokeMethod(
        realtime_,
        [client = realtime_, host, port, sessionId, sessionKey] {
          client->start(host, port, sessionId, sessionKey);
        },
        Qt::QueuedConnection);
  }
  media_->start(host_, mediaPort_, control_->sessionId(), sessionKey);
  bulk_->start(host_, bulkPort_, control_->sessionId(), sessionKey);
  if (media_->isActive()) emit mediaReadyChanged(true);
  setState(WorkbenchConnectionState::Online,
           QStringLiteral("会话 %1 在线，实时 %2 / 媒体 %3 / 批量 %4")
               .arg(control_->sessionId())
               .arg(realtimePort_)
               .arg(mediaPort_)
               .arg(bulkPort_));
  emit online();
}

void RemoteWorkbenchSession::onControlResponse(quint64 requestId,
                                               const QJsonObject& response) {
  const QString operation = pendingOperations_.take(requestId);
  const bool ok = response.value(QStringLiteral("ok")).toBool(false);
  const QJsonObject data = response.value(QStringLiteral("data")).toObject();
  const QString errorCode =
      response.value(QStringLiteral("errorCode")).toString();
  const QString errorText =
      response.value(QStringLiteral("errorText")).toString();
  emit responseReceived(operation, requestId, ok, data, errorCode, errorText);
}

void RemoteWorkbenchSession::onRequestTimedOut(quint64 requestId,
                                               const QString& operation) {
  const QString tracked = pendingOperations_.take(requestId);
  emit responseReceived(tracked.isEmpty() ? operation : tracked, requestId,
                        false, {}, QStringLiteral("timeout"),
                        QStringLiteral("小车主机响应超时"));
}

void RemoteWorkbenchSession::stopRealtimeAsync() {
  if (!realtime_ || !realtimeThread_ || !realtimeThread_->isRunning()) return;
  QMetaObject::invokeMethod(
      realtime_, [client = realtime_] { client->stop(); },
      Qt::QueuedConnection);
}

void RemoteWorkbenchSession::setState(WorkbenchConnectionState state,
                                      const QString& detail) {
  statusText_ = detail;
  if (state_ == state) return;
  state_ = state;
  emit stateChanged(state_, detail);
}

}  // namespace pa1664::remote
