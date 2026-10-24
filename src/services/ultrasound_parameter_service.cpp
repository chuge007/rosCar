#include "ultrasound_parameter_service.h"

#include "services/parameter_key.h"
#include "services/remote_workbench_session.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonValue>
#include <QSet>
#include <QTimer>

namespace pa1664::remote {

UltrasoundParameterService& UltrasoundParameterService::instance() {
  static UltrasoundParameterService service;
  return service;
}

UltrasoundParameterService::UltrasoundParameterService(QObject* parent)
    : QObject(parent),
      getTimer_(new QTimer(this)),
      setTimer_(new QTimer(this)),
      changedTimer_(new QTimer(this)) {
  getTimer_->setSingleShot(true);
  getTimer_->setInterval(8);
  setTimer_->setSingleShot(true);
  setTimer_->setInterval(8);
  changedTimer_->setSingleShot(true);
  changedTimer_->setInterval(35);
  connect(getTimer_, &QTimer::timeout, this, &UltrasoundParameterService::flushPendingGets);
  connect(setTimer_, &QTimer::timeout, this, &UltrasoundParameterService::flushPendingSets);
  connect(changedTimer_, &QTimer::timeout, this, &UltrasoundParameterService::parametersChanged);
}

void UltrasoundParameterService::bindRemoteSession(RemoteWorkbenchSession* session) {
  if (remote_ == session) return;
  if (remote_) disconnect(remote_, nullptr, this, nullptr);
  remote_ = session;
  values_.clear();
  pendingGets_.clear();
  pendingSets_.clear();
  pendingSetOrder_.clear();
  snapshotRequested_ = false;
  presets_.clear();
  currentPresetId_ = 0;
  presetRequests_.clear();
  presetRequestQueue_.clear();
  activePresetRequestId_ = 0;
  activePresetOperation_.clear();
  activePresetId_ = -1;
  activeExportPath_.clear();
  scanPrecisionRequests_.clear();
  encoderCalibrationRequests_.clear();
  applyCommitPending_ = false;
  pendingApplyBatches_ = 0;
  applyFailureCount_ = 0;
  applyErrors_.clear();
  commitRequested_ = false;
  if (!remote_) return;
  connect(remote_, &RemoteWorkbenchSession::online, this, [this] {
    requestSnapshot();
    refreshPresets();
    requestEncoderCalibration();
  });
  connect(remote_, &RemoteWorkbenchSession::offline, this, [this](const QString& reason) {
    values_.clear();
    pendingGets_.clear();
    pendingSets_.clear();
    pendingSetOrder_.clear();
    presetRequests_.clear();
    scanPrecisionRequests_.clear();
    encoderCalibrationRequests_.clear();
    const QString presetFailure =
        QStringLiteral("与小车主机的控制通道已断开：%1").arg(reason);
    if (!activePresetOperation_.isEmpty()) {
      emit remotePresetOperationFinished(activePresetOperation_, false,
                                         activePresetId_, presetFailure);
    }
    // Every queued UI operation needs a terminal response. Otherwise the
    // preset controls stay busy until the application exits.
    while (!presetRequestQueue_.isEmpty()) {
      const PresetRequest queued = presetRequestQueue_.dequeue();
      emit remotePresetOperationFinished(queued.operation, false,
                                         queued.presetId, presetFailure);
    }
    activePresetRequestId_ = 0;
    activePresetOperation_.clear();
    activePresetId_ = -1;
    activeExportPath_.clear();
    if (applyCommitPending_)
      finishRemoteApply(false,
                        QStringLiteral("与小车主机的控制通道已断开：%1").arg(reason));
    applyCommitPending_ = false;
    pendingApplyBatches_ = 0;
    applyFailureCount_ = 0;
    applyErrors_.clear();
    commitRequested_ = false;
    snapshotRequested_ = false;
    emit message(QStringLiteral("超声参数缓存已失效：%1").arg(reason), false);
    scheduleChanged();
  });
  connect(remote_, &RemoteWorkbenchSession::responseReceived, this,
          &UltrasoundParameterService::onResponse);
  if (remote_->isReady()) {
    requestSnapshot();
    refreshPresets();
    requestEncoderCalibration();
  }
}

bool UltrasoundParameterService::isRemote() const { return !remote_.isNull(); }

void UltrasoundParameterService::requestSnapshot() {
  if (!remote_ || !remote_->isReady()) return;
  snapshotRequested_ = true;
  remote_->request(QStringLiteral("ultrasound.parameters.snapshot"));
}

void UltrasoundParameterService::refreshPresets() {
  if (!remote_ || !remote_->isReady()) return;
  remote_->request(QStringLiteral("ultrasound.presets.list"));
}

void UltrasoundParameterService::sendPresetOperation(const QString& operation,
                                                      const QJsonObject& data,
                                                      int presetId,
                                                      const QString& exportPath) {
  if (!remote_ || !remote_->isReady()) {
    emit remotePresetOperationFinished(
        operation, false, presetId,
        QStringLiteral("小车主机尚未连接，参数预设操作未发送"));
    return;
  }
  // Preset mutations are ordered.  A second UI click must not overwrite the
  // response context of an operation that is still running on the host.
  presetRequestQueue_.enqueue({operation, data, presetId, exportPath});
  dispatchNextPresetOperation();
}

void UltrasoundParameterService::dispatchNextPresetOperation() {
  if (activePresetRequestId_ != 0 || presetRequestQueue_.isEmpty() ||
      !remote_ || !remote_->isReady())
    return;
  const PresetRequest request = presetRequestQueue_.dequeue();
  const quint64 requestId = remote_->request(request.operation, request.data);
  if (requestId == 0) {
    emit remotePresetOperationFinished(
        request.operation, false, request.presetId,
        QStringLiteral("参数预设请求发送失败"));
    QTimer::singleShot(0, this,
                       &UltrasoundParameterService::dispatchNextPresetOperation);
    return;
  }
  activePresetRequestId_ = requestId;
  activePresetOperation_ = request.operation;
  activePresetId_ = request.presetId;
  activeExportPath_ = request.exportPath;
  presetRequests_.insert(requestId, request.operation);
}

void UltrasoundParameterService::capturePreset(int presetId) {
  sendPresetOperation(QStringLiteral("ultrasound.presets.capture"),
                      QJsonObject{{QStringLiteral("presetId"), presetId}},
                      presetId);
}

void UltrasoundParameterService::switchPreset(int presetId) {
  sendPresetOperation(QStringLiteral("ultrasound.presets.switch"),
                      QJsonObject{{QStringLiteral("presetId"), presetId}},
                      presetId);
}

void UltrasoundParameterService::copyPreset(int sourcePresetId) {
  sendPresetOperation(QStringLiteral("ultrasound.presets.copy"),
                      QJsonObject{{QStringLiteral("sourcePresetId"),
                                   sourcePresetId}},
                      sourcePresetId);
}

void UltrasoundParameterService::removePreset(int presetId) {
  sendPresetOperation(QStringLiteral("ultrasound.presets.remove"),
                      QJsonObject{{QStringLiteral("presetId"), presetId}},
                      presetId);
}

void UltrasoundParameterService::importPresetFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    emit remotePresetOperationFinished(
        QStringLiteral("ultrasound.presets.import"), false, -1,
        QStringLiteral("无法读取导入配置：%1").arg(file.errorString()));
    return;
  }
  const QByteArray bytes = file.readAll();
  if (bytes.size() > kMaxPresetImportBytes) {
    emit remotePresetOperationFinished(
        QStringLiteral("ultrasound.presets.import"), false, -1,
        QStringLiteral("远程预设文件不能超过 %1 KiB；Base64 编码后会超过控制帧上限，"
                       "请拆分或精简预设后再导入")
            .arg(kMaxPresetImportBytes / 1024));
    return;
  }
  const QJsonObject config = QJsonDocument::fromJson(bytes).object();
  if (config.isEmpty()) {
    emit remotePresetOperationFinished(
        QStringLiteral("ultrasound.presets.import"), false, -1,
        QStringLiteral("导入文件不是有效 JSON 配置"));
    return;
  }
  sendPresetOperation(
      QStringLiteral("ultrasound.presets.import"),
      QJsonObject{{QStringLiteral("configBase64"),
                   QString::fromLatin1(bytes.toBase64())}});
}

void UltrasoundParameterService::exportPresetFile(const QString& path) {
  if (path.isEmpty()) return;
  sendPresetOperation(QStringLiteral("ultrasound.presets.export"), {},
                      -1, path);
}

