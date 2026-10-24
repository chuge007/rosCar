#include "ultrasound_host_service.h"

#include "client.h"
#include "devicecontroller.h"
#include "parameterpresetstore.h"
#include "pulseloadguard.h"
#include "services/parameter_key.h"
#include "services/workbench_data_codec.h"
#include "ultrasound_parameter_rpc.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QQueue>
#include <QThread>
#include <QTimer>

#include <cmath>

namespace pa1664::remote {
namespace {

constexpr int kMaximumParameterRequests = 512;
constexpr qint64 kMaximumEncoderBatchBacklogBytes = 128ll * 1024 * 1024;
constexpr int kFrameEncodeIntervalMs = 20;
constexpr int kBatchEncodeIntervalMs = 4;

QJsonArray presetArray(const QList<ParameterPresetInfo>& presets) {
  QJsonArray result;
  for (const ParameterPresetInfo& preset : presets) {
    result.append(QJsonObject{{QStringLiteral("id"), preset.id},
                              {QStringLiteral("name"), preset.name}});
  }
  return result;
}

QJsonObject deviceStatus(DeviceController* device) {
  QJsonObject status;
  if (!device) {
    status.insert(QStringLiteral("available"), false);
    return status;
  }
  status.insert(QStringLiteral("available"), true);
  status.insert(QStringLiteral("serviceConnected"), device->serviceConnected());
  status.insert(QStringLiteral("hardwareOnline"), device->hardwareOnline());
  status.insert(QStringLiteral("capturing"), device->capturing());
  status.insert(QStringLiteral("captureReady"), device->captureReady());
  status.insert(QStringLiteral("startPending"), device->startPending());
  status.insert(QStringLiteral("pointCount"), device->pointCount());

  status.insert(QStringLiteral("maxAmplitudeValue"),
                device->maxAmplitudeValue());
  status.insert(QStringLiteral("currentGroup"), device->currentGroup());
  status.insert(QStringLiteral("totalFrames"),
                static_cast<double>(device->totalFrames()));
  status.insert(QStringLiteral("displayDropped"),
                static_cast<double>(device->displayDroppedPackets()));
  status.insert(QStringLiteral("scanDropped"),
                static_cast<double>(device->cScanDroppedPackets()));
  status.insert(QStringLiteral("scanQueueBytes"),
                static_cast<double>(device->cScanQueueBytes()));
  status.insert(QStringLiteral("scanQueueDepth"),
                static_cast<double>(device->cScanQueueDepth()));
  return status;
}

QVector<GroupScanSettings> decoderSettingsFromJson(const QJsonObject& data,
                                                    bool* ok,
                                                    QString* error) {
  if (ok) *ok = false;
  if (error) error->clear();
  const QJsonArray groups = data.value(QStringLiteral("groups")).toArray();
  if (groups.isEmpty() || groups.size() > 64) {
    if (error) *error = QStringLiteral("解码工作组配置为空或数量超限");
    return {};
  }
  QVector<GroupScanSettings> result;
  result.reserve(groups.size());
  for (const QJsonValue& value : groups) {
    const QJsonObject item = value.toObject();
    GroupScanSettings settings;
    settings.groupId = item.value(QStringLiteral("groupId")).toInt(-1);
    settings.groupOffsetWords =
        item.value(QStringLiteral("groupOffsetWords")).toInt(-1);
    settings.beamCount = item.value(QStringLiteral("beamCount")).toInt(1);
    settings.pointCount = item.value(QStringLiteral("pointCount")).toInt(1024);
    settings.scanModes = item.value(QStringLiteral("scanModes")).toInt();
    settings.selectedBeam =
        item.value(QStringLiteral("selectedBeam")).toInt();
    settings.amplitudeScale =
        item.value(QStringLiteral("amplitudeScale")).toInt(4096);
    settings.encoderPrecision =
        item.value(QStringLiteral("encoderPrecision")).toInt(10);
    settings.rangeStart =
        item.value(QStringLiteral("rangeStart")).toDouble();
    settings.rangeEnd = item.value(QStringLiteral("rangeEnd")).toDouble(50.0);
    if (settings.groupId < 0 || settings.groupOffsetWords < 0 ||
        settings.beamCount <= 0 || settings.beamCount > 8192 ||
        settings.pointCount <= 0 || settings.pointCount > 65536 ||
        settings.selectedBeam < 0 || settings.selectedBeam >= settings.beamCount ||
        settings.encoderPrecision <= 0 || settings.rangeEnd <= settings.rangeStart) {
      if (error)
        *error = QStringLiteral("解码工作组 %1 的几何配置无效")
                     .arg(settings.groupId);
      return {};
    }
    result.push_back(settings);
  }
  if (ok) *ok = true;
  return result;
}

}  // namespace

// Keeps expensive frame/scan compression away from both the SDK control thread
// and the QObject thread that owns the socket channels. Live frames are a
// latest-value mailbox; scan batches are a FIFO because they are part of a
// complete C-scan and must preserve ordering.
class UltrasoundUplinkEncoder final : public QObject {
  Q_OBJECT
 public:
  explicit UltrasoundUplinkEncoder(QObject* parent = nullptr)
      : QObject(parent), frameTimer_(new QTimer(this)),
        batchTimer_(new QTimer(this)) {
    frameTimer_->setInterval(kFrameEncodeIntervalMs);
    batchTimer_->setInterval(kBatchEncodeIntervalMs);
    connect(frameTimer_, &QTimer::timeout, this,
            &UltrasoundUplinkEncoder::encodeFrame);
    connect(batchTimer_, &QTimer::timeout, this,
            &UltrasoundUplinkEncoder::encodeBatch);
    qRegisterMetaType<DecodedFrame>("DecodedFrame");
    qRegisterMetaType<GroupScanFrameBatch>("GroupScanFrameBatch");
  }

