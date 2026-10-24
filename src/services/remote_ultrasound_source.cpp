#include "remote_ultrasound_source.h"

#include "services/workbench_data_codec.h"

#include <QJsonArray>
#include <QJsonValue>

#include <utility>

namespace pa1664::remote {
namespace {

QJsonObject groupToJson(const GroupScanSettings& group) {
  return QJsonObject{{QStringLiteral("groupId"), group.groupId},
                     {QStringLiteral("groupOffsetWords"),
                      group.groupOffsetWords},
                     {QStringLiteral("beamCount"), group.beamCount},
                     {QStringLiteral("pointCount"), group.pointCount},
                     {QStringLiteral("scanModes"), group.scanModes},
                     {QStringLiteral("selectedBeam"), group.selectedBeam},
                     {QStringLiteral("amplitudeScale"), group.amplitudeScale},
                     {QStringLiteral("encoderPrecision"),
                      group.encoderPrecision},
                     {QStringLiteral("rangeStart"), group.rangeStart},
                     {QStringLiteral("rangeEnd"), group.rangeEnd}};
}

}  // namespace

RemoteUltrasoundSource::RemoteUltrasoundSource(QObject* parent)
    : QObject(parent) {
  qRegisterMetaType<DecodedFrame>("DecodedFrame");
  qRegisterMetaType<GroupScanFrameBatch>("GroupScanFrameBatch");
  qRegisterMetaType<QJsonObject>("QJsonObject");
}

void RemoteUltrasoundSource::bindRemoteSession(
    RemoteWorkbenchSession* session) {
  if (remote_ == session) return;
  if (remote_) disconnect(remote_, nullptr, this, nullptr);
  remote_ = session;
  status_ = {};
  const bool wasReady = ready_;
  ready_ = false;
  if (wasReady) emit readyChanged(false);
  if (!remote_) return;

  connect(remote_, &RemoteWorkbenchSession::stateChanged, this,
          &RemoteUltrasoundSource::onSessionStateChanged);
  connect(remote_, &RemoteWorkbenchSession::responseReceived, this,
          &RemoteUltrasoundSource::onResponseReceived);
  connect(remote_, &RemoteWorkbenchSession::eventReceived, this,
          &RemoteUltrasoundSource::onEventReceived);
  connect(remote_, &RemoteWorkbenchSession::mediaMessageReceived, this,
          &RemoteUltrasoundSource::onMediaMessage);
  connect(remote_, &RemoteWorkbenchSession::bulkMessageReceived, this,
          &RemoteUltrasoundSource::onBulkMessage);
  connect(remote_, &RemoteWorkbenchSession::protocolError, this,
          [this](const QString& detail) {
            emit message(QStringLiteral("远程协议错误：%1").arg(detail), true);
          });
  if (remote_->isReady()) {
    ready_ = true;
    emit readyChanged(true);
    requestStatus();
  }
}

bool RemoteUltrasoundSource::isReady() const {
  return remote_ && remote_->isReady();
}

void RemoteUltrasoundSource::requestStatus() {
  if (!isReady()) return;
  remote_->request(QStringLiteral("ultrasound.status"));
}

void RemoteUltrasoundSource::connectDevice(const QString& address,
                                           int deviceId) {
  if (!isReady()) {
    emit message(QStringLiteral("小车主机尚未连接，无法连接超声设备"), true);
    return;
  }
  remote_->request(QStringLiteral("ultrasound.device.connect"),
                   QJsonObject{{QStringLiteral("address"), address},
                               {QStringLiteral("deviceId"), deviceId}});
}

void RemoteUltrasoundSource::disconnectDevice() {
  if (isReady())
    remote_->request(QStringLiteral("ultrasound.device.disconnect"));
}

void RemoteUltrasoundSource::startAcquisition() {
  if (isReady())
    remote_->request(QStringLiteral("ultrasound.acquisition.start"));
}

void RemoteUltrasoundSource::stopAcquisition() {
  if (isReady())
    remote_->request(QStringLiteral("ultrasound.acquisition.stop"));
}

void RemoteUltrasoundSource::resetEncoder() {
  if (isReady())
    remote_->request(QStringLiteral("ultrasound.encoder.reset"));
}

void RemoteUltrasoundSource::refreshGeometry() {
  if (isReady())
    remote_->request(QStringLiteral("ultrasound.geometry.refresh"));
}

void RemoteUltrasoundSource::updateDecoderSettings(
    QVector<GroupScanSettings> groups, int displayGroupId) {
  if (!isReady() || groups.isEmpty()) return;
  QJsonArray encoded;
  for (const GroupScanSettings& group : qAsConst(groups))
    encoded.append(groupToJson(group));
  remote_->request(
      QStringLiteral("ultrasound.decoder.settings"),
      QJsonObject{{QStringLiteral("groups"), encoded},
                  {QStringLiteral("displayGroupId"), displayGroupId}});
}

void RemoteUltrasoundSource::onSessionStateChanged(
    WorkbenchConnectionState state, const QString& detail) {
  const bool ready = state == WorkbenchConnectionState::Online;
  if (ready_ != ready) {
    ready_ = ready;
    emit readyChanged(ready_);
  }
  if (ready) {
    requestStatus();
  } else {
    status_ = {};
    emit statusChanged(status_);
    if (state == WorkbenchConnectionState::Offline ||
        state == WorkbenchConnectionState::Degraded)
      emit message(QStringLiteral("远程超声链路中断：%1").arg(detail), true);
  }
}

void RemoteUltrasoundSource::onResponseReceived(
    const QString& operation, quint64, bool ok, const QJsonObject& data,
    const QString& errorCode, const QString& errorText) {
  if (!operation.startsWith(QStringLiteral("ultrasound."))) return;
  if (!ok) {
    reportFailure(operation, errorCode, errorText);
    return;
  }
  if (operation == QStringLiteral("ultrasound.status") ||
      operation == QStringLiteral("ultrasound.device.connect") ||
      operation == QStringLiteral("ultrasound.device.disconnect") ||
      operation == QStringLiteral("ultrasound.acquisition.start") ||
      operation == QStringLiteral("ultrasound.acquisition.stop")) {
    if (data.contains(QStringLiteral("device"))) applyStatus(data);
  } else if (operation == QStringLiteral("ultrasound.decoder.settings")) {
    emit decoderSettingsApplied();
  }
}

void RemoteUltrasoundSource::onEventReceived(const QString& eventName,
                                             const QJsonObject& data) {

  if (eventName == QStringLiteral("ultrasound.parameters.invalidated")) {
    emit parametersInvalidated(status_);
    return;
  }
  if (eventName == QStringLiteral("ultrasound.backlogOverflow")) {
    const qint64 bytes = static_cast<qint64>(
        data.value(QStringLiteral("bytes")).toDouble());
    const QString text = data.value(QStringLiteral("message"))
                             .toString(QStringLiteral("扫描积压超限"));
    emit backlogOverflow(bytes, text);
    emit message(text, true);
    requestStatus();
    return;
  }
  if (eventName == QStringLiteral("ultrasound.bulkOverflow")) {
    const qint64 bytes = static_cast<qint64>(
        data.value(QStringLiteral("pendingBytes")).toDouble());
    const QString text = data.value(QStringLiteral("message"))
                             .toString(QStringLiteral("可靠批量队列已满"));
    emit bulkOverflow(bytes, text);
    emit message(text, true);
    requestStatus();
  }
}

void RemoteUltrasoundSource::onMediaMessage(
    quint16 streamId, MediaStreamKind kind, quint64, quint64,
    const QByteArray& payload) {
  if (kind != MediaStreamKind::UltrasoundFrame ||
      streamId < kMediaStreamUltrasoundLiveBase ||
      streamId >= kMediaStreamUltrasoundLiveBase + 256)
    return;
  DecodedFrame frame;
  QString error;
  if (!decodeDecodedFrame(payload, &frame, &error)) {
    emit message(QStringLiteral("远程超声帧解码失败：%1").arg(error), true);
    return;
  }
  emit frameDecoded(std::move(frame),
                    int(streamId - kMediaStreamUltrasoundLiveBase), 0);
}

void RemoteUltrasoundSource::onBulkMessage(BulkStreamKind kind, quint64,
                                           const QByteArray& payload) {
  if (kind != BulkStreamKind::UltrasoundScanBatch) return;
  GroupScanFrameBatch batch;
  QString error;
  if (!decodeGroupScanBatch(payload, &batch, &error)) {
    emit message(QStringLiteral("远程扫描批次解码失败：%1").arg(error), true);
    return;
  }
  if (!batch.isEmpty()) emit groupScanBatchDecoded(std::move(batch), 0);
}

void RemoteUltrasoundSource::applyStatus(const QJsonObject& status) {
  status_ = status;
  emit statusChanged(status_);
}

void RemoteUltrasoundSource::reportFailure(const QString& operation,
                                           const QString& errorCode,
                                           const QString& errorText) {
  emit message(QStringLiteral("%1 失败 [%2]：%3")
                   .arg(operation, errorCode, errorText),
               true);
}

}  // namespace pa1664::remote