void UltrasoundParameterService::saveScanPrecisionMmPerCell(int presetId,
                                                             double mmPerCell) {
  if (!remote_ || !remote_->isReady()) {
    emit message(QStringLiteral("小车主机尚未连接，扫查精度未保存"), true);
    return;
  }
  const quint64 requestId = remote_->request(
      QStringLiteral("ultrasound.scan.precision.set"),
      QJsonObject{{QStringLiteral("presetId"), presetId},
                  {QStringLiteral("mmPerCell"), mmPerCell}});
  if (requestId == 0) {
    emit message(QStringLiteral("扫查精度保存请求发送失败"), true);
    return;
  }
  scanPrecisionRequests_.insert(requestId, qMakePair(presetId, mmPerCell));
}

void UltrasoundParameterService::requestScanPrecision(int presetId) {
  if (!remote_ || !remote_->isReady() || presetId < 0) return;
  remote_->request(QStringLiteral("ultrasound.scan.precision.get"),
                   QJsonObject{{QStringLiteral("presetId"), presetId}});
}

void UltrasoundParameterService::saveEncoderCalibration(double wheelDiameterMm,
                                                        int resolution) {
  if (!remote_ || !remote_->isReady()) {
    emit message(QStringLiteral("小车主机尚未连接，编码器标定未保存"), true);
    return;
  }
  const quint64 requestId = remote_->request(
      QStringLiteral("ultrasound.encoder.calibration.set"),
      QJsonObject{{QStringLiteral("wheelDiameterMm"), wheelDiameterMm},
                  {QStringLiteral("resolution"), resolution}});
  if (requestId == 0) {
    emit message(QStringLiteral("编码器标定保存请求发送失败"), true);
    return;
  }
  encoderCalibrationRequests_.insert(
      requestId, qMakePair(wheelDiameterMm, resolution));
}

void UltrasoundParameterService::requestEncoderCalibration() {
  if (!remote_ || !remote_->isReady()) return;
  remote_->request(QStringLiteral("ultrasound.encoder.calibration.get"));
}

void UltrasoundParameterService::commitPendingChanges(int presetId) {
  if (!remote_ || !remote_->isReady()) {
    emit remoteApplyFinished(false, QStringLiteral("小车主机尚未连接"));
    return;
  }
  applyPresetId_ = presetId;
  applyCommitPending_ = true;
  applyFailureCount_ = 0;
  applyErrors_.clear();
  commitRequested_ = false;
  setTimer_->stop();
  flushPendingSets();
  if (pendingSetOrder_.isEmpty() && pendingApplyBatches_ == 0 &&
      !commitRequested_) {
    commitRequested_ = true;
    const quint64 id = remote_->request(
        QStringLiteral("ultrasound.parameters.commit"),
        QJsonObject{{QStringLiteral("presetId"), applyPresetId_}});
    if (id == 0)
      finishRemoteApply(false,
                        QStringLiteral("参数提交请求发送失败"));
  }
}

void UltrasoundParameterService::applyPresetState(const QJsonObject& state,
                                                   bool emitSnapshot) {
  const QJsonArray encoded = state.value(QStringLiteral("presets")).toArray();
  QList<ParameterPresetInfo> presets;
  for (const QJsonValue& value : encoded) {
    const QJsonObject item = value.toObject();
    if (item.value(QStringLiteral("id")).toInt(-1) < 0) continue;
    presets.append(ParameterPresetInfo{
        item.value(QStringLiteral("id")).toInt(),
        item.value(QStringLiteral("name")).toString()});
  }
  if (!presets.isEmpty()) presets_ = presets;
  currentPresetId_ =
      state.value(QStringLiteral("currentPreset")).toInt(currentPresetId_);
  if (state.contains(QStringLiteral("scanPrecisionMmPerCell"))) {
    emit scanPrecisionChanged(
        currentPresetId_,
        state.value(QStringLiteral("scanPrecisionMmPerCell")).toDouble());
  }
  if (state.contains(QStringLiteral("wheelDiameterMm")) &&
      state.contains(QStringLiteral("encoderResolution"))) {
    emit encoderCalibrationReady(
        state.value(QStringLiteral("wheelDiameterMm")).toDouble(500.0),
        state.value(QStringLiteral("encoderResolution")).toInt(4096));
  }
  if (emitSnapshot) {
    const QByteArray config = QByteArray::fromBase64(
        state.value(QStringLiteral("configBase64")).toString().toLatin1());
    if (!config.isEmpty()) {
      QJsonParseError parseError;
      const QJsonDocument document = QJsonDocument::fromJson(config, &parseError);
      if (parseError.error == QJsonParseError::NoError && document.isObject())
        emit hardwareSnapshotReady(document);
    }
  }
  emit presetsChanged();
}

void UltrasoundParameterService::finishRemoteApply(bool ok,
                                                   const QString& message) {
  applyCommitPending_ = false;
  commitRequested_ = false;
  pendingApplyBatches_ = 0;
  QString text = message;
  if (applyFailureCount_ > 0 && !applyErrors_.isEmpty())
    text += QStringLiteral("；") + applyErrors_.join(QStringLiteral("；"));
  const bool finalOk = ok && applyFailureCount_ == 0;
  emit remoteApplyFinished(finalOk, text);
  applyFailureCount_ = 0;
  applyErrors_.clear();
  applyPresetId_ = -1;
}

QVariant UltrasoundParameterService::cachedValue(const QString& method,
                                                      const QJsonArray& args,
                                                      const QVariant& fallback) {
  const QString key = parameterCacheKey(method, args);
  const auto it = values_.constFind(key);
  if (it != values_.constEnd()) return it.value();
  enqueueGet(method, args);
  return fallback;
}

void UltrasoundParameterService::enqueueGet(const QString& method, const QJsonArray& args) {
  if (!remote_ || !remote_->isReady()) return;
  for (const GetRequest& request : qAsConst(pendingGets_)) {
    if (request.method == method && request.args == args) return;
  }
  pendingGets_.push_back({method, args});
  if (!getTimer_->isActive()) getTimer_->start();
}

void UltrasoundParameterService::flushPendingGets() {
  if (!remote_ || !remote_->isReady() || pendingGets_.isEmpty()) return;
  QJsonArray requests;
  const int count = qMin(256, pendingGets_.size());
  for (int i = 0; i < count; ++i) {
    const GetRequest& request = pendingGets_.at(i);
    requests.append(QJsonObject{{QStringLiteral("method"), request.method},
                                {QStringLiteral("args"), request.args}});
  }
  pendingGets_.erase(pendingGets_.begin(), pendingGets_.begin() + count);
  remote_->request(QStringLiteral("ultrasound.parameters.getMany"),
                   QJsonObject{{QStringLiteral("requests"), requests}});
  if (!pendingGets_.isEmpty()) getTimer_->start();
}

bool UltrasoundParameterService::sendSet(const QString& method,
                                           const QJsonArray& args,
                                           const QString& readbackMethod,
                                           const QJsonArray& readbackArgs) {
  if (!remote_ || !remote_->isReady()) return false;
  const QString key = parameterCacheKey(readbackMethod, readbackArgs);
  if (!pendingSets_.contains(key)) pendingSetOrder_.push_back(key);
  pendingSets_.insert(
      key, SetRequest{method, args, readbackMethod, readbackArgs, key});
  if (!setTimer_->isActive()) setTimer_->start();
  return true;
}

void UltrasoundParameterService::flushPendingSets() {
  if (!remote_ || !remote_->isReady() || pendingSetOrder_.isEmpty()) return;
  QJsonArray changes;
  const int count = qMin(128, pendingSetOrder_.size());
  QSet<QString> sentKeys;
  for (int i = 0; i < count; ++i) {
    const QString key = pendingSetOrder_.at(i);
    const auto it = pendingSets_.constFind(key);
    if (it == pendingSets_.constEnd() || sentKeys.contains(key)) continue;
    const SetRequest& request = it.value();
    changes.append(QJsonObject{
        {QStringLiteral("method"), request.method},
        {QStringLiteral("args"), request.args},
        {QStringLiteral("readbackMethod"), request.readbackMethod},
        {QStringLiteral("readbackArgs"), request.readbackArgs}});
    sentKeys.insert(key);
  }
  pendingSetOrder_.erase(pendingSetOrder_.begin(),
                         pendingSetOrder_.begin() + count);
  for (const QString& key : qAsConst(sentKeys)) pendingSets_.remove(key);
  if (!changes.isEmpty()) {
    const quint64 id =
        remote_->request(QStringLiteral("ultrasound.parameters.apply"),
                         QJsonObject{{QStringLiteral("changes"), changes}});
    if (id != 0) {
      ++pendingApplyBatches_;
    } else {
      ++applyFailureCount_;
      applyErrors_ << QStringLiteral("参数批次发送失败");
    }
  }
  if (!pendingSetOrder_.isEmpty()) {
    setTimer_->start();
  } else if (applyCommitPending_ && pendingApplyBatches_ == 0) {
    commitRequested_ = true;
    const quint64 id = remote_->request(
        QStringLiteral("ultrasound.parameters.commit"),
        QJsonObject{{QStringLiteral("presetId"), applyPresetId_}});
    if (id == 0)
      finishRemoteApply(false, QStringLiteral("参数提交请求发送失败"));
  }
}