 public slots:
  void enqueueFrame(DecodedFrame frame, int groupId, int deviceId) {
    pendingFrame_ = std::move(frame);
    pendingFrameGroupId_ = groupId;
    pendingFrameDeviceId_ = deviceId;
    if (!frameTimer_->isActive()) frameTimer_->start();
  }

  void enqueueBatch(GroupScanFrameBatch frames, int) {
    qint64 bytes = 0;
    for (const GroupScanFrame& item : frames) {
      for (const QVector<qint16>& beam : item.frame.beams) {
        bytes += static_cast<qint64>(beam.size()) * sizeof(qint16);
      }
      bytes += static_cast<qint64>(item.frame.measurements.size()) *
               sizeof(BeamMeasurement);
    }
    if (batchBacklogBytes_ + bytes > kMaximumEncoderBatchBacklogBytes) {
      emit backlogOverflow(batchBacklogBytes_ + bytes);
      return;
    }
    batchBacklogBytes_ += bytes;
    pendingBatches_.enqueue({std::move(frames), bytes});
    if (!batchTimer_->isActive()) batchTimer_->start();
  }

 signals:
  void frameReady(const QByteArray& payload, int groupId, int deviceId);
  void batchReady(const QByteArray& payload);
  void backlogOverflow(qint64 bytes);

 private slots:
  void clearPending() {
    pendingFrame_ = {};
    pendingBatches_.clear();
    batchBacklogBytes_ = 0;
    frameTimer_->stop();
    batchTimer_->stop();
  }

  void encodeFrame() {
    if (pendingFrame_.beams.isEmpty()) {
      frameTimer_->stop();
      return;
    }
    const QByteArray payload = encodeDecodedFrame(pendingFrame_);
    const int groupId = pendingFrameGroupId_;
    const int deviceId = pendingFrameDeviceId_;
    pendingFrame_ = {};
    if (!payload.isEmpty()) emit frameReady(payload, groupId, deviceId);
  }

  void encodeBatch() {
    if (pendingBatches_.isEmpty()) {
      batchTimer_->stop();
      return;
    }
    BatchJob job = std::move(pendingBatches_.head());
    pendingBatches_.dequeue();
    batchBacklogBytes_ = qMax<qint64>(0, batchBacklogBytes_ - job.bytes);
    const QByteArray payload = encodeGroupScanBatch(job.frames);
    if (!payload.isEmpty()) emit batchReady(payload);
  }

 private:
  struct BatchJob {
    GroupScanFrameBatch frames;
    qint64 bytes = 0;
  };
  QTimer* frameTimer_ = nullptr;
  QTimer* batchTimer_ = nullptr;
  DecodedFrame pendingFrame_;
  int pendingFrameGroupId_ = 0;
  int pendingFrameDeviceId_ = 0;
  QQueue<BatchJob> pendingBatches_;
  qint64 batchBacklogBytes_ = 0;
};

UltrasoundHostService::UltrasoundHostService(QObject* parent)
    : QObject(parent), statusTimer_(new QTimer(this)) {
  statusTimer_->setInterval(1000);
  connect(statusTimer_, &QTimer::timeout, this,
          &UltrasoundHostService::sendStatusEvent);
}

UltrasoundHostService::~UltrasoundHostService() { shutdown(); }

void UltrasoundHostService::initialize() {
  if (initialized_ || shuttingDown_) return;
  initialized_ = true;
  QString presetMessage;
  QString presetError;
  if (!ParameterPresetStore::prepare(&presetMessage, &presetError)) {
    emit logMessage(ownerSessionId_,
                    QStringLiteral("参数预设初始化失败：%1").arg(presetError));
  } else if (!presetMessage.isEmpty()) {
    emit logMessage(ownerSessionId_, presetMessage);
  }
  qRegisterMetaType<GroupScanSettings>("GroupScanSettings");
  qRegisterMetaType<QVector<GroupScanSettings>>("QVector<GroupScanSettings>");
  device_ = new DeviceController(this);

  decoderThread_ = new QThread(this);
  decoderThread_->setObjectName(QStringLiteral("UltrasoundDecoder"));
  decoder_ = new FrameDecoderWorker(device_);
  decoder_->moveToThread(decoderThread_);
  connect(decoderThread_, &QThread::started, decoder_,
          &FrameDecoderWorker::startPolling, Qt::QueuedConnection);
  connect(decoderThread_, &QThread::finished, decoder_,
          &QObject::deleteLater);

  encoderThread_ = new QThread(this);
  encoderThread_->setObjectName(QStringLiteral("UltrasoundUplink"));
  encoder_ = new UltrasoundUplinkEncoder;
  encoder_->moveToThread(encoderThread_);
  connect(encoderThread_, &QThread::finished, encoder_,
          &QObject::deleteLater);
  connect(decoder_, &FrameDecoderWorker::frameDecoded, encoder_,
          &UltrasoundUplinkEncoder::enqueueFrame, Qt::QueuedConnection);
  connect(decoder_, &FrameDecoderWorker::groupScanBatchDecoded, encoder_,
          &UltrasoundUplinkEncoder::enqueueBatch, Qt::QueuedConnection);
  connect(encoder_, &UltrasoundUplinkEncoder::frameReady, this,
          &UltrasoundHostService::frameEncoded, Qt::QueuedConnection);
  connect(encoder_, &UltrasoundUplinkEncoder::batchReady, this,
          &UltrasoundHostService::scanBatchEncoded, Qt::QueuedConnection);
  connect(encoder_, &UltrasoundUplinkEncoder::backlogOverflow, this,
          [this](qint64 bytes) {
            sendEvent(QStringLiteral("ultrasound.backlogOverflow"),
                      QJsonObject{{QStringLiteral("bytes"),
                                   static_cast<double>(bytes)},
                                  {QStringLiteral("message"),
                                   QStringLiteral("扫描批次待发送队列超过安全上限，"
                                                  "采集已停止以避免无声数据丢失")}});
            if (device_) device_->stop();
            emit logMessage(ownerSessionId_,
                            QStringLiteral("超声扫描批次积压超限：%1 字节")
                                .arg(bytes));
          }, Qt::QueuedConnection);

  connect(decoder_, &FrameDecoderWorker::decodeFailed, this,
          [this](const QString& error) {
            emit logMessage(ownerSessionId_,
                            QStringLiteral("超声解码失败：%1").arg(error));
          }, Qt::QueuedConnection);
  connect(device_, &DeviceController::message, this,
          [this](const QString& text, bool error) {
            emit logMessage(ownerSessionId_,
                            QStringLiteral("%1%2")
                                .arg(error ? QStringLiteral("[错误] ")
                                           : QStringLiteral("[设备] "),
                                     text));
          }, Qt::QueuedConnection);
  connect(device_, &DeviceController::statusChanged, this,
          [this] {
            const bool hardwareOnline = device_->hardwareOnline();
            const bool captureReady = device_->captureReady();
            sendStatusEvent();
            if (lastHardwareOnline_ != hardwareOnline ||
                lastCaptureReady_ != captureReady) {
              lastHardwareOnline_ = hardwareOnline;
              lastCaptureReady_ = captureReady;
              sendEvent(QStringLiteral("ultrasound.parameters.invalidated"),
                        QJsonObject{{QStringLiteral("hardwareOnline"),
                                     hardwareOnline},
                                    {QStringLiteral("captureReady"),
                                     captureReady}});
            }
          }, Qt::QueuedConnection);

  decoderThread_->start(QThread::HighPriority);
  encoderThread_->start(QThread::NormalPriority);
  statusTimer_->start();

  // Headless deployment stays ready across operator reconnects. A failed
  // first connect is reported by DeviceController and does not block the
  // control or media listeners.
  device_->connectDevice(QStringLiteral("127.0.0.1"), 0);
  device_->start();
}

void UltrasoundHostService::shutdown() {
  if (shuttingDown_) return;
  shuttingDown_ = true;
  if (statusTimer_) statusTimer_->stop();
  if (device_) {
    device_->stop();
    device_->disconnectDevice();
  }
  if (decoderThread_ && decoderThread_->isRunning()) {
    decoderThread_->requestInterruption();
    decoderThread_->quit();
    if (!decoderThread_->wait(5000)) {
      qWarning() << "ULTRASOUND_DECODER_THREAD_SHUTDOWN_TIMEOUT";
      decoderThread_->terminate();
      decoderThread_->wait();
    }
  }
  if (encoderThread_ && encoderThread_->isRunning()) {
    encoderThread_->requestInterruption();
    encoderThread_->quit();
    if (!encoderThread_->wait(5000)) {
      qWarning() << "ULTRASOUND_ENCODER_THREAD_SHUTDOWN_TIMEOUT";
      encoderThread_->terminate();
      encoderThread_->wait();
    }
  }
  decoder_ = nullptr;
  encoder_ = nullptr;
  initialized_ = false;
  emit shutdownComplete();
}

void UltrasoundHostService::abortAcquisition(const QString& reason) {
  if (shuttingDown_) return;
  if (device_ && (device_->capturing() || device_->startPending()))
    device_->stop();
  const QString text = reason.isEmpty()
                           ? QStringLiteral("超声采集已由主机安全中止")
                           : reason;
  emit logMessage(ownerSessionId_, text);
  sendEvent(QStringLiteral("ultrasound.acquisitionAborted"),
            QJsonObject{{QStringLiteral("reason"), text}});
  sendStatusEvent();
}

void UltrasoundHostService::claimSession(quint64 sessionId) {
  ownerSessionId_ = sessionId;
}

void UltrasoundHostService::releaseSession(quint64 sessionId) {
  if (ownerSessionId_ != sessionId) return;
  // A disconnected operator must not leave the transducer transmitting or
  // filling the reliable scan queue with nobody able to consume it.
  if (device_ && (device_->capturing() || device_->startPending()))
    device_->stop();
  if (encoder_) {
    QMetaObject::invokeMethod(encoder_, "clearPending", Qt::QueuedConnection);
  }
  ownerSessionId_ = 0;
}

void UltrasoundHostService::handleRequest(quint64 sessionId,
                                          quint64 requestId,
                                          const QJsonObject& request) {
  if (!acceptOwner(sessionId, requestId)) return;
  const QString operation =
      request.value(QStringLiteral("operation")).toString();
  const QJsonObject data = request.value(QStringLiteral("data")).toObject();

  if (operation == QStringLiteral("ultrasound.parameters.snapshot")) {
    handleParameterSnapshot(sessionId, requestId);
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameters.getMany")) {
    handleParameterGetMany(sessionId, requestId, data);
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameter.set")) {
    handleParameterSet(sessionId, requestId, data);
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameters.apply")) {
    handleParameterApply(sessionId, requestId, data);
    return;
  }
  if (operation.startsWith(QStringLiteral("ultrasound.parameters.commit")) ||
      operation.startsWith(QStringLiteral("ultrasound.presets.")) ||
      operation.startsWith(QStringLiteral("ultrasound.scan.precision.")) ||
      operation.startsWith(QStringLiteral("ultrasound.encoder.calibration"))) {
    handlePresetRequest(sessionId, requestId, operation, data);
    return;
  }
  if (operation == QStringLiteral("ultrasound.decoder.settings")) {
    bool ok = false;
    QString error;
    const QVector<GroupScanSettings> groups =
        decoderSettingsFromJson(data, &ok, &error);
    if (!ok) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_decoder_settings"), error);
      return;
    }
    const int displayGroupId =
        data.value(QStringLiteral("displayGroupId")).toInt(-1);
    QMetaObject::invokeMethod(decoder_, "updateSettings", Qt::QueuedConnection,
                              Q_ARG(QVector<GroupScanSettings>, groups),
                              Q_ARG(int, displayGroupId));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.device.connect")) {
    const QString address =
        data.value(QStringLiteral("address"))
            .toString(QStringLiteral("127.0.0.1"));
    const int deviceId = data.value(QStringLiteral("deviceId")).toInt(0);
    if (!device_->connectDevice(address, deviceId)) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("device_connect_failed"),
              QStringLiteral("SDK 连接请求失败"));
      return;
    }
    respond(sessionId, requestId, true, statusSnapshot());
    return;
  }
  if (operation == QStringLiteral("ultrasound.device.disconnect")) {
    device_->stop();
    device_->disconnectDevice();
    respond(sessionId, requestId, true, statusSnapshot());
    return;
  }
  if (operation == QStringLiteral("ultrasound.acquisition.start")) {
    if (!device_->start()) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("acquisition_start_failed"),
              QStringLiteral("采集启动请求失败"));
      return;
    }
    respond(sessionId, requestId, true, statusSnapshot());
    return;
  }
  if (operation == QStringLiteral("ultrasound.acquisition.stop")) {
    device_->stop();
    respond(sessionId, requestId, true, statusSnapshot());
    return;
  }
  if (operation == QStringLiteral("ultrasound.encoder.reset")) {
    device_->resetEncoder();
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.geometry.refresh")) {
    device_->refreshGeometry();
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.status")) {
    respond(sessionId, requestId, true, statusSnapshot());
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的超声主机操作：%1").arg(operation));
}