void UltrasoundParameterService::scheduleChanged() {
  if (!changedTimer_->isActive()) changedTimer_->start();
}

double UltrasoundParameterService::getBeamAngleLinear(int groupId) {
  if (!remote_) return Client::getInstance().getBeamAngleLinear(groupId);
  return cachedValue(QStringLiteral("getBeamAngleLinear"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getBeamAngleMax(int groupId) {
  if (!remote_) return Client::getInstance().getBeamAngleMax(groupId);
  return cachedValue(QStringLiteral("getBeamAngleMax"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getBeamAngleMin(int groupId) {
  if (!remote_) return Client::getInstance().getBeamAngleMin(groupId);
  return cachedValue(QStringLiteral("getBeamAngleMin"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getBeamAngleStep(int groupId) {
  if (!remote_) return Client::getInstance().getBeamAngleStep(groupId);
  return cachedValue(QStringLiteral("getBeamAngleStep"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getBeamAperture(int groupId) {
  if (!remote_) return Client::getInstance().getBeamAperture(groupId);
  return cachedValue(QStringLiteral("getBeamAperture"), QJsonArray{groupId}, 0).toInt();
}

int UltrasoundParameterService::getBeamElementStep(int groupId) {
  if (!remote_) return Client::getInstance().getBeamElementStep(groupId);
  return cachedValue(QStringLiteral("getBeamElementStep"), QJsonArray{groupId}, 0).toInt();
}

int UltrasoundParameterService::getBeamFirstElement(int groupId) {
  if (!remote_) return Client::getInstance().getBeamFirstElement(groupId);
  return cachedValue(QStringLiteral("getBeamFirstElement"), QJsonArray{groupId}, 0).toInt();
}

int UltrasoundParameterService::getBeamLastElement(int groupId) {
  if (!remote_) return Client::getInstance().getBeamLastElement(groupId);
  return cachedValue(QStringLiteral("getBeamLastElement"), QJsonArray{groupId}, 0).toInt();
}

BeamMode UltrasoundParameterService::getBeamMode(int groupId) {
  if (!remote_) return Client::getInstance().getBeamMode(groupId);
  return static_cast<BeamMode>(cachedValue(QStringLiteral("getBeamMode"), QJsonArray{groupId}, 0).toInt());
}

bool UltrasoundParameterService::getDualCrystalEnable(int groupId) {
  if (!remote_) return Client::getInstance().getDualCrystalEnable(groupId);
  return cachedValue(QStringLiteral("getDualCrystalEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getFilterHigh(int groupId) {
  if (!remote_) return Client::getInstance().getFilterHigh(groupId);
  return cachedValue(QStringLiteral("getFilterHigh"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getFilterLow(int groupId) {
  if (!remote_) return Client::getInstance().getFilterLow(groupId);
  return cachedValue(QStringLiteral("getFilterLow"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getFocalPosition(int groupId) {
  if (!remote_) return Client::getInstance().getFocalPosition(groupId);
  return cachedValue(QStringLiteral("getFocalPosition"), QJsonArray{groupId}, 0.0).toDouble();
}

FocalType UltrasoundParameterService::getFocusMode(int groupId) {
  if (!remote_) return Client::getInstance().getFocusMode(groupId);
  return static_cast<FocalType>(cachedValue(QStringLiteral("getFocusMode"), QJsonArray{groupId}, 0).toInt());
}

QVector<int> UltrasoundParameterService::getGroupsNo(bool includeHardware) {
  if (!remote_) return Client::getInstance().getGroupsNo(includeHardware);
  return QVector<int>{0};
}

int UltrasoundParameterService::getBeamCounts(int groupId) {
  if (!remote_) return Client::getInstance().getBeamCounts(groupId);
  const int first = getBeamFirstElement(groupId);
  const int last = getBeamLastElement(groupId);
  const int step = qMax(1, getBeamElementStep(groupId));
  return qMax(1, (last - first) / step + 1);
}

int UltrasoundParameterService::getFrameRate() {
  if (!remote_) return Client::getInstance().getFrameRate();
  return cachedValue(QStringLiteral("getFrameRate"), QJsonArray{}, 0).toInt();
}

double UltrasoundParameterService::getGain(int groupId) {
  if (!remote_) return Client::getInstance().getGain(groupId);
  return cachedValue(QStringLiteral("getGain"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getGateAEnable(int groupId) {
  if (!remote_) return Client::getInstance().getGateAEnable(groupId);
  return cachedValue(QStringLiteral("getGateAEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getGateAEnd(int groupId) {
  if (!remote_) return Client::getInstance().getGateAEnd(groupId);
  return cachedValue(QStringLiteral("getGateAEnd"), QJsonArray{groupId}, 0.0).toDouble();
}

measureType UltrasoundParameterService::getGateAMeasureType(int groupId) {
  if (!remote_) return Client::getInstance().getGateAMeasureType(groupId);
  return static_cast<measureType>(cachedValue(QStringLiteral("getGateAMeasureType"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateAStart(int groupId) {
  if (!remote_) return Client::getInstance().getGateAStart(groupId);
  return cachedValue(QStringLiteral("getGateAStart"), QJsonArray{groupId}, 0.0).toDouble();
}

GateSynchron UltrasoundParameterService::getGateASynchronMode(int groupId) {
  if (!remote_) return Client::getInstance().getGateASynchronMode(groupId);
  return static_cast<GateSynchron>(cachedValue(QStringLiteral("getGateASynchronMode"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateAThreshold(int groupId) {
  if (!remote_) return Client::getInstance().getGateAThreshold(groupId);
  return cachedValue(QStringLiteral("getGateAThreshold"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getGateBEnable(int groupId) {
  if (!remote_) return Client::getInstance().getGateBEnable(groupId);
  return cachedValue(QStringLiteral("getGateBEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getGateBEnd(int groupId) {
  if (!remote_) return Client::getInstance().getGateBEnd(groupId);
  return cachedValue(QStringLiteral("getGateBEnd"), QJsonArray{groupId}, 0.0).toDouble();
}

measureType UltrasoundParameterService::getGateBMeasureType(int groupId) {
  if (!remote_) return Client::getInstance().getGateBMeasureType(groupId);
  return static_cast<measureType>(cachedValue(QStringLiteral("getGateBMeasureType"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateBStart(int groupId) {
  if (!remote_) return Client::getInstance().getGateBStart(groupId);
  return cachedValue(QStringLiteral("getGateBStart"), QJsonArray{groupId}, 0.0).toDouble();
}

GateSynchron UltrasoundParameterService::getGateBSynchronMode(int groupId) {
  if (!remote_) return Client::getInstance().getGateBSynchronMode(groupId);
  return static_cast<GateSynchron>(cachedValue(QStringLiteral("getGateBSynchronMode"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateBThreshold(int groupId) {
  if (!remote_) return Client::getInstance().getGateBThreshold(groupId);
  return cachedValue(QStringLiteral("getGateBThreshold"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getGateCEnable(int groupId) {
  if (!remote_) return Client::getInstance().getGateCEnable(groupId);
  return cachedValue(QStringLiteral("getGateCEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getGateCEnd(int groupId) {
  if (!remote_) return Client::getInstance().getGateCEnd(groupId);
  return cachedValue(QStringLiteral("getGateCEnd"), QJsonArray{groupId}, 0.0).toDouble();
}

measureType UltrasoundParameterService::getGateCMeasureType(int groupId) {
  if (!remote_) return Client::getInstance().getGateCMeasureType(groupId);
  return static_cast<measureType>(cachedValue(QStringLiteral("getGateCMeasureType"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateCStart(int groupId) {
  if (!remote_) return Client::getInstance().getGateCStart(groupId);
  return cachedValue(QStringLiteral("getGateCStart"), QJsonArray{groupId}, 0.0).toDouble();
}

GateSynchron UltrasoundParameterService::getGateCSynchronMode(int groupId) {
  if (!remote_) return Client::getInstance().getGateCSynchronMode(groupId);
  return static_cast<GateSynchron>(cachedValue(QStringLiteral("getGateCSynchronMode"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateCThreshold(int groupId) {
  if (!remote_) return Client::getInstance().getGateCThreshold(groupId);
  return cachedValue(QStringLiteral("getGateCThreshold"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getGateIEnable(int groupId) {
  if (!remote_) return Client::getInstance().getGateIEnable(groupId);
  return cachedValue(QStringLiteral("getGateIEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getGateIEnd(int groupId) {
  if (!remote_) return Client::getInstance().getGateIEnd(groupId);
  return cachedValue(QStringLiteral("getGateIEnd"), QJsonArray{groupId}, 0.0).toDouble();
}

measureType UltrasoundParameterService::getGateIMeasureType(int groupId) {
  if (!remote_) return Client::getInstance().getGateIMeasureType(groupId);
  return static_cast<measureType>(cachedValue(QStringLiteral("getGateIMeasureType"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateIStart(int groupId) {
  if (!remote_) return Client::getInstance().getGateIStart(groupId);
  return cachedValue(QStringLiteral("getGateIStart"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getGateISyncSample(int groupId) {
  if (!remote_) return Client::getInstance().getGateISyncSample(groupId);
  return cachedValue(QStringLiteral("getGateISyncSample"), QJsonArray{groupId}, false).toBool();
}

GateSynchron UltrasoundParameterService::getGateISynchronMode(int groupId) {
  if (!remote_) return Client::getInstance().getGateISynchronMode(groupId);
  return static_cast<GateSynchron>(cachedValue(QStringLiteral("getGateISynchronMode"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getGateIThreshold(int groupId) {
  if (!remote_) return Client::getInstance().getGateIThreshold(groupId);
  return cachedValue(QStringLiteral("getGateIThreshold"), QJsonArray{groupId}, 0.0).toDouble();
}

MaxAmplitude UltrasoundParameterService::getMaxAmplitude(int groupId) {
  if (!remote_) return Client::getInstance().getMaxAmplitude(groupId);
  return static_cast<MaxAmplitude>(cachedValue(QStringLiteral("getMaxAmplitude"), QJsonArray{groupId}, 0).toInt());
}

int UltrasoundParameterService::getPaVoltage() {
  if (!remote_) return Client::getInstance().getPaVoltage();
  return cachedValue(QStringLiteral("getPaVoltage"), QJsonArray{}, 0).toInt();
}

int UltrasoundParameterService::getPointQuantity(int groupId) {
  if (!remote_) return Client::getInstance().getPointQuantity(groupId);
  return cachedValue(QStringLiteral("getPointQuantity"), QJsonArray{groupId}, 0).toInt();
}

double UltrasoundParameterService::getProbeFrequency(int groupId) {
  if (!remote_) return Client::getInstance().getProbeFrequency(groupId);
  return cachedValue(QStringLiteral("getProbeFrequency"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getProbePrimaryElements(int groupId) {
  if (!remote_) return Client::getInstance().getProbePrimaryElements(groupId);
  return cachedValue(QStringLiteral("getProbePrimaryElements"), QJsonArray{groupId}, 0).toInt();
}

double UltrasoundParameterService::getProbePrimaryElementsPitch(int groupId) {
  if (!remote_) return Client::getInstance().getProbePrimaryElementsPitch(groupId);
  return cachedValue(QStringLiteral("getProbePrimaryElementsPitch"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getProbeSecondaryElements(int groupId) {
  if (!remote_) return Client::getInstance().getProbeSecondaryElements(groupId);
  return cachedValue(QStringLiteral("getProbeSecondaryElements"), QJsonArray{groupId}, 0).toInt();
}

double UltrasoundParameterService::getProbeSecondaryElementsPitch(int groupId) {
  if (!remote_) return Client::getInstance().getProbeSecondaryElementsPitch(groupId);
  return cachedValue(QStringLiteral("getProbeSecondaryElementsPitch"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getPulseWidth(int groupId) {
  if (!remote_) return Client::getInstance().getPulseWidth(groupId);
  return cachedValue(QStringLiteral("getPulseWidth"), QJsonArray{groupId}, 0).toInt();
}

double UltrasoundParameterService::getRangeEnd(int groupId) {
  if (!remote_) return Client::getInstance().getRangeEnd(groupId);
  return cachedValue(QStringLiteral("getRangeEnd"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getRangeStart(int groupId) {
  if (!remote_) return Client::getInstance().getRangeStart(groupId);
  return cachedValue(QStringLiteral("getRangeStart"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getReceiverReject(int groupId) {
  if (!remote_) return Client::getInstance().getReceiverReject(groupId);
  return cachedValue(QStringLiteral("getReceiverReject"), QJsonArray{groupId}, 0).toInt();
}

int UltrasoundParameterService::getReceptionChannel(int groupId) {
  if (!remote_) return Client::getInstance().getReceptionChannel(groupId);
  return cachedValue(QStringLiteral("getReceptionChannel"), QJsonArray{groupId}, 0).toInt();
}

RectifierType UltrasoundParameterService::getRectifierMode(int groupId) {
  if (!remote_) return Client::getInstance().getRectifierMode(groupId);
  return static_cast<RectifierType>(cachedValue(QStringLiteral("getRectifierMode"), QJsonArray{groupId}, 0).toInt());
}

bool UltrasoundParameterService::getReverseEnable(int groupId) {
  if (!remote_) return Client::getInstance().getReverseEnable(groupId);
  return cachedValue(QStringLiteral("getReverseEnable"), QJsonArray{groupId}, false).toBool();
}

int UltrasoundParameterService::getScanMode() {
  if (!remote_) return Client::getInstance().getScanMode();
  return cachedValue(QStringLiteral("getScanMode"), QJsonArray{}, 0).toInt();
}

scanType UltrasoundParameterService::getScanType(int groupId) {
  if (!remote_) return Client::getInstance().getScanType(groupId);
  return static_cast<scanType>(cachedValue(QStringLiteral("getScanType"), QJsonArray{groupId}, 0).toInt());
}

ScannerDir UltrasoundParameterService::getScannerDirection() {
  if (!remote_) return Client::getInstance().getScannerDirection();
  return static_cast<ScannerDir>(cachedValue(QStringLiteral("getScannerDirection"), QJsonArray{}, 0).toInt());
}

bool UltrasoundParameterService::getTcgEnable(int groupId) {
  if (!remote_) return Client::getInstance().getTcgEnable(groupId);
  return cachedValue(QStringLiteral("getTcgEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getTcgPointDepth(int index, int beamIndex, int groupId) {
  if (!remote_) return Client::getInstance().getTcgPointDepth(index, beamIndex, groupId);
  return cachedValue(QStringLiteral("getTcgPointDepth"), QJsonArray{index, beamIndex, groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getTcgPointDepthIndex(int index, int groupId) {
  if (!remote_) return Client::getInstance().getTcgPointDepthIndex(index, groupId);
  return cachedValue(QStringLiteral("getTcgPointDepthIndex"), QJsonArray{index, groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getTcgPointGain(int index, int beamIndex, int groupId) {
  if (!remote_) return Client::getInstance().getTcgPointGain(index, beamIndex, groupId);
  return cachedValue(QStringLiteral("getTcgPointGain"), QJsonArray{index, beamIndex, groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getTcgPointGainIndex(int index, int groupId) {
  if (!remote_) return Client::getInstance().getTcgPointGainIndex(index, groupId);
  return cachedValue(QStringLiteral("getTcgPointGainIndex"), QJsonArray{index, groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getTfmRecDelay(int groupId) {
  if (!remote_) return Client::getInstance().getTfmRecDelay(groupId);
  return cachedValue(QStringLiteral("getTfmRecDelay"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getThickness(int groupId) {
  if (!remote_) return Client::getInstance().getThickness(groupId);
  return cachedValue(QStringLiteral("getThickness"), QJsonArray{groupId}, 0.0).toDouble();
}

int UltrasoundParameterService::getTransmissionChannel(int groupId) {
  if (!remote_) return Client::getInstance().getTransmissionChannel(groupId);
  return cachedValue(QStringLiteral("getTransmissionChannel"), QJsonArray{groupId}, 0).toInt();
}

int UltrasoundParameterService::getUtVoltage() {
  if (!remote_) return Client::getInstance().getUtVoltage();
  return cachedValue(QStringLiteral("getUtVoltage"), QJsonArray{}, 0).toInt();
}

VideoFilter UltrasoundParameterService::getVideoFilterMHz(int groupId) {
  if (!remote_) return Client::getInstance().getVideoFilterMHz(groupId);
  return static_cast<VideoFilter>(cachedValue(QStringLiteral("getVideoFilterMHz"), QJsonArray{groupId}, 0).toInt());
}

PolarityType UltrasoundParameterService::getVoltagePolarity(int groupId) {
  if (!remote_) return Client::getInstance().getVoltagePolarity(groupId);
  return static_cast<PolarityType>(cachedValue(QStringLiteral("getVoltagePolarity"), QJsonArray{groupId}, 0).toInt());
}

double UltrasoundParameterService::getWedgeAngle(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeAngle(groupId);
  return cachedValue(QStringLiteral("getWedgeAngle"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::getWedgeEnable(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeEnable(groupId);
  return cachedValue(QStringLiteral("getWedgeEnable"), QJsonArray{groupId}, false).toBool();
}

double UltrasoundParameterService::getWedgeGCp(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeGCp(groupId);
  return cachedValue(QStringLiteral("getWedgeGCp"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeHeight(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeHeight(groupId);
  return cachedValue(QStringLiteral("getWedgeHeight"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeLength(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeLength(groupId);
  return cachedValue(QStringLiteral("getWedgeLength"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeRoofAngle(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeRoofAngle(groupId);
  return cachedValue(QStringLiteral("getWedgeRoofAngle"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeVelocity(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeVelocity(groupId);
  return cachedValue(QStringLiteral("getWedgeVelocity"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeWidth(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeWidth(groupId);
  return cachedValue(QStringLiteral("getWedgeWidth"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeX(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeX(groupId);
  return cachedValue(QStringLiteral("getWedgeX"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWedgeZ(int groupId) {
  if (!remote_) return Client::getInstance().getWedgeZ(groupId);
  return cachedValue(QStringLiteral("getWedgeZ"), QJsonArray{groupId}, 0.0).toDouble();
}

double UltrasoundParameterService::getWorkpieceVelocity(int groupId) {
  if (!remote_) return Client::getInstance().getWorkpieceVelocity(groupId);
  return cachedValue(QStringLiteral("getWorkpieceVelocity"), QJsonArray{groupId}, 0.0).toDouble();
}

bool UltrasoundParameterService::setBeamAngleLinear(double angle, int groupId) {
  if (!remote_) return Client::getInstance().setBeamAngleLinear(angle, groupId);
  return sendSet(QStringLiteral("setBeamAngleLinear"), QJsonArray{angle, groupId}, QStringLiteral("getBeamAngleLinear"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamAngleMax(double BeamAngleMax, int groupId) {
  if (!remote_) return Client::getInstance().setBeamAngleMax(BeamAngleMax, groupId);
  return sendSet(QStringLiteral("setBeamAngleMax"), QJsonArray{BeamAngleMax, groupId}, QStringLiteral("getBeamAngleMax"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamAngleMin(double BeamAngleMin, int groupId) {
  if (!remote_) return Client::getInstance().setBeamAngleMin(BeamAngleMin, groupId);
  return sendSet(QStringLiteral("setBeamAngleMin"), QJsonArray{BeamAngleMin, groupId}, QStringLiteral("getBeamAngleMin"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamAngleStep(double BeamAngleStep, int groupId) {
  if (!remote_) return Client::getInstance().setBeamAngleStep(BeamAngleStep, groupId);
  return sendSet(QStringLiteral("setBeamAngleStep"), QJsonArray{BeamAngleStep, groupId}, QStringLiteral("getBeamAngleStep"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamAperture(int BeamAperture, int groupId) {
  if (!remote_) return Client::getInstance().setBeamAperture(BeamAperture, groupId);
  return sendSet(QStringLiteral("setBeamAperture"), QJsonArray{BeamAperture, groupId}, QStringLiteral("getBeamAperture"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamElementStep(int step, int groupId) {
  if (!remote_) return Client::getInstance().setBeamElementStep(step, groupId);
  return sendSet(QStringLiteral("setBeamElementStep"), QJsonArray{step, groupId}, QStringLiteral("getBeamElementStep"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamFirstElement(int firstElement, int groupId) {
  if (!remote_) return Client::getInstance().setBeamFirstElement(firstElement, groupId);
  return sendSet(QStringLiteral("setBeamFirstElement"), QJsonArray{firstElement, groupId}, QStringLiteral("getBeamFirstElement"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamLastElement(int lastElement, int groupId) {
  if (!remote_) return Client::getInstance().setBeamLastElement(lastElement, groupId);
  return sendSet(QStringLiteral("setBeamLastElement"), QJsonArray{lastElement, groupId}, QStringLiteral("getBeamLastElement"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setBeamMode(BeamMode mode, int groupId) {
  if (!remote_) return Client::getInstance().setBeamMode(mode, groupId);
  return sendSet(QStringLiteral("setBeamMode"), QJsonArray{static_cast<int>(mode), groupId}, QStringLiteral("getBeamMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setDualCrystalEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setDualCrystalEnable(enable, groupId);
  return sendSet(QStringLiteral("setDualCrystalEnable"), QJsonArray{enable, groupId}, QStringLiteral("getDualCrystalEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setFilterHigh(double freq, int groupId) {
  if (!remote_) return Client::getInstance().setFilterHigh(freq, groupId);
  return sendSet(QStringLiteral("setFilterHigh"), QJsonArray{freq, groupId}, QStringLiteral("getFilterHigh"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setFilterLow(double freq, int groupId) {
  if (!remote_) return Client::getInstance().setFilterLow(freq, groupId);
  return sendSet(QStringLiteral("setFilterLow"), QJsonArray{freq, groupId}, QStringLiteral("getFilterLow"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setFocalPosition(double position, int groupId) {
  if (!remote_) return Client::getInstance().setFocalPosition(position, groupId);
  return sendSet(QStringLiteral("setFocalPosition"), QJsonArray{position, groupId}, QStringLiteral("getFocalPosition"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setFocusMode(FocalType mode, int groupId) {
  if (!remote_) return Client::getInstance().setFocusMode(mode, groupId);
  return sendSet(QStringLiteral("setFocusMode"), QJsonArray{static_cast<int>(mode), groupId}, QStringLiteral("getFocusMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setFrameRate(int rate) {
  if (!remote_) return Client::getInstance().setFrameRate(rate);
  return sendSet(QStringLiteral("setFrameRate"), QJsonArray{rate}, QStringLiteral("getFrameRate"), QJsonArray{});
}

bool UltrasoundParameterService::setGain(double gain, int groupId) {
  if (!remote_) return Client::getInstance().setGain(gain, groupId);
  return sendSet(QStringLiteral("setGain"), QJsonArray{gain, groupId}, QStringLiteral("getGain"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateAEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setGateAEnable(enable, groupId);
  return sendSet(QStringLiteral("setGateAEnable"), QJsonArray{enable, groupId}, QStringLiteral("getGateAEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateAEnd(double end, int groupId) {
  if (!remote_) return Client::getInstance().setGateAEnd(end, groupId);
  return sendSet(QStringLiteral("setGateAEnd"), QJsonArray{end, groupId}, QStringLiteral("getGateAEnd"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateAMeasureType(measureType measureType, int groupId) {
  if (!remote_) return Client::getInstance().setGateAMeasureType(measureType, groupId);
  return sendSet(QStringLiteral("setGateAMeasureType"), QJsonArray{static_cast<int>(measureType), groupId}, QStringLiteral("getGateAMeasureType"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateAStart(double start, int groupId) {
  if (!remote_) return Client::getInstance().setGateAStart(start, groupId);
  return sendSet(QStringLiteral("setGateAStart"), QJsonArray{start, groupId}, QStringLiteral("getGateAStart"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateASynchronMode(GateSynchron synchro, int groupId) {
  if (!remote_) return Client::getInstance().setGateASynchronMode(synchro, groupId);
  return sendSet(QStringLiteral("setGateASynchronMode"), QJsonArray{static_cast<int>(synchro), groupId}, QStringLiteral("getGateASynchronMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateAThreshold(double threshold, int groupId) {
  if (!remote_) return Client::getInstance().setGateAThreshold(threshold, groupId);
  return sendSet(QStringLiteral("setGateAThreshold"), QJsonArray{threshold, groupId}, QStringLiteral("getGateAThreshold"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setGateBEnable(enable, groupId);
  return sendSet(QStringLiteral("setGateBEnable"), QJsonArray{enable, groupId}, QStringLiteral("getGateBEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBEnd(double end, int groupId) {
  if (!remote_) return Client::getInstance().setGateBEnd(end, groupId);
  return sendSet(QStringLiteral("setGateBEnd"), QJsonArray{end, groupId}, QStringLiteral("getGateBEnd"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBMeasureType(measureType measureType, int groupId) {
  if (!remote_) return Client::getInstance().setGateBMeasureType(measureType, groupId);
  return sendSet(QStringLiteral("setGateBMeasureType"), QJsonArray{static_cast<int>(measureType), groupId}, QStringLiteral("getGateBMeasureType"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBStart(double start, int groupId) {
  if (!remote_) return Client::getInstance().setGateBStart(start, groupId);
  return sendSet(QStringLiteral("setGateBStart"), QJsonArray{start, groupId}, QStringLiteral("getGateBStart"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBSynchronMode(GateSynchron synchro, int groupId) {
  if (!remote_) return Client::getInstance().setGateBSynchronMode(synchro, groupId);
  return sendSet(QStringLiteral("setGateBSynchronMode"), QJsonArray{static_cast<int>(synchro), groupId}, QStringLiteral("getGateBSynchronMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateBThreshold(double threshold, int groupId) {
  if (!remote_) return Client::getInstance().setGateBThreshold(threshold, groupId);
  return sendSet(QStringLiteral("setGateBThreshold"), QJsonArray{threshold, groupId}, QStringLiteral("getGateBThreshold"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setGateCEnable(enable, groupId);
  return sendSet(QStringLiteral("setGateCEnable"), QJsonArray{enable, groupId}, QStringLiteral("getGateCEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCEnd(double end, int groupId) {
  if (!remote_) return Client::getInstance().setGateCEnd(end, groupId);
  return sendSet(QStringLiteral("setGateCEnd"), QJsonArray{end, groupId}, QStringLiteral("getGateCEnd"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCMeasureType(measureType measureType, int groupId) {
  if (!remote_) return Client::getInstance().setGateCMeasureType(measureType, groupId);
  return sendSet(QStringLiteral("setGateCMeasureType"), QJsonArray{static_cast<int>(measureType), groupId}, QStringLiteral("getGateCMeasureType"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCStart(double start, int groupId) {
  if (!remote_) return Client::getInstance().setGateCStart(start, groupId);
  return sendSet(QStringLiteral("setGateCStart"), QJsonArray{start, groupId}, QStringLiteral("getGateCStart"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCSynchronMode(GateSynchron synchro, int groupId) {
  if (!remote_) return Client::getInstance().setGateCSynchronMode(synchro, groupId);
  return sendSet(QStringLiteral("setGateCSynchronMode"), QJsonArray{static_cast<int>(synchro), groupId}, QStringLiteral("getGateCSynchronMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateCThreshold(double threshold, int groupId) {
  if (!remote_) return Client::getInstance().setGateCThreshold(threshold, groupId);
  return sendSet(QStringLiteral("setGateCThreshold"), QJsonArray{threshold, groupId}, QStringLiteral("getGateCThreshold"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateIEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setGateIEnable(enable, groupId);
  return sendSet(QStringLiteral("setGateIEnable"), QJsonArray{enable, groupId}, QStringLiteral("getGateIEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateIEnd(double end, int groupId) {
  if (!remote_) return Client::getInstance().setGateIEnd(end, groupId);
  return sendSet(QStringLiteral("setGateIEnd"), QJsonArray{end, groupId}, QStringLiteral("getGateIEnd"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateIMeasureType(measureType measureType, int groupId) {
  if (!remote_) return Client::getInstance().setGateIMeasureType(measureType, groupId);
  return sendSet(QStringLiteral("setGateIMeasureType"), QJsonArray{static_cast<int>(measureType), groupId}, QStringLiteral("getGateIMeasureType"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateIStart(double start, int groupId) {
  if (!remote_) return Client::getInstance().setGateIStart(start, groupId);
  return sendSet(QStringLiteral("setGateIStart"), QJsonArray{start, groupId}, QStringLiteral("getGateIStart"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateISyncSample(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setGateISyncSample(enable, groupId);
  return sendSet(QStringLiteral("setGateISyncSample"), QJsonArray{enable, groupId}, QStringLiteral("getGateISyncSample"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateISynchronMode(GateSynchron synchro, int groupId) {
  if (!remote_) return Client::getInstance().setGateISynchronMode(synchro, groupId);
  return sendSet(QStringLiteral("setGateISynchronMode"), QJsonArray{static_cast<int>(synchro), groupId}, QStringLiteral("getGateISynchronMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setGateIThreshold(double threshold, int groupId) {
  if (!remote_) return Client::getInstance().setGateIThreshold(threshold, groupId);
  return sendSet(QStringLiteral("setGateIThreshold"), QJsonArray{threshold, groupId}, QStringLiteral("getGateIThreshold"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setMaxAmplitude(MaxAmplitude type, int groupId) {
  if (!remote_) return Client::getInstance().setMaxAmplitude(type, groupId);
  return sendSet(QStringLiteral("setMaxAmplitude"), QJsonArray{static_cast<int>(type), groupId}, QStringLiteral("getMaxAmplitude"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setPaVoltage(int voltage) {
  if (!remote_) return Client::getInstance().setPaVoltage(voltage);
  return sendSet(QStringLiteral("setPaVoltage"), QJsonArray{voltage}, QStringLiteral("getPaVoltage"), QJsonArray{});
}

bool UltrasoundParameterService::setPointQuantity(int quantity, int groupId) {
  if (!remote_) return Client::getInstance().setPointQuantity(quantity, groupId);
  return sendSet(QStringLiteral("setPointQuantity"), QJsonArray{quantity, groupId}, QStringLiteral("getPointQuantity"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setProbeFrequency(double frequency, int groupId) {
  if (!remote_) return Client::getInstance().setProbeFrequency(frequency, groupId);
  return sendSet(QStringLiteral("setProbeFrequency"), QJsonArray{frequency, groupId}, QStringLiteral("getProbeFrequency"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setProbePrimaryElements(int elements, int groupId) {
  if (!remote_) return Client::getInstance().setProbePrimaryElements(elements, groupId);
  return sendSet(QStringLiteral("setProbePrimaryElements"), QJsonArray{elements, groupId}, QStringLiteral("getProbePrimaryElements"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setProbePrimaryElementsPitch(double pitch, int groupId) {
  if (!remote_) return Client::getInstance().setProbePrimaryElementsPitch(pitch, groupId);
  return sendSet(QStringLiteral("setProbePrimaryElementsPitch"), QJsonArray{pitch, groupId}, QStringLiteral("getProbePrimaryElementsPitch"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setProbeSecondaryElements(int elements, int groupId) {
  if (!remote_) return Client::getInstance().setProbeSecondaryElements(elements, groupId);
  return sendSet(QStringLiteral("setProbeSecondaryElements"), QJsonArray{elements, groupId}, QStringLiteral("getProbeSecondaryElements"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setProbeSecondaryElementsPitch(double pitch, int groupId) {
  if (!remote_) return Client::getInstance().setProbeSecondaryElementsPitch(pitch, groupId);
  return sendSet(QStringLiteral("setProbeSecondaryElementsPitch"), QJsonArray{pitch, groupId}, QStringLiteral("getProbeSecondaryElementsPitch"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setPulseWidth(int width, int groupId) {
  if (!remote_) return Client::getInstance().setPulseWidth(width, groupId);
  return sendSet(QStringLiteral("setPulseWidth"), QJsonArray{width, groupId}, QStringLiteral("getPulseWidth"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setRangeEnd(double end, int groupId) {
  if (!remote_) return Client::getInstance().setRangeEnd(end, groupId);
  return sendSet(QStringLiteral("setRangeEnd"), QJsonArray{end, groupId}, QStringLiteral("getRangeEnd"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setRangeStart(double start, int groupId) {
  if (!remote_) return Client::getInstance().setRangeStart(start, groupId);
  return sendSet(QStringLiteral("setRangeStart"), QJsonArray{start, groupId}, QStringLiteral("getRangeStart"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setReceiverReject(int reject, int groupId) {
  if (!remote_) return Client::getInstance().setReceiverReject(reject, groupId);
  return sendSet(QStringLiteral("setReceiverReject"), QJsonArray{reject, groupId}, QStringLiteral("getReceiverReject"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setReceptionChannel(int reception, int groupId) {
  if (!remote_) return Client::getInstance().setReceptionChannel(reception, groupId);
  return sendSet(QStringLiteral("setReceptionChannel"), QJsonArray{reception, groupId}, QStringLiteral("getReceptionChannel"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setRectifierMode(RectifierType mode, int groupId) {
  if (!remote_) return Client::getInstance().setRectifierMode(mode, groupId);
  return sendSet(QStringLiteral("setRectifierMode"), QJsonArray{static_cast<int>(mode), groupId}, QStringLiteral("getRectifierMode"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setReverseEnable(bool flag, int groupId) {
  if (!remote_) return Client::getInstance().setReverseEnable(flag, groupId);
  return sendSet(QStringLiteral("setReverseEnable"), QJsonArray{flag, groupId}, QStringLiteral("getReverseEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setScanMode(int mode) {
  if (!remote_) return Client::getInstance().setScanMode(mode);
  return sendSet(QStringLiteral("setScanMode"), QJsonArray{mode}, QStringLiteral("getScanMode"), QJsonArray{});
}

bool UltrasoundParameterService::setScanType(scanType type, int groupId) {
  if (!remote_) return Client::getInstance().setScanType(type, groupId);
  return sendSet(QStringLiteral("setScanType"), QJsonArray{static_cast<int>(type), groupId}, QStringLiteral("getScanType"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setScannerDirection(ScannerDir mode) {
  if (!remote_) return Client::getInstance().setScannerDirection(mode);
  return sendSet(QStringLiteral("setScannerDirection"), QJsonArray{static_cast<int>(mode)}, QStringLiteral("getScannerDirection"), QJsonArray{});
}

bool UltrasoundParameterService::setTcgEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setTcgEnable(enable, groupId);
  return sendSet(QStringLiteral("setTcgEnable"), QJsonArray{enable, groupId}, QStringLiteral("getTcgEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setTcgPointDepth(int index, double depth, int beamIndex, int groupId) {
  if (!remote_) return Client::getInstance().setTcgPointDepth(index, depth, beamIndex, groupId);
  return sendSet(QStringLiteral("setTcgPointDepth"), QJsonArray{index, depth, beamIndex, groupId}, QStringLiteral("getTcgPointDepth"), QJsonArray{index, beamIndex, groupId});
}

bool UltrasoundParameterService::setTcgPointDepthIndex(int index, double depth, int groupId) {
  if (!remote_) return Client::getInstance().setTcgPointDepthIndex(index, depth, groupId);
  return sendSet(QStringLiteral("setTcgPointDepthIndex"), QJsonArray{index, depth, groupId}, QStringLiteral("getTcgPointDepthIndex"), QJsonArray{index, groupId});
}

bool UltrasoundParameterService::setTcgPointGain(int index, double gain, int beamIndex, int groupId) {
  if (!remote_) return Client::getInstance().setTcgPointGain(index, gain, beamIndex, groupId);
  return sendSet(QStringLiteral("setTcgPointGain"), QJsonArray{index, gain, beamIndex, groupId}, QStringLiteral("getTcgPointGain"), QJsonArray{index, beamIndex, groupId});
}

bool UltrasoundParameterService::setTcgPointGainIndex(int index, double gain, int groupId) {
  if (!remote_) return Client::getInstance().setTcgPointGainIndex(index, gain, groupId);
  return sendSet(QStringLiteral("setTcgPointGainIndex"), QJsonArray{index, gain, groupId}, QStringLiteral("getTcgPointGainIndex"), QJsonArray{index, groupId});
}

bool UltrasoundParameterService::setTfmRecDelay(double delay, int groupId) {
  if (!remote_) return Client::getInstance().setTfmRecDelay(delay, groupId);
  return sendSet(QStringLiteral("setTfmRecDelay"), QJsonArray{delay, groupId}, QStringLiteral("getTfmRecDelay"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setThickness(double thick, int groupId) {
  if (!remote_) return Client::getInstance().setThickness(thick, groupId);
  return sendSet(QStringLiteral("setThickness"), QJsonArray{thick, groupId}, QStringLiteral("getThickness"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setTransmissionChannel(int transmission, int groupId) {
  if (!remote_) return Client::getInstance().setTransmissionChannel(transmission, groupId);
  return sendSet(QStringLiteral("setTransmissionChannel"), QJsonArray{transmission, groupId}, QStringLiteral("getTransmissionChannel"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setUtVoltage(int voltage) {
  if (!remote_) return Client::getInstance().setUtVoltage(voltage);
  return sendSet(QStringLiteral("setUtVoltage"), QJsonArray{voltage}, QStringLiteral("getUtVoltage"), QJsonArray{});
}

bool UltrasoundParameterService::setVideoFilterMHz(VideoFilter type, int groupId) {
  if (!remote_) return Client::getInstance().setVideoFilterMHz(type, groupId);
  return sendSet(QStringLiteral("setVideoFilterMHz"), QJsonArray{static_cast<int>(type), groupId}, QStringLiteral("getVideoFilterMHz"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setVoltagePolarity(PolarityType mode, int groupId) {
  if (!remote_) return Client::getInstance().setVoltagePolarity(mode, groupId);
  return sendSet(QStringLiteral("setVoltagePolarity"), QJsonArray{static_cast<int>(mode), groupId}, QStringLiteral("getVoltagePolarity"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeAngle(double angle, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeAngle(angle, groupId);
  return sendSet(QStringLiteral("setWedgeAngle"), QJsonArray{angle, groupId}, QStringLiteral("getWedgeAngle"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeEnable(bool enable, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeEnable(enable, groupId);
  return sendSet(QStringLiteral("setWedgeEnable"), QJsonArray{enable, groupId}, QStringLiteral("getWedgeEnable"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeGCp(double gcp, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeGCp(gcp, groupId);
  return sendSet(QStringLiteral("setWedgeGCp"), QJsonArray{gcp, groupId}, QStringLiteral("getWedgeGCp"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeHeight(double wedgeHeight, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeHeight(wedgeHeight, groupId);
  return sendSet(QStringLiteral("setWedgeHeight"), QJsonArray{wedgeHeight, groupId}, QStringLiteral("getWedgeHeight"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeLength(double wedgeLength, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeLength(wedgeLength, groupId);
  return sendSet(QStringLiteral("setWedgeLength"), QJsonArray{wedgeLength, groupId}, QStringLiteral("getWedgeLength"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeRoofAngle(double roofangle, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeRoofAngle(roofangle, groupId);
  return sendSet(QStringLiteral("setWedgeRoofAngle"), QJsonArray{roofangle, groupId}, QStringLiteral("getWedgeRoofAngle"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeVelocity(double wedgeVelocity, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeVelocity(wedgeVelocity, groupId);
  return sendSet(QStringLiteral("setWedgeVelocity"), QJsonArray{wedgeVelocity, groupId}, QStringLiteral("getWedgeVelocity"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeWidth(double wedgeWidth, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeWidth(wedgeWidth, groupId);
  return sendSet(QStringLiteral("setWedgeWidth"), QJsonArray{wedgeWidth, groupId}, QStringLiteral("getWedgeWidth"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeX(double x, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeX(x, groupId);
  return sendSet(QStringLiteral("setWedgeX"), QJsonArray{x, groupId}, QStringLiteral("getWedgeX"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWedgeZ(double z, int groupId) {
  if (!remote_) return Client::getInstance().setWedgeZ(z, groupId);
  return sendSet(QStringLiteral("setWedgeZ"), QJsonArray{z, groupId}, QStringLiteral("getWedgeZ"), QJsonArray{groupId});
}

bool UltrasoundParameterService::setWorkpieceVelocity(double velocity, int groupId) {
  if (!remote_) return Client::getInstance().setWorkpieceVelocity(velocity, groupId);
  return sendSet(QStringLiteral("setWorkpieceVelocity"), QJsonArray{velocity, groupId}, QStringLiteral("getWorkpieceVelocity"), QJsonArray{groupId});
}

void UltrasoundParameterService::onResponse(const QString& operation,
                                             quint64 requestId, bool ok,
                                             const QJsonObject& data,
                                             const QString& errorCode,
                                             const QString& errorText) {
  const QString trackedPresetOperation = presetRequests_.take(requestId);
  if (!trackedPresetOperation.isEmpty() &&
      requestId == activePresetRequestId_) {
    if (ok) {
      const QJsonObject state =
          data.contains(QStringLiteral("presets"))
              ? data
              : data.value(QStringLiteral("presetState")).toObject();
      if (!state.isEmpty()) applyPresetState(state, true);
    }
    QString message = data.value(QStringLiteral("message")).toString();
    if (trackedPresetOperation == QStringLiteral("ultrasound.presets.export")) {
      if (!ok) {
        message = QStringLiteral("导出主机参数预设失败 [%1]：%2")
                      .arg(errorCode, errorText);
      } else {
        const QByteArray config = QByteArray::fromBase64(
            data.value(QStringLiteral("configBase64")).toString().toLatin1());
        QSaveFile file(activeExportPath_);
        const bool written =
            !config.isEmpty() && file.open(QIODevice::WriteOnly) &&
            file.write(config) == config.size() && file.commit();
        message = written
                      ? QStringLiteral("主机参数预设已导出：%1")
                            .arg(activeExportPath_)
                      : QStringLiteral("写入导出文件失败：%1")
                            .arg(file.errorString());
        if (!written) {
          emit remotePresetOperationFinished(
              trackedPresetOperation, false, activePresetId_, message);
          activePresetRequestId_ = 0;
          activePresetOperation_.clear();
          activePresetId_ = -1;
          activeExportPath_.clear();
          dispatchNextPresetOperation();
          return;
        }
      }
    } else if (!ok) {
      message = QStringLiteral("%1 [%2]").arg(errorText, errorCode);
    } else if (message.isEmpty()) {
      message = QStringLiteral("主机已完成参数预设操作");
    }
    emit remotePresetOperationFinished(
        trackedPresetOperation, ok,
        data.value(QStringLiteral("newPresetId")).toInt(activePresetId_),
        message);
    if (trackedPresetOperation == QStringLiteral("ultrasound.presets.import") &&
        ok) {
      const QJsonArray ids =
          data.value(QStringLiteral("importedPresetIds")).toArray();
      QStringList encoded;
      for (const QJsonValue& id : ids) encoded << QString::number(id.toInt());
      emit message(QStringLiteral("主机已导入 %1 个参数预设：%2")
                       .arg(ids.size())
                       .arg(encoded.join(QStringLiteral(", "))),
                   false);
    }
    activePresetRequestId_ = 0;
    activePresetOperation_.clear();
    activePresetId_ = -1;
    activeExportPath_.clear();
    dispatchNextPresetOperation();
    return;
  }

  if (operation == QStringLiteral("ultrasound.scan.precision.get")) {
    if (ok)
      emit scanPrecisionChanged(
          data.value(QStringLiteral("presetId")).toInt(currentPresetId_),
          data.value(QStringLiteral("mmPerCell")).toDouble());
    else
      emit message(QStringLiteral("读取主机扫查精度失败 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.scan.precision.set")) {
    const auto it = scanPrecisionRequests_.find(requestId);
    if (!ok) {
      emit message(QStringLiteral("远程保存扫查精度失败 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
      if (it != scanPrecisionRequests_.end())
        scanPrecisionRequests_.erase(it);
    } else if (it != scanPrecisionRequests_.end()) {
      emit scanPrecisionChanged(it.value().first, it.value().second);
      scanPrecisionRequests_.erase(it);
    }
    return;
  }
  if (operation == QStringLiteral("ultrasound.encoder.calibration.get")) {
    if (ok) {
      emit encoderCalibrationReady(
          data.value(QStringLiteral("wheelDiameterMm")).toDouble(500.0),
          data.value(QStringLiteral("resolution")).toInt(4096));
    } else {
      emit message(QStringLiteral("读取主机编码器标定失败 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
    }
    return;
  }
  if (operation == QStringLiteral("ultrasound.encoder.calibration.set")) {
    const auto it = encoderCalibrationRequests_.find(requestId);
    if (!ok) {
      emit message(QStringLiteral("远程保存编码器标定失败 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
      if (it != encoderCalibrationRequests_.end())
        encoderCalibrationRequests_.erase(it);
    } else if (it != encoderCalibrationRequests_.end()) {
      // The controls already contain the requested value. Re-emitting a
      // snapshot here would echo the save back through the operator UI.
      emit message(QStringLiteral("编码器标定已保存"), false);
      encoderCalibrationRequests_.erase(it);
    }
    return;
  }

  if (operation == QStringLiteral("ultrasound.presets.list")) {
    if (ok)
      applyPresetState(data, true);
    else
      emit message(QStringLiteral("读取主机参数预设失败 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
    return;
  }

  if (operation == QStringLiteral("ultrasound.parameters.apply")) {
    const QJsonObject readbacks =
        data.value(QStringLiteral("readbacks")).toObject();
    for (auto it = readbacks.constBegin(); it != readbacks.constEnd(); ++it)
      values_.insert(it.key(), it.value().toVariant());
    scheduleChanged();

    // A rejected batch may have no per-field errors. It must still count as
    // a failed apply or the following commit could report false success.
    if (!ok) {
      ++applyFailureCount_;
      applyErrors_ << QStringLiteral("参数批次被主机拒绝 [%1]：%2")
                          .arg(errorCode, errorText);
    }
    const QJsonArray errors = data.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty()) {
      ++applyFailureCount_;
      for (const QJsonValue& value : errors) {
        const QJsonObject item = value.toObject();
        applyErrors_ << QStringLiteral("%1：%2")
                            .arg(item.value(QStringLiteral("method")).toString(),
                                 item.value(QStringLiteral("error")).toString());
      }
    }
    pendingApplyBatches_ = qMax(0, pendingApplyBatches_ - 1);
    if (applyCommitPending_) {
      if (pendingApplyBatches_ == 0 && pendingSetOrder_.isEmpty() &&
          !commitRequested_) {
        commitRequested_ = true;
        const quint64 id = remote_->request(
            QStringLiteral("ultrasound.parameters.commit"),
            QJsonObject{{QStringLiteral("presetId"), applyPresetId_}});
        if (id == 0)
          finishRemoteApply(false, QStringLiteral("参数提交请求发送失败"));
      }
    } else if (!ok) {
      emit message(QStringLiteral("远程参数应用未完全成功 [%1]：%2")
                       .arg(errorCode, errorText),
                   true);
    }
    return;
  }

  if (operation == QStringLiteral("ultrasound.parameters.commit")) {
    if (ok) applyPresetState(data, true);
    QStringList detail;
    const QString limit = data.value(QStringLiteral("limitMessage")).toString();
    if (!limit.isEmpty()) detail << limit;
    if (!ok) detail << QStringLiteral("[%1] %2").arg(errorCode, errorText);
    if (applyFailureCount_ > 0)
      detail << QStringLiteral("%1 项参数写入失败").arg(applyFailureCount_);
    finishRemoteApply(ok, detail.join(QStringLiteral("；")));
    requestSnapshot();
    return;
  }

  if (!ok) {
    if (operation.startsWith(QStringLiteral("ultrasound.")))
      emit message(QStringLiteral("%1 失败 [%2]：%3")
                       .arg(operation, errorCode, errorText),
                   true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameters.snapshot")) {
    const QJsonObject values = data.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
      values_.insert(it.key(), it.value().toVariant());
    const bool complete = data.value(QStringLiteral("complete")).toBool(true);
    const QByteArray config = QByteArray::fromBase64(
        data.value(QStringLiteral("configBase64")).toString().toLatin1());
    if (complete && !config.isEmpty()) {
      QJsonParseError parseError;
      const QJsonDocument document = QJsonDocument::fromJson(config, &parseError);
      if (parseError.error == QJsonParseError::NoError && document.isObject())
        emit hardwareSnapshotReady(document);
    }
    snapshotRequested_ = false;
    scheduleChanged();
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameters.getMany")) {
    const QJsonObject values = data.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
      values_.insert(it.key(), it.value().toVariant());
    scheduleChanged();
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameter.set")) {
    const QString key = data.value(QStringLiteral("readbackKey")).toString();
    if (!key.isEmpty() && data.contains(QStringLiteral("readbackValue")))
      values_.insert(key, data.value(QStringLiteral("readbackValue")).toVariant());
    scheduleChanged();
  }
}
}  // namespace pa1664::remote