void UltrasoundHostService::handleParameterSnapshot(quint64 sessionId,
                                                    quint64 requestId) {
  QJsonObject values;
  QJsonArray errors;
  Client& client = Client::getInstance();
  for (const QString& method : UltrasoundParameterRpc::getterNames()) {
    const QJsonArray args = UltrasoundParameterRpc::defaultArguments(method);
    QJsonValue value;
    QString error;
    if (UltrasoundParameterRpc::read(client, method, args, &value, &error)) {
      values.insert(parameterCacheKey(method, args), value);
    } else {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("error"), error}});
    }
  }
  QByteArray config;
  readConfigBase64(&config);
  const QJsonObject result{
      {QStringLiteral("values"), values},
      {QStringLiteral("complete"), errors.isEmpty()},
      {QStringLiteral("configBase64"), QString::fromLatin1(config.toBase64())},
      {QStringLiteral("errors"), errors},
      {QStringLiteral("status"), statusSnapshot()}};
  respond(sessionId, requestId, true, result);
}

void UltrasoundHostService::handleParameterGetMany(
    quint64 sessionId, quint64 requestId, const QJsonObject& data) {
  const QJsonArray requests = data.value(QStringLiteral("requests")).toArray();
  if (requests.isEmpty() || requests.size() > kMaximumParameterRequests) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_parameter_batch"),
            QStringLiteral("参数读取批次为空或数量超限"));
    return;
  }
  QJsonObject values;
  QJsonArray errors;
  Client& client = Client::getInstance();
  for (const QJsonValue& item : requests) {
    const QJsonObject request = item.toObject();
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonArray args = request.value(QStringLiteral("args")).toArray();
    if (!method.startsWith(QStringLiteral("get"))) {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("error"),
                                 QStringLiteral("只接受 getter")}});
      continue;
    }
    QJsonValue value;
    QString error;
    if (UltrasoundParameterRpc::read(client, method, args, &value, &error)) {
      values.insert(parameterCacheKey(method, args), value);
    } else {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("args"), args},
                                {QStringLiteral("error"), error}});
    }
  }
  respond(sessionId, requestId, true,
          QJsonObject{{QStringLiteral("values"), values},
                      {QStringLiteral("errors"), errors}});
}

void UltrasoundHostService::handleParameterSet(quint64 sessionId,
                                               quint64 requestId,
                                               const QJsonObject& data) {
  const QString method = data.value(QStringLiteral("method")).toString();
  const QJsonArray args = data.value(QStringLiteral("args")).toArray();
  const QString readbackMethod =
      data.value(QStringLiteral("readbackMethod")).toString();
  const QJsonArray readbackArgs =
      data.value(QStringLiteral("readbackArgs")).toArray();
  if (!method.startsWith(QStringLiteral("set")) ||
      !readbackMethod.startsWith(QStringLiteral("get"))) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_parameter_set"),
            QStringLiteral("参数写入/回读方法无效"));
    return;
  }
  Client& client = Client::getInstance();
  QString error;
  if (!UltrasoundParameterRpc::write(client, method, args, &error)) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("parameter_write_failed"), error);
    return;
  }
  QJsonValue readbackValue;
  if (!UltrasoundParameterRpc::read(client, readbackMethod, readbackArgs,
                                    &readbackValue, &error)) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("parameter_readback_failed"), error);
    return;
  }
  respond(sessionId, requestId, true,
          QJsonObject{{QStringLiteral("readbackKey"),
                       parameterCacheKey(readbackMethod, readbackArgs)},
                      {QStringLiteral("readbackValue"), readbackValue}});
}

void UltrasoundHostService::handleParameterApply(
    quint64 sessionId, quint64 requestId, const QJsonObject& data) {
  const QJsonArray changes = data.value(QStringLiteral("changes")).toArray();
  if (changes.isEmpty() || changes.size() > kMaximumParameterRequests) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_parameter_batch"),
            QStringLiteral("参数应用批次为空或数量超限"));
    return;
  }
  QJsonObject readbacks;
  QJsonArray errors;
  Client& client = Client::getInstance();
  for (const QJsonValue& value : changes) {
    const QJsonObject change = value.toObject();
    const QString method = change.value(QStringLiteral("method")).toString();
    const QJsonArray args = change.value(QStringLiteral("args")).toArray();
    const QString readbackMethod =
        change.value(QStringLiteral("readbackMethod")).toString();
    const QJsonArray readbackArgs =
        change.value(QStringLiteral("readbackArgs")).toArray();
    QString error;
    if (!method.startsWith(QStringLiteral("set")) ||
        !readbackMethod.startsWith(QStringLiteral("get"))) {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("error"),
                                 QStringLiteral("参数写入/回读方法无效")}});
      continue;
    }
    if (!UltrasoundParameterRpc::write(client, method, args, &error)) {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("error"), error}});
      continue;
    }
    QJsonValue readbackValue;
    if (!UltrasoundParameterRpc::read(client, readbackMethod, readbackArgs,
                                      &readbackValue, &error)) {
      errors.append(QJsonObject{{QStringLiteral("method"), method},
                                {QStringLiteral("error"), error}});
      continue;
    }
    readbacks.insert(parameterCacheKey(readbackMethod, readbackArgs),
                     readbackValue);
  }
  QString limitMessage;
  if (errors.isEmpty()) {
    if (!PulseLoadGuard::enforceSdkLimit(&limitMessage)) {
      errors.append(QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("PulseLoadGuard")},
                                {QStringLiteral("error"),
                                 QStringLiteral("PRF 安全限幅失败")}});
    } else if (!limitMessage.isEmpty()) {
      QJsonValue frameRate;
      QString readError;
      if (UltrasoundParameterRpc::read(
              client, QStringLiteral("getFrameRate"), QJsonArray{}, &frameRate,
              &readError)) {
        readbacks.insert(parameterCacheKey(QStringLiteral("getFrameRate"),
                                           QJsonArray{}),
                         frameRate);
      }
    }
  }
  respond(sessionId, requestId, errors.isEmpty(),
          QJsonObject{{QStringLiteral("readbacks"), readbacks},
                      {QStringLiteral("errors"), errors},
                      {QStringLiteral("limitMessage"), limitMessage}},
          errors.isEmpty() ? QString()
                           : QStringLiteral("parameter_apply_partial"),
          errors.isEmpty()
              ? QString()
              : QStringLiteral("有 %1 项参数写入或回读失败").arg(errors.size()));
}

void UltrasoundHostService::handlePresetRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  const auto presetState = [this]() {
    QString error;
    const QList<ParameterPresetInfo> presets =
        ParameterPresetStore::presets(&error);
    const int current = ParameterPresetStore::currentPresetId();
    QByteArray config;
    readConfigBase64(&config);
    double wheelDiameterMm = 500.0;
    int encoderResolution = 4096;
    ParameterPresetStore::loadEncoderCalibration(&wheelDiameterMm,
                                                  &encoderResolution);
    return QJsonObject{
        {QStringLiteral("presets"), presetArray(presets)},
        {QStringLiteral("currentPreset"), current},
        {QStringLiteral("scanPrecisionMmPerCell"),
         ParameterPresetStore::scanPrecisionMmPerCell(current)},
        {QStringLiteral("wheelDiameterMm"), wheelDiameterMm},
        {QStringLiteral("encoderResolution"), encoderResolution},
        {QStringLiteral("configBase64"), QString::fromLatin1(config.toBase64())},
        {QStringLiteral("error"), error},
        {QStringLiteral("status"), statusSnapshot()}};
  };
  const auto fail = [this, sessionId, requestId](const QString& code,
                                                 const QString& text) {
    respond(sessionId, requestId, false, {}, code, text);
  };

  if (operation == QStringLiteral("ultrasound.presets.list")) {
    respond(sessionId, requestId, true, presetState());
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.capture")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    if (presetId < 0) {
      fail(QStringLiteral("invalid_preset"), QStringLiteral("参数预设 ID 无效"));
      return;
    }
    QString error;
    if (!ParameterPresetStore::captureCurrentHardware(presetId, &error)) {
      fail(QStringLiteral("preset_capture_failed"), error);
      return;
    }
    sendEvent(QStringLiteral("ultrasound.parameters.invalidated"),
              QJsonObject{{QStringLiteral("reason"),
                           QStringLiteral("preset_captured")}});
    respond(sessionId, requestId, true, presetState());
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.switch")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    if (presetId < 0) {
      fail(QStringLiteral("invalid_preset"), QStringLiteral("目标参数预设 ID 无效"));
      return;
    }
    const int previous = ParameterPresetStore::currentPresetId();
    if (presetId == previous) {
      respond(sessionId, requestId, true, presetState());
      return;
    }
    QString error;
    if (device_) device_->stop();
    if (!ParameterPresetStore::captureCurrentHardware(previous, &error)) {
      fail(QStringLiteral("preset_capture_failed"),
           QStringLiteral("切换前保存当前参数预设失败：%1").arg(error));
      return;
    }
    if (!ParameterPresetStore::setCurrentPresetId(presetId, &error) ||
        !ParameterPresetStore::applyPresetToHardware(presetId, nullptr, &error)) {
      QString rollbackError;
      if (!ParameterPresetStore::setCurrentPresetId(previous, &rollbackError))
        error += QStringLiteral("；恢复原预设选择失败：%1").arg(rollbackError);
      if (!ParameterPresetStore::applyPresetToHardware(
              previous, nullptr, &rollbackError)) {
        error += QStringLiteral("；恢复原预设硬件失败：%1").arg(rollbackError);
      }
      fail(QStringLiteral("preset_switch_failed"), error);
      return;
    }
    if (device_) device_->refreshGeometry();
    sendEvent(QStringLiteral("ultrasound.parameters.invalidated"),
              QJsonObject{{QStringLiteral("reason"),
                           QStringLiteral("preset_switched")},
                          {QStringLiteral("presetId"), presetId}});
    respond(sessionId, requestId, true, presetState());
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.copy")) {
    const int source = data.value(QStringLiteral("sourcePresetId")).toInt(-1);
    QString error;
    const int newPreset = ParameterPresetStore::copyPreset(source, &error);
    if (newPreset < 0) {
      fail(QStringLiteral("preset_copy_failed"), error);
      return;
    }
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("newPresetId"), newPreset},
                        {QStringLiteral("presetState"), presetState()}});
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.remove")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    if (presetId == ParameterPresetStore::currentPresetId()) {
      fail(QStringLiteral("preset_in_use"),
           QStringLiteral("当前参数预设不能直接删除，请先切换到其他预设"));
      return;
    }
    int current = ParameterPresetStore::currentPresetId();
    QString error;
    if (!ParameterPresetStore::removePreset(presetId, &current, &error)) {
      fail(QStringLiteral("preset_remove_failed"), error);
      return;
    }
    respond(sessionId, requestId, true, presetState());
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.import")) {
    const QByteArray bytes =
        QByteArray::fromBase64(data.value(QStringLiteral("configBase64"))
                                   .toString()
                                   .toLatin1());
    if (bytes.isEmpty()) {
      fail(QStringLiteral("invalid_import"),
           QStringLiteral("导入配置为空或 Base64 无效"));
      return;
    }
    const QString temporaryPath =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("parameter_presets_import.tmp.json"));
    QFile temporary(temporaryPath);
    if (!temporary.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        temporary.write(bytes) != bytes.size()) {
      fail(QStringLiteral("import_write_failed"),
           QStringLiteral("主机无法暂存导入配置：%1")
               .arg(temporary.errorString()));
      return;
    }
    temporary.close();
    QString error;
    const QList<int> added =
        ParameterPresetStore::importConfiguration(temporaryPath, &error);
    temporary.remove();
    if (added.isEmpty()) {
      fail(QStringLiteral("preset_import_failed"), error);
      return;
    }
    QJsonArray ids;
    for (int id : added) ids.append(id);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("importedPresetIds"), ids},
                        {QStringLiteral("presetState"), presetState()}});
    return;
  }
  if (operation == QStringLiteral("ultrasound.presets.export")) {
    QString error;
    const QList<ParameterPresetInfo> presets =
        ParameterPresetStore::presets(&error);
    if (presets.isEmpty()) {
      fail(QStringLiteral("preset_export_failed"), error);
      return;
    }
    QByteArray config;
    readConfigBase64(&config);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("presets"), presetArray(presets)},
                        {QStringLiteral("configBase64"),
                         QString::fromLatin1(config.toBase64())}});
    return;
  }
  if (operation == QStringLiteral("ultrasound.parameters.commit")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    if (presetId < 0) {
      fail(QStringLiteral("invalid_preset"), QStringLiteral("参数预设 ID 无效"));
      return;
    }
    QString limitMessage;
    if (!PulseLoadGuard::enforceSdkLimit(&limitMessage)) {
      fail(QStringLiteral("pulse_limit_failed"),
           QStringLiteral("参数提交前 PRF 安全限幅失败"));
      return;
    }
    QString error;
    if (!ParameterPresetStore::captureCurrentHardware(presetId, &error)) {
      fail(QStringLiteral("preset_capture_failed"),
           QStringLiteral("参数已写入设备，但保存参数预设失败：%1").arg(error));
      return;
    }
    sendEvent(QStringLiteral("ultrasound.parameters.invalidated"),
              QJsonObject{{QStringLiteral("reason"),
                           QStringLiteral("parameters_committed")},
                          {QStringLiteral("limitMessage"), limitMessage}});
    respond(sessionId, requestId, true, presetState());
    return;
  }
  if (operation == QStringLiteral("ultrasound.scan.precision.get")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("presetId"), presetId},
                        {QStringLiteral("mmPerCell"),
                         ParameterPresetStore::scanPrecisionMmPerCell(presetId)}});
    return;
  }
  if (operation == QStringLiteral("ultrasound.scan.precision.set")) {
    const int presetId = data.value(QStringLiteral("presetId")).toInt(-1);
    const double mmPerCell =
        data.value(QStringLiteral("mmPerCell")).toDouble(0.0);
    QString error;
    if (!ParameterPresetStore::saveScanPrecisionMmPerCell(
            presetId, mmPerCell, &error)) {
      fail(QStringLiteral("scan_precision_failed"), error);
      return;
    }
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("ultrasound.encoder.calibration.get")) {
    double diameterMm = 500.0;
    int resolution = 4096;
    ParameterPresetStore::loadEncoderCalibration(&diameterMm, &resolution);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("wheelDiameterMm"), diameterMm},
                        {QStringLiteral("resolution"), resolution}});
    return;
  }
  if (operation == QStringLiteral("ultrasound.encoder.calibration.set")) {
    const double diameterMm =
        data.value(QStringLiteral("wheelDiameterMm")).toDouble(0.0);
    const int resolution =
        data.value(QStringLiteral("resolution")).toInt(0);
    if (!std::isfinite(diameterMm) || diameterMm <= 0.0 || resolution <= 0) {
      fail(QStringLiteral("invalid_encoder_calibration"),
           QStringLiteral("编码器轮径或分辨率无效"));
      return;
    }
    QString error;
    if (!ParameterPresetStore::saveEncoderCalibration(
            diameterMm, resolution, &error)) {
      fail(QStringLiteral("encoder_calibration_failed"), error);
      return;
    }
    respond(sessionId, requestId, true);
    return;
  }
  fail(QStringLiteral("unsupported_operation"),
       QStringLiteral("不支持的超声预设操作：%1").arg(operation));
}

QJsonObject UltrasoundHostService::statusSnapshot() const {
  QJsonObject result;
  result.insert(QStringLiteral("device"), deviceStatus(device_));
  result.insert(QStringLiteral("ownerSessionId"),
                QString::number(ownerSessionId_));
  result.insert(QStringLiteral("decoderAvailable"), decoder_ != nullptr);
  result.insert(QStringLiteral("timeMs"),
                static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
  return result;
}

void UltrasoundHostService::respond(quint64 sessionId, quint64 requestId,
                                    bool ok, const QJsonObject& data,
                                    const QString& errorCode,
                                    const QString& errorText) {
  emit responseReady(sessionId, requestId, ok, data, errorCode, errorText);
}

void UltrasoundHostService::sendEvent(const QString& name,
                                      const QJsonObject& data) {
  if (ownerSessionId_ != 0)
    emit eventReady(ownerSessionId_, name, data);
}

void UltrasoundHostService::sendStatusEvent() {
  if (ownerSessionId_ == 0 || shuttingDown_) return;
  emit eventReady(ownerSessionId_, QStringLiteral("ultrasound.status"),
                  statusSnapshot());
}

bool UltrasoundHostService::acceptOwner(quint64 sessionId,
                                        quint64 requestId) {
  if (sessionId != 0 && sessionId == ownerSessionId_) return true;
  respond(sessionId, requestId, false, {},
          QStringLiteral("not_session_owner"),
          QStringLiteral("当前操作台不是小车主机的控制会话"));
  return false;
}

bool UltrasoundHostService::readConfigBase64(QByteArray* bytes) const {
  if (!bytes) return false;
  bytes->clear();
  QFile config(QDir(QCoreApplication::applicationDirPath())
                   .filePath(QStringLiteral("default_config.json")));
  if (!config.open(QIODevice::ReadOnly)) return false;
  *bytes = config.readAll();
  return true;
}

}  // namespace pa1664::remote

#include "ultrasound_host_service.moc"



