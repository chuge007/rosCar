#include "robot_peripheral_host_service.h"

#include "robot_host_service.h"
#include "clamp_motor_controller.h"
#include "laser_correction_controller.h"
#include "robot_sensor_controller.h"
#include "robot_usb_camera_controller.h"
#include "candle_can_adapter.h"
#include "pcan_can_adapter.h"
#include "services/robot_json_codec.h"
#include "services/workbench_data_codec.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSerialPortInfo>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <cmath>
#include <utility>

namespace pa1664::remote {
namespace {

constexpr int kLaserImageIntervalMs = 33;
constexpr int kLaserPointIntervalMs = 50;
constexpr int kUsbImageIntervalMs = 66;
constexpr int kImuIntervalMs = 50;
constexpr int kCorrectionStatusIntervalMs = 100;
constexpr int kLaserEventIntervalMs = 100;

int motorIndexOrInvalid(const crawling::DriveSettings& settings, int axis,
                        int index) {
  if (axis < 0 || axis >= 3 || index < 0 ||
      index >= settings.clampMotors[axis].size()) {
    return -1;
  }
  return axis * 1000 + index;
}

QJsonArray clampPositionsToJson(const QHash<int, double>& positions) {
  QJsonArray array;
  QList<int> keys = positions.keys();
  std::sort(keys.begin(), keys.end());
  for (int key : keys) {
    array.append(QJsonObject{{QStringLiteral("axis"), key / 1000},
                             {QStringLiteral("index"), key % 1000},
                             {QStringLiteral("degrees"), positions.value(key)}});
  }
  return array;
}

QJsonArray boolArray(const bool* values, int count) {
  QJsonArray array;
  for (int index = 0; index < count; ++index) array.append(values[index]);
  return array;
}

QJsonObject enumerateHostPorts() {
  QJsonArray ports;
  QStringList seen;
  const auto appendPort = [&ports, &seen](const QString& id,
                                          const QString& label,
                                          const QString& description,
                                          const QString& kind) {
    if (id.isEmpty() || seen.contains(id)) return;
    seen.append(id);
    ports.append(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label.isEmpty() ? id : label},
        {QStringLiteral("description"), description},
        {QStringLiteral("kind"), kind}});
  };
  for (const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
    appendPort(port.portName(), port.portName(), port.description(),
               QStringLiteral("serial"));
  }
  for (const auto& port : crawling::CandleCanAdapter::availablePorts()) {
    appendPort(port.id, port.name,
               QStringLiteral("原生 candleLight USB CAN"),
               QStringLiteral("can"));
  }
  QString pcanError;
  for (const auto& port :
       crawling::PcanCanAdapter::availablePorts(&pcanError)) {
    appendPort(port.id, port.name, port.description, QStringLiteral("can"));
  }
  return QJsonObject{{QStringLiteral("ports"), ports},
                     {QStringLiteral("pcanError"), pcanError}};
}

bool validSession(quint64 sessionId) { return sessionId != 0; }

}  // namespace

RobotPeripheralMediaEncoder::RobotPeripheralMediaEncoder(QObject* parent)
    : QObject(parent), timer_(new QTimer(this)) {
  timer_->setTimerType(Qt::PreciseTimer);
  timer_->setInterval(2);
  connect(timer_, &QTimer::timeout, this,
          &RobotPeripheralMediaEncoder::encodePending);
}

void RobotPeripheralMediaEncoder::setActive(bool active) {
  active_ = active;
  if (!active_) {
    if (timer_) timer_->stop();
    laserImage_ = QImage();
    laserPoints_.clear();
    for (QImage& image : usbImages_) image = QImage();
    imuSample_ = crawling::ImuSample();
    laserImagePending_ = false;
    laserPointsPending_ = false;
    imuPending_ = false;
    for (bool& pending : usbImagePending_) pending = false;
    return;
  }
  schedule();
}

void RobotPeripheralMediaEncoder::submitLaserImage(const QImage& image) {
  if (!active_ || image.isNull()) return;
  laserImage_ = image;
  laserImagePending_ = true;
  schedule();
}

void RobotPeripheralMediaEncoder::submitLaserPoints(
    const QVector<QVector3D>& points) {
  if (!active_ || points.isEmpty()) return;
  laserPoints_ = points;
  laserPointsPending_ = true;
  schedule();
}

void RobotPeripheralMediaEncoder::submitUsbImage(int slot,
                                                  const QImage& image) {
  if (!active_ || slot < 0 || slot >= crawling::kUsbCameraChannelCount ||
      image.isNull()) {
    return;
  }
  usbImages_[slot] = image;
  usbImagePending_[slot] = true;
  schedule();
}

void RobotPeripheralMediaEncoder::submitImu(
    const crawling::ImuSample& sample) {
  if (!active_) return;
  imuSample_ = sample;
  imuPending_ = true;
  schedule();
}

void RobotPeripheralMediaEncoder::shutdown() {
  active_ = false;
  if (timer_) timer_->stop();
  laserImagePending_ = false;
  laserPointsPending_ = false;
  imuPending_ = false;
  for (bool& pending : usbImagePending_) pending = false;
}

void RobotPeripheralMediaEncoder::schedule() {
  if (!active_ || !timer_) return;
  const bool pending = laserImagePending_ || laserPointsPending_ || imuPending_ ||
                       usbImagePending_[0] || usbImagePending_[1];
  if (pending && !timer_->isActive()) timer_->start();
}

bool RobotPeripheralMediaEncoder::intervalElapsed(qint64* lastMs,
                                                   qint64 intervalMs,
                                                   qint64 nowMs) {
  if (!lastMs || nowMs - *lastMs < intervalMs) return false;
  *lastMs = nowMs;
  return true;
}

void RobotPeripheralMediaEncoder::encodePending() {
  if (!active_) {
    if (timer_) timer_->stop();
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (laserImagePending_ &&
      intervalElapsed(&lastLaserImageMs_, kLaserImageIntervalMs, now)) {
    laserImagePending_ = false;
    const QByteArray payload =
        encodeImagePayload(laserImage_, 78);
    laserImage_ = QImage();
    if (!payload.isEmpty() && payload.size() <= kMaxMediaMessageBytes) {
      emit mediaReady(kMediaStreamLaserImage, MediaStreamKind::LaserImage,
                      payload);
    }
  }

  if (laserPointsPending_ &&
      intervalElapsed(&lastLaserPointsMs_, kLaserPointIntervalMs, now)) {
    laserPointsPending_ = false;
    const QByteArray payload = encodePointCloud(laserPoints_);
    laserPoints_ = QVector<QVector3D>();
    if (!payload.isEmpty() && payload.size() <= kMaxMediaMessageBytes) {
      emit mediaReady(kMediaStreamLaserPointCloud,
                      MediaStreamKind::LaserPointCloud, payload);
    } else if (!payload.isEmpty()) {
      emit logMessage(QStringLiteral("激光点云帧超过媒体通道上限，已丢弃"));
    }
  }

  for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
    if (!usbImagePending_[slot] ||
        !intervalElapsed(&lastUsbImageMs_[slot], kUsbImageIntervalMs, now)) {
      continue;
    }
    usbImagePending_[slot] = false;
    const QByteArray payload = encodeImagePayload(usbImages_[slot], 72);
    usbImages_[slot] = QImage();
    if (!payload.isEmpty() && payload.size() <= kMaxMediaMessageBytes) {
      emit mediaReady(static_cast<quint16>(kMediaStreamUsbCameraBase + slot),
                      MediaStreamKind::UsbCameraImage, payload);
    }
  }

  if (imuPending_ && intervalElapsed(&lastImuMs_, kImuIntervalMs, now)) {
    imuPending_ = false;
    const QByteArray payload = encodeImuSample(imuSample_);
    if (!payload.isEmpty()) {
      emit mediaReady(kMediaStreamImu, MediaStreamKind::ImuSample, payload);
    }
  }

  const bool pending = laserImagePending_ || laserPointsPending_ || imuPending_ ||
                       usbImagePending_[0] || usbImagePending_[1];
  if (!pending && timer_) timer_->stop();
}


RobotPeripheralHostService::RobotPeripheralHostService(
    RobotHostService* drive, QObject* parent)
    : QObject(parent), drive_(drive) {
  qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
  qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
  qRegisterMetaType<crawling::ImuSample>("crawling::ImuSample");
  qRegisterMetaType<crawling::LaserCorrectionSettings>(
      "crawling::LaserCorrectionSettings");
  qRegisterMetaType<crawling::LaserCorrectionStatus>(
      "crawling::LaserCorrectionStatus");
  qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");
  QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                       QSettings::IniFormat);
  settings_ = crawling::DriveSettings::load(persistent);
}

RobotPeripheralHostService::~RobotPeripheralHostService() { shutdown(); }

QJsonObject RobotPeripheralHostService::snapshot() const {
  QJsonObject data;
  data.insert(QStringLiteral("ownerSessionId"),
              QString::number(ownerSessionId_));
  data.insert(QStringLiteral("driveConnected"), driveConnected_);
  data.insert(QStringLiteral("driveState"),
              static_cast<int>(lastTelemetry_.state));
  data.insert(QStringLiteral("driveStateKey"),
              driveStateKey(lastTelemetry_.state));
  data.insert(QStringLiteral("imuConnected"), imuConnected_);
  data.insert(QStringLiteral("laserConnected"), laserConnected_);
  data.insert(QStringLiteral("usbConnected"),
              boolArray(usbConnected_, crawling::kUsbCameraChannelCount));
  data.insert(QStringLiteral("clampConnected"), clampConnected_);
  data.insert(QStringLiteral("clampStatus"), clampStatus_);
  data.insert(QStringLiteral("clampPositions"),
              clampPositionsToJson(clampPositions_));
  data.insert(QStringLiteral("laserDevices"),
              QJsonArray::fromStringList(laserDevices_));
  data.insert(QStringLiteral("usbDevices"),
              QJsonArray::fromStringList(usbDevices_));
  data.insert(QStringLiteral("settings"), driveSettingsToJson(settings_));
  data.insert(QStringLiteral("correctionStatus"),
              laserCorrectionStatusToJson(lastCorrectionStatus_));
  data.insert(QStringLiteral("correctionStartPending"),
              correctionStartPending_);
  data.insert(QStringLiteral("correctionActive"), correctionActive_);
  data.insert(QStringLiteral("correctionPaused"), correctionPaused_);
  data.insert(QStringLiteral("mediaReceptionActive"), mediaReceptionActive_);
  return data;
}

void RobotPeripheralHostService::initialize() {
  if (initialized_) return;
  initialized_ = true;
  shuttingDown_ = false;
  QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                       QSettings::IniFormat);
  settings_ = crawling::DriveSettings::load(persistent);
  startPortEnumerationThread();
  startSensorThreads();
  appendLog(QStringLiteral("小车外设后台服务已启动"));
}


void RobotPeripheralHostService::startPortEnumerationThread() {
  if (portEnumerationThread_) return;
  ++portEnumerationGeneration_;
  portEnumerationThread_ = new QThread(this);
  portEnumerationThread_->setObjectName(
      QStringLiteral("RobotPortEnumeration"));
  portEnumerationWorker_ = new QObject;
  portEnumerationWorker_->moveToThread(portEnumerationThread_);
  connect(portEnumerationThread_, &QThread::finished,
          portEnumerationWorker_, &QObject::deleteLater);
  portEnumerationThread_->start(QThread::LowPriority);
}

void RobotPeripheralHostService::stopPortEnumerationThread() {
  ++portEnumerationGeneration_;
  portEnumerationPending_ = false;
  portEnumerationRequests_.clear();
  if (!portEnumerationThread_) {
    portEnumerationWorker_ = nullptr;
    return;
  }
  portEnumerationThread_->requestInterruption();
  portEnumerationThread_->quit();
  if (!portEnumerationThread_->wait(10000)) {
    qWarning() << "ROBOT_PORT_ENUMERATION_SHUTDOWN_TIMEOUT";
    portEnumerationThread_->terminate();
    portEnumerationThread_->wait();
  }
  portEnumerationWorker_ = nullptr;
  delete portEnumerationThread_;
  portEnumerationThread_ = nullptr;
}

void RobotPeripheralHostService::startSensorThreads() {
  if (!encoderThread_) {
    encoderThread_ = new QThread(this);
    encoderThread_->setObjectName(QStringLiteral("RobotPeripheralMedia"));
    encoder_ = new RobotPeripheralMediaEncoder;
    encoder_->moveToThread(encoderThread_);
    connect(encoderThread_, &QThread::finished, encoder_,
            &QObject::deleteLater);
    connect(encoder_, &RobotPeripheralMediaEncoder::logMessage, this,
            [this](const QString& message) {
              if (ownerSessionId_ != 0) appendLog(message);
            }, Qt::QueuedConnection);
    connect(encoder_, &RobotPeripheralMediaEncoder::mediaReady, this,
            &RobotPeripheralHostService::mediaReady, Qt::QueuedConnection);
    encoderThread_->start(QThread::HighPriority);
  }

  if (!sensorThread_) {
    sensorThread_ = new QThread(this);
    sensorThread_->setObjectName(QStringLiteral("RobotSensors"));
    sensor_ = new crawling::RobotSensorController;
    sensor_->moveToThread(sensorThread_);
    connect(sensorThread_, &QThread::finished, sensor_,
            &QObject::deleteLater);

    const auto submitImage = [this](const QImage& image) {
      if (ownerSessionId_ == 0 || !mediaReceptionActive_ || !encoder_) return;
      QMetaObject::invokeMethod(
          encoder_, [encoder = encoder_, image] { encoder->submitLaserImage(image); },
          Qt::QueuedConnection);
    };
    const auto submitPoints = [this](const QVector<QVector3D>& points) {
      if (ownerSessionId_ == 0 || !mediaReceptionActive_ || !encoder_) return;
      QMetaObject::invokeMethod(
          encoder_, [encoder = encoder_, points] { encoder->submitLaserPoints(points); },
          Qt::QueuedConnection);
    };
    const auto publishLaserFrameEvent = [this](const QString& source) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (ownerSessionId_ == 0 ||
          now - lastLaserEventMs_ < kLaserEventIntervalMs) {
        return;
      }
      lastLaserEventMs_ = now;
      ++laserFrameSequence_;
      emit eventReady(
          ownerSessionId_, QStringLiteral("robot.laser.frame"),
          QJsonObject{{QStringLiteral("sequence"),
                       static_cast<double>(laserFrameSequence_)},
                      {QStringLiteral("width"), lastLaserImageWidth_},
                      {QStringLiteral("height"), lastLaserImageHeight_},
                      {QStringLiteral("pointCount"), lastLaserPointCount_},
                      {QStringLiteral("source"), source}});
    };

    connect(sensor_, &crawling::RobotSensorController::imuConnectionChanged,
            this, [this](bool connected, const QString& message) {
              imuConnected_ = connected;
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.imu.connection"),
                  QJsonObject{{QStringLiteral("connected"), connected},
                              {QStringLiteral("message"), message}});
              appendLog(message);
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::imuSampleChanged,
            this, [this](const crawling::ImuSample& sample) {
              if (ownerSessionId_ == 0 || !mediaReceptionActive_ || !encoder_) {
                return;
              }
              QMetaObject::invokeMethod(
                  encoder_,
                  [encoder = encoder_, sample] { encoder->submitImu(sample); },
                  Qt::QueuedConnection);
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::cameraDevicesChanged,
            this, [this](const QStringList& devices) {
              laserDevices_ = devices;
              emit eventReady(ownerSessionId_,
                              QStringLiteral("robot.laser.devices"),
                              QJsonObject{{QStringLiteral("devices"),
                                           QJsonArray::fromStringList(devices)}});
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::cameraConnectionChanged,
            this, [this](bool connected, const QString& message) {
              laserConnected_ = connected;
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.laser.connection"),
                  QJsonObject{{QStringLiteral("connected"), connected},
                              {QStringLiteral("message"), message}});
              appendLog(message);
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::deviceDetectionChanged,
            this, [this](bool running, const QString& message) {
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.laser.scanning"),
                  QJsonObject{{QStringLiteral("running"), running},
                              {QStringLiteral("message"), message}});
              if (!message.isEmpty()) appendLog(message);
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::sensorSettingsDetected,
            this, [this](const QString& imuPort, int baudRate,
                         const QString& laserSerial) {
              emit eventReady(
                  ownerSessionId_,
                  QStringLiteral("robot.sensorSettingsDetected"),
                  QJsonObject{{QStringLiteral("imuPort"), imuPort},
                              {QStringLiteral("imuBaudRate"), baudRate},
                              {QStringLiteral("laserSerialNumber"), laserSerial}});
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::cameraFrameChanged, this,
            [this](quint32, quint32 width, quint32 height, quint64 points) {
              lastLaserImageWidth_ = static_cast<int>(width);
              lastLaserImageHeight_ = static_cast<int>(height);
              lastLaserPointCount_ = static_cast<int>(points);
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::cameraImageFrameChanged,
            this, [submitImage, publishLaserFrameEvent](const QImage& image) {
              submitImage(image);
              publishLaserFrameEvent(QStringLiteral("camera"));
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::pointCloudProfileChanged,
            this, [this, submitPoints, publishLaserFrameEvent](
                      const QVector<QVector3D>& points) {
              lastLaserPointCount_ = points.size();
              submitPoints(points);
              publishLaserFrameEvent(QStringLiteral("profile"));
            }, Qt::QueuedConnection);
    connect(sensor_,
            &crawling::RobotSensorController::correctionCameraFrameReady, this,
            [submitImage, publishLaserFrameEvent](
                const QImage& image, quint32, qint64) {
              submitImage(image);
              publishLaserFrameEvent(QStringLiteral("correctionCamera"));
            }, Qt::QueuedConnection);
    connect(sensor_,
            &crawling::RobotSensorController::correctionProfileFrameReady, this,
            [this, submitPoints, publishLaserFrameEvent](
                const QVector<QVector3D>& points, quint32, qint64) {
              lastLaserPointCount_ = points.size();
              submitPoints(points);
              publishLaserFrameEvent(QStringLiteral("correctionProfile"));
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::correctionProfilePrepared,
            this, [this](bool ready) {
              if (!correctionStartPending_) return;
              correctionStartPending_ = false;
              if (!ready || ownerSessionId_ == 0 || !correction_) {
                correctionActive_ = false;
                correctionPaused_ = false;
                if (ownerSessionId_ != 0) {
                  emit eventReady(
                      ownerSessionId_,
                      QStringLiteral("robot.autoCorrection.failed"),
                      QJsonObject{{QStringLiteral("reason"),
                                   QStringLiteral("激光轮廓准备失败")}});
                }
                return;
              }
              correctionActive_ = true;
              correctionPaused_ = false;
              QMetaObject::invokeMethod(
                  correction_, "setPaused", Qt::QueuedConnection,
                  Q_ARG(bool, false));
              QMetaObject::invokeMethod(
                  correction_, "setEnabled", Qt::QueuedConnection,
                  Q_ARG(bool, true));
              emit eventReady(ownerSessionId_,
                              QStringLiteral("robot.autoCorrection.started"),
                              QJsonObject{{QStringLiteral("reason"),
                                           QStringLiteral("profile_ready")}});
            }, Qt::QueuedConnection);
    connect(sensor_, &crawling::RobotSensorController::logMessage, this,
            [this](const QString& message) { appendLog(message); },
            Qt::QueuedConnection);
    sensorThread_->start(QThread::HighPriority);
  }


  if (!cameraThread_) {
    cameraThread_ = new QThread(this);
    cameraThread_->setObjectName(QStringLiteral("RobotUsbCameras"));
    camera_ = new crawling::RobotUsbCameraController;
    camera_->moveToThread(cameraThread_);
    connect(cameraThread_, &QThread::finished, camera_,
            &QObject::deleteLater);
    connect(camera_, &crawling::RobotUsbCameraController::devicesChanged, this,
            [this](const QStringList& devices) {
              usbDevices_ = devices;
              emit eventReady(ownerSessionId_,
                              QStringLiteral("robot.camera.devices"),
                              QJsonObject{{QStringLiteral("devices"),
                                           QJsonArray::fromStringList(devices)}});
            }, Qt::QueuedConnection);
    connect(camera_, &crawling::RobotUsbCameraController::connectionChanged,
            this, [this](int slot, bool connected, const QString& message) {
              if (slot < 0 || slot >= crawling::kUsbCameraChannelCount) return;
              usbConnected_[slot] = connected;
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.camera.connection"),
                  QJsonObject{{QStringLiteral("slot"), slot},
                              {QStringLiteral("connected"), connected},
                              {QStringLiteral("message"), message}});
              appendLog(message);
            }, Qt::QueuedConnection);
    connect(camera_, &crawling::RobotUsbCameraController::frameChanged, this,
            [this](int slot, const QImage& image) {
              if (slot < 0 || slot >= crawling::kUsbCameraChannelCount ||
                  ownerSessionId_ == 0 || !mediaReceptionActive_ || !encoder_) {
                return;
              }
              ++cameraFrameSequence_[slot];
              QMetaObject::invokeMethod(
                  encoder_,
                  [encoder = encoder_, slot, image] {
                    encoder->submitUsbImage(slot, image);
                  },
                  Qt::QueuedConnection);
            }, Qt::QueuedConnection);
    connect(camera_, &crawling::RobotUsbCameraController::logMessage, this,
            [this](const QString& message) { appendLog(message); },
            Qt::QueuedConnection);
    cameraThread_->start(QThread::NormalPriority);
  }

  if (!clampThread_) {
    clampThread_ = new QThread(this);
    clampThread_->setObjectName(QStringLiteral("RobotClampMotors"));
    clamp_ = new crawling::ClampMotorController;
    clamp_->moveToThread(clampThread_);
    connect(clampThread_, &QThread::finished, clamp_,
            &QObject::deleteLater);
    connect(clamp_, &crawling::ClampMotorController::connectionChanged, this,
            [this](bool connected, const QString& message) {
              clampConnected_ = connected;
              clampStatus_ = message;
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.clamp.connection"),
                  QJsonObject{{QStringLiteral("connected"), connected},
                              {QStringLiteral("message"), message}});
              appendLog(message);
            }, Qt::QueuedConnection);
    connect(clamp_, &crawling::ClampMotorController::statusChanged, this,
            [this](const QString& message) {
              clampStatus_ = message;
              emit eventReady(ownerSessionId_,
                              QStringLiteral("robot.clamp.status"),
                              QJsonObject{{QStringLiteral("message"), message}});
            }, Qt::QueuedConnection);
    connect(clamp_, &crawling::ClampMotorController::positionReceived, this,
            [this](int axis, int index, double degrees) {
              const int key = motorIndexOrInvalid(settings_, axis, index);
              if (key < 0) return;
              clampPositions_.insert(key, degrees);
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.clamp.position"),
                  QJsonObject{{QStringLiteral("axis"), axis},
                              {QStringLiteral("index"), index},
                              {QStringLiteral("degrees"), degrees}});
            }, Qt::QueuedConnection);
    connect(clamp_, &crawling::ClampMotorController::limitReached, this,
            [this](int axis, int index, bool upper) {
              emit eventReady(
                  ownerSessionId_, QStringLiteral("robot.clamp.limitReached"),
                  QJsonObject{{QStringLiteral("axis"), axis},
                              {QStringLiteral("index"), index},
                              {QStringLiteral("upper"), upper}});
            }, Qt::QueuedConnection);
    connect(clamp_, &crawling::ClampMotorController::settingsChanged, this,
            [this] {
              if (!clamp_) return;
              const crawling::DriveSettings current = clamp_->settings();
              settings_.clampMotors = current.clampMotors;
              settings_.clampMotorSyncEnabled = current.clampMotorSyncEnabled;
              emit eventReady(
                  ownerSessionId_,
                  QStringLiteral("robot.clamp.settingsChanged"),
                  QJsonObject{{QStringLiteral("settings"),
                               driveSettingsToJson(settings_)}});
            }, Qt::QueuedConnection);
    clampThread_->start(QThread::LowPriority);
  }

  if (clamp_) {
    QMetaObject::invokeMethod(
        clamp_,
        [controller = clamp_, settings = settings_] {
          controller->setSettings(settings);
        },
        Qt::QueuedConnection);
  }

  if (!correctionThread_) {
    correctionThread_ = new QThread(this);
    correctionThread_->setObjectName(QStringLiteral("RobotAutoCorrection"));
    correction_ = new crawling::LaserCorrectionController;
    correction_->moveToThread(correctionThread_);
    connect(correctionThread_, &QThread::finished, correction_,
            &QObject::deleteLater);
    connect(correction_, &crawling::LaserCorrectionController::commandChanged,
            this, [this](double linearMps, double angularRadps) {
              if (!correctionActive_ || correctionPaused_ ||
                  ownerSessionId_ == 0) {
                return;
              }
              emit driveCorrectionCommand(linearMps, angularRadps);
            }, Qt::QueuedConnection);
    connect(correction_, &crawling::LaserCorrectionController::statusChanged,
            this, [this](const crawling::LaserCorrectionStatus& status) {
              lastCorrectionStatus_ = status;
              if (!status.active && correctionActive_) {
                correctionActive_ = false;
                correctionPaused_ = false;
                if (sensor_) {
                  QMetaObject::invokeMethod(
                      sensor_, "restoreOriginalPreview", Qt::QueuedConnection);
                }
                emit eventReady(
                    ownerSessionId_,
                    QStringLiteral("robot.autoCorrection.stopped"),
                    QJsonObject{{QStringLiteral("reason"),
                                 status.reason.isEmpty()
                                     ? QStringLiteral("controller_stopped")
                                     : status.reason}});
              }
              publishCorrectionStatus();
            }, Qt::QueuedConnection);
    connect(correction_, &crawling::LaserCorrectionController::logMessage, this,
            [this](const QString& message) { appendLog(message); },
            Qt::QueuedConnection);
    connect(correction_,
            &crawling::LaserCorrectionController::trackingPhaseStarted, this,
            [this] {
              if (ownerSessionId_ != 0 && correctionActive_) {
                emit eventReady(
                    ownerSessionId_,
                    QStringLiteral("robot.autoCorrection.trackingStarted"),
                    QJsonObject{{QStringLiteral("reason"),
                                 QStringLiteral("tracking_phase")}});
              }
            }, Qt::QueuedConnection);
    connect(correction_,
            &crawling::LaserCorrectionController::calibrationCompleted, this,
            [this](double offset) {
              emit eventReady(
                  ownerSessionId_,
                  QStringLiteral("robot.autoCorrection.calibrated"),
                  QJsonObject{{QStringLiteral("offset"), offset}});
            }, Qt::QueuedConnection);
    correctionThread_->start(QThread::HighPriority);
  }
}


void RobotPeripheralHostService::stopSensorThreads() {
  // Queue every stop before waiting. If one driver is wedged, the other
  // devices still receive their release calls and the main thread only
  // performs bounded waits instead of an unbounded BlockingQueuedConnection.
  const auto queueWorkerStop = [](QThread* thread, QObject* object,
                                  const std::function<void()>& stop) {
    if (!thread) return;
    thread->requestInterruption();
    bool queued = false;
    if (object && thread->isRunning()) {
      QThread* const threadPtr = thread;
      const std::function<void()> stopCopy = stop;
      queued = QMetaObject::invokeMethod(
          object,
          [threadPtr, stopCopy] {
            stopCopy();
            threadPtr->quit();
          },
          Qt::QueuedConnection);
    }
    if (!queued) thread->quit();
  };
  const auto finishThread = [](QThread* thread) {
    if (!thread) return;
    if (!thread->wait(10000)) {
      qWarning() << "ROBOT_PERIPHERAL_THREAD_SHUTDOWN_TIMEOUT"
                 << thread->objectName();
      thread->requestInterruption();
      thread->quit();
      if (!thread->wait(2000)) {
        qWarning() << "ROBOT_PERIPHERAL_THREAD_TERMINATE"
                   << thread->objectName();
        thread->terminate();
        thread->wait();
      }
    }
    thread->wait(1000);
    delete thread;
  };

  queueWorkerStop(sensorThread_, sensor_,
                  [controller = sensor_] { controller->shutdown(); });
  queueWorkerStop(cameraThread_, camera_,
                  [controller = camera_] { controller->shutdown(); });
  queueWorkerStop(clampThread_, clamp_, [controller = clamp_] {
    controller->stop();
    controller->disconnectDevice();
  });
  queueWorkerStop(correctionThread_, correction_, [controller = correction_] {
    controller->setEnabled(false);
    controller->shutdown();
  });
  queueWorkerStop(encoderThread_, encoder_,
                  [encoder = encoder_] { encoder->shutdown(); });

  finishThread(sensorThread_);
  sensorThread_ = nullptr;
  sensor_ = nullptr;
  finishThread(cameraThread_);
  cameraThread_ = nullptr;
  camera_ = nullptr;
  finishThread(clampThread_);
  clampThread_ = nullptr;
  clamp_ = nullptr;
  finishThread(correctionThread_);
  correctionThread_ = nullptr;
  correction_ = nullptr;
  finishThread(encoderThread_);
  encoderThread_ = nullptr;
  encoder_ = nullptr;
  mediaReceptionActive_ = false;
  imuConnected_ = false;
  laserConnected_ = false;
  clampConnected_ = false;
  for (bool& connected : usbConnected_) connected = false;
}

void RobotPeripheralHostService::shutdown() {
  if (shuttingDown_) return;
  shuttingDown_ = true;
  const quint64 closingOwner = ownerSessionId_;
  if (correction_ || correctionStartPending_) {
    stopAutoCorrection(QStringLiteral("host_shutdown"), false);
  }
  // stopSensorThreads() queues the clamp release together with the other
  // worker shutdowns, so do not post a duplicate command here.
  stopSensorThreads();
  stopPortEnumerationThread();
  if (closingOwner != 0) {
    emit eventReady(closingOwner,
                    QStringLiteral("host.peripheralsReleased"),
                    QJsonObject{{QStringLiteral("reason"),
                                 QStringLiteral("host_shutdown")}});
  }
  ownerSessionId_ = 0;
  initialized_ = false;
}

bool RobotPeripheralHostService::claimSession(quint64 sessionId) {
  if (!validSession(sessionId) || shuttingDown_) return false;
  if (ownerSessionId_ == 0) {
    ownerSessionId_ = sessionId;
    emit eventReady(ownerSessionId_,
                    QStringLiteral("host.peripheralsClaimed"), snapshot());
    return true;
  }
  return ownerSessionId_ == sessionId;
}

void RobotPeripheralHostService::releaseSession(quint64 sessionId) {
  if (!validSession(sessionId) || sessionId != ownerSessionId_) return;
  stopAutoCorrection(QStringLiteral("control_connection_closed"), true);
  stopClampHardware();
  if (sensor_ && sensorThread_ && sensorThread_->isRunning()) {
    QMetaObject::invokeMethod(sensor_, "disconnectImu", Qt::QueuedConnection);
    QMetaObject::invokeMethod(sensor_, "disconnectCamera",
                              Qt::QueuedConnection);
  }
  if (camera_ && cameraThread_ && cameraThread_->isRunning()) {
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
      QMetaObject::invokeMethod(camera_, "disconnectCamera",
                                Qt::QueuedConnection, Q_ARG(int, slot));
    }
  }
  if (encoder_) {
    QMetaObject::invokeMethod(encoder_, "setActive", Qt::QueuedConnection,
                              Q_ARG(bool, false));
  }
  mediaReceptionActive_ = false;
  ++portEnumerationGeneration_;
  portEnumerationPending_ = false;
  portEnumerationRequests_.clear();
  emit eventReady(sessionId, QStringLiteral("host.peripheralsReleased"),
                  QJsonObject{{QStringLiteral("reason"),
                               QStringLiteral("control_connection_closed")}});
  ownerSessionId_ = 0;
}

void RobotPeripheralHostService::handleRequest(quint64 sessionId,
                                                quint64 requestId,
                                                const QJsonObject& request) {
  if (!acceptOwner(sessionId, requestId)) return;
  const QString operation =
      request.value(QStringLiteral("operation")).toString().trimmed();
  const QJsonObject data = request.value(QStringLiteral("data")).toObject();
  if (operation == QStringLiteral("robot.peripherals.snapshot") ||
      operation == QStringLiteral("robot.sensors.snapshot")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  if (operation == QStringLiteral("robot.peripherals.serialPorts")) {
    if (!portEnumerationThread_ || !portEnumerationWorker_ ||
        !portEnumerationThread_->isRunning()) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("port_enumeration_unavailable"),
              QStringLiteral("小车主机端口枚举服务不可用，请重启主机程序"));
      return;
    }
    if (portEnumerationPending_) {
      // The chassis and probe pages refresh together after reconnect.  Coalesce
      // them into one hardware scan rather than showing a spurious failure.
      if (portEnumerationRequests_.size() >= 16) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("port_enumeration_queue_full"),
                QStringLiteral("小车主机端口刷新请求过多，请稍后重试"));
      } else {
        portEnumerationRequests_.append(
        PortEnumerationRequest{sessionId, requestId});
      }
      return;
    }

    portEnumerationPending_ = true;
    portEnumerationRequests_.clear();
    portEnumerationRequests_.append(
            PortEnumerationRequest{sessionId, requestId});
    const quint64 generation = ++portEnumerationGeneration_;
    const bool queued = QMetaObject::invokeMethod(
        portEnumerationWorker_,
        [this, sessionId, generation] {
          const QJsonObject result = enumerateHostPorts();
          QMetaObject::invokeMethod(
              this,
              [this, sessionId, generation, result] {
                if (generation != portEnumerationGeneration_) return;
                const QVector<PortEnumerationRequest> requests =
                    portEnumerationRequests_;
                portEnumerationRequests_.clear();
                portEnumerationPending_ = false;
                if (shuttingDown_ || ownerSessionId_ != sessionId) return;
                for (const PortEnumerationRequest& request : requests) {
                  respond(request.sessionId, request.requestId, true, result);
                }
              },
              Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    if (!queued) {
      const QVector<PortEnumerationRequest> requests =
          portEnumerationRequests_;
      portEnumerationRequests_.clear();
      portEnumerationPending_ = false;
      for (const PortEnumerationRequest& request : requests) {
        respond(request.sessionId, request.requestId, false, {},
                QStringLiteral("port_enumeration_unavailable"),
                QStringLiteral("无法启动小车主机端口枚举任务"));
      }
    }
    return;
  }
  if (operation.startsWith(QStringLiteral("robot.imu."))) {
    handleImuRequest(sessionId, requestId, operation, data);
    return;
  }
  if (operation.startsWith(QStringLiteral("robot.laser.")) ||
      operation == QStringLiteral("robot.sensors.scan")) {
    handleLaserRequest(sessionId, requestId, operation, data);
    return;
  }
  if (operation.startsWith(QStringLiteral("robot.camera."))) {
    handleCameraRequest(sessionId, requestId, operation, data);
    return;
  }
  if (operation.startsWith(QStringLiteral("robot.clamp."))) {
    handleClampRequest(sessionId, requestId, operation, data);
    return;
  }
  if (operation.startsWith(QStringLiteral("robot.autoCorrection."))) {
    handleCorrectionRequest(sessionId, requestId, operation, data);
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的机器人外设操作：%1").arg(operation));
}

void RobotPeripheralHostService::processDriveTelemetry(
    const crawling::DriveTelemetry& telemetry) {
  lastTelemetry_ = telemetry;
  if (drive_) driveConnected_ = drive_->isDriveConnected();
  if (correction_ && correctionThread_ && correctionThread_->isRunning()) {
    QMetaObject::invokeMethod(
        correction_,
        [controller = correction_, telemetry] {
          controller->processDriveTelemetry(telemetry);
        },
        Qt::QueuedConnection);
  }
  if ((correctionActive_ || correctionStartPending_) &&
      (!driveConnected_ ||
       telemetry.state != crawling::DriveState::Enabled)) {
    interruptAutoCorrection(
        QStringLiteral("底盘状态不满足自动纠偏安全条件"));
  }
}

void RobotPeripheralHostService::mediaReceptionActive(bool active) {
  mediaReceptionActive_ = active && ownerSessionId_ != 0;
  if (encoder_) {
    QMetaObject::invokeMethod(encoder_, "setActive", Qt::QueuedConnection,
                              Q_ARG(bool, mediaReceptionActive_));
  }
}

void RobotPeripheralHostService::interruptAutoCorrection(
    const QString& reason) {
  if (!correctionActive_ && !correctionStartPending_) return;
  stopAutoCorrection(reason, true);
}

void RobotPeripheralHostService::safetyStop(quint64 sessionId,
                                             const QString& reason) {
  if (sessionId == 0 || sessionId != ownerSessionId_) return;
  emit driveCorrectionCommand(0.0, 0.0);
  stopAutoCorrection(reason, true);
  if (clamp_) {
    QMetaObject::invokeMethod(clamp_, "stop", Qt::QueuedConnection);
  }
  emit eventReady(sessionId, QStringLiteral("robot.peripheralsSafetyStop"),
                  QJsonObject{{QStringLiteral("reason"), reason}});
}

void RobotPeripheralHostService::respond(
    quint64 sessionId, quint64 requestId, bool ok, const QJsonObject& data,
    const QString& errorCode, const QString& errorText) {
  emit responseReady(sessionId, requestId, ok, data, errorCode, errorText);
}

bool RobotPeripheralHostService::acceptOwner(quint64 sessionId,
                                              quint64 requestId) {
  if (sessionId != 0 && sessionId == ownerSessionId_) return true;
  respond(sessionId, requestId, false, {},
          QStringLiteral("not_session_owner"),
          ownerSessionId_ == 0
              ? QStringLiteral("控制会话尚未建立")
              : QStringLiteral("另一个操作台正在控制小车"));
  return false;
}

void RobotPeripheralHostService::sendEvent(quint64 sessionId,
                                            const QString& name,
                                            const QJsonObject& data) {
  if (sessionId != 0) emit eventReady(sessionId, name, data);
}


void RobotPeripheralHostService::handleImuRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  if (!sensor_ || !sensorThread_ || !sensorThread_->isRunning()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("sensor_service_unavailable"),
            QStringLiteral("IMU 后台线程尚未就绪"));
    return;
  }
  if (operation == QStringLiteral("robot.imu.connect")) {
    const QString port =
        data.value(QStringLiteral("port")).toString().trimmed();
    const int baudRate = data.value(QStringLiteral("baudRate")).toInt(115200);
    const int divider =
        data.value(QStringLiteral("outputDivider")).toInt(10);
    if (port.isEmpty() || baudRate < 1200 || baudRate > 4000000 ||
        divider < 1 || divider > 100) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("IMU 串口、波特率或输出分频参数无效"));
      return;
    }
    settings_.imuSerialPort = port;
    settings_.imuBaudRate = baudRate;
    settings_.imuOutputDivider = divider;
    QMetaObject::invokeMethod(
        sensor_, "connectImu", Qt::QueuedConnection, Q_ARG(QString, port),
        Q_ARG(int, baudRate), Q_ARG(int, divider));
    emit eventReady(sessionId, QStringLiteral("robot.imu.connecting"),
                    QJsonObject{{QStringLiteral("port"), port},
                                {QStringLiteral("baudRate"), baudRate}});
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.imu.disconnect")) {
    QMetaObject::invokeMethod(sensor_, "disconnectImu", Qt::QueuedConnection);
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.imu.status")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的 IMU 操作：%1").arg(operation));
}

void RobotPeripheralHostService::handleLaserRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  if (!sensor_ || !sensorThread_ || !sensorThread_->isRunning()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("sensor_service_unavailable"),
            QStringLiteral("激光后台线程尚未就绪"));
    return;
  }
  if (operation == QStringLiteral("robot.laser.scan") ||
      operation == QStringLiteral("robot.laser.scanDevices")) {
    QMetaObject::invokeMethod(sensor_, "scanCamera", Qt::QueuedConnection);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.sensors.scan")) {
    const QString preferredImu =
        data.value(QStringLiteral("imuPort")).toString(settings_.imuSerialPort);
    const QString preferredLaser =
        data.value(QStringLiteral("laserSerialNumber"))
            .toString(settings_.laserSerialNumber);
    QMetaObject::invokeMethod(
        sensor_, "autoDetectDevices", Qt::QueuedConnection,
        Q_ARG(QString, preferredImu), Q_ARG(QString, preferredLaser));
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.laser.connect")) {
    const QString serial =
        data.value(QStringLiteral("serialNumber")).toString().trimmed();
    settings_.laserSerialNumber = serial;
    QMetaObject::invokeMethod(sensor_, "connectCamera", Qt::QueuedConnection,
                              Q_ARG(QString, serial));
    emit eventReady(sessionId, QStringLiteral("robot.laser.connecting"),
                    QJsonObject{{QStringLiteral("serialNumber"), serial}});
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.laser.disconnect")) {
    QMetaObject::invokeMethod(sensor_, "disconnectCamera",
                              Qt::QueuedConnection);
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.laser.status")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的激光操作：%1").arg(operation));
}

void RobotPeripheralHostService::handleCameraRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  if (!camera_ || !cameraThread_ || !cameraThread_->isRunning()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("camera_service_unavailable"),
            QStringLiteral("USB 摄像头后台线程尚未就绪"));
    return;
  }
  if (operation == QStringLiteral("robot.camera.scan")) {
    QMetaObject::invokeMethod(camera_, "scanDevices", Qt::QueuedConnection);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.camera.connect")) {
    const int slot = data.value(QStringLiteral("slot")).toInt(-1);
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("USB 摄像头通道编号无效"));
      return;
    }
    const QString source =
        data.value(QStringLiteral("source")).toString(QStringLiteral("usb"));
    const bool flipHorizontal =
        data.value(QStringLiteral("flipHorizontal")).toBool(false);
    const bool flipVertical =
        data.value(QStringLiteral("flipVertical")).toBool(false);
    if (source.compare(QStringLiteral("network"), Qt::CaseInsensitive) == 0) {
      const QString url =
          data.value(QStringLiteral("url")).toString().trimmed();
      if (url.isEmpty()) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("invalid_argument"),
                QStringLiteral("网络摄像头地址为空"));
        return;
      }
      QMetaObject::invokeMethod(
          camera_, "connectNetworkCamera", Qt::QueuedConnection,
          Q_ARG(int, slot), Q_ARG(QString, url),
          Q_ARG(bool, flipHorizontal), Q_ARG(bool, flipVertical));
    } else {
      const int deviceIndex =
          data.value(QStringLiteral("deviceIndex")).toInt(-1);
      const int fps = qBound(1, data.value(QStringLiteral("fps")).toInt(15),
                             60);
      if (deviceIndex < 0) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("invalid_argument"),
                QStringLiteral("USB 摄像头设备编号无效"));
        return;
      }
      QMetaObject::invokeMethod(
          camera_, "connectCamera", Qt::QueuedConnection, Q_ARG(int, slot),
          Q_ARG(int, deviceIndex), Q_ARG(int, fps),
          Q_ARG(bool, flipHorizontal), Q_ARG(bool, flipVertical));
    }
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true},
                        {QStringLiteral("slot"), slot}});
    return;
  }
  if (operation == QStringLiteral("robot.camera.disconnect")) {
    const int slot = data.value(QStringLiteral("slot")).toInt(-1);
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("USB 摄像头通道编号无效"));
      return;
    }
    QMetaObject::invokeMethod(camera_, "disconnectCamera",
                              Qt::QueuedConnection, Q_ARG(int, slot));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.camera.disconnectAll")) {
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
      QMetaObject::invokeMethod(camera_, "disconnectCamera",
                                Qt::QueuedConnection, Q_ARG(int, slot));
    }
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.camera.displaySize")) {
    const int slot = data.value(QStringLiteral("slot")).toInt(-1);
    const int width = data.value(QStringLiteral("width")).toInt(0);
    const int height = data.value(QStringLiteral("height")).toInt(0);
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount ||
        width <= 0 || height <= 0 || width > 8192 || height > 8192) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("摄像头显示尺寸参数无效"));
      return;
    }
    QMetaObject::invokeMethod(camera_, "setDisplaySize",
                              Qt::QueuedConnection, Q_ARG(int, slot),
                              Q_ARG(int, width), Q_ARG(int, height));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.camera.status")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的摄像头操作：%1").arg(operation));
}


void RobotPeripheralHostService::handleClampRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  if (operation == QStringLiteral("robot.clamp.settings.get") ||
      operation == QStringLiteral("robot.clamp.status")) {
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  if (!clamp_ || !clampThread_ || !clampThread_->isRunning()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("clamp_service_unavailable"),
            QStringLiteral("夹子电机后台线程尚未就绪"));
    return;
  }

  if (operation == QStringLiteral("robot.clamp.settings.update") ||
      operation == QStringLiteral("robot.clamp.connect")) {
    const QJsonObject settingsJson =
        data.contains(QStringLiteral("iniBase64"))
            ? data
            : data.value(QStringLiteral("settings")).toObject();
    if (!settingsJson.isEmpty()) {
      crawling::DriveSettings requested = settings_;
      QString error;
      if (!driveSettingsFromJson(settingsJson, &requested, &error)) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("invalid_settings"), error);
        return;
      }
      const QString validation = requested.clampValidationError();
      if (!validation.isEmpty()) {
        respond(sessionId, requestId, false, {},
                QStringLiteral("invalid_settings"), validation);
        return;
      }
      applyClampSettings(requested, true);
    }
    if (operation == QStringLiteral("robot.clamp.settings.update")) {
      respond(sessionId, requestId, true, snapshot());
      return;
    }
    const QString validation = settings_.clampValidationError();
    if (!validation.isEmpty()) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_settings"), validation);
      return;
    }
    QMetaObject::invokeMethod(clamp_, "connectDevice", Qt::QueuedConnection);
    emit eventReady(sessionId, QStringLiteral("robot.clamp.connecting"),
                    QJsonObject{});
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }

  if (operation == QStringLiteral("robot.clamp.disconnect")) {
    stopClampHardware();
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.clamp.stop")) {
    QMetaObject::invokeMethod(clamp_, "stop", Qt::QueuedConnection);
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.clamp.setMotorSpeed")) {
    const int axis = data.value(QStringLiteral("axis")).toInt(-1);
    const int index = data.value(QStringLiteral("index")).toInt(-1);
    const int percent = data.value(QStringLiteral("percent")).toInt(0);
    if (motorIndexOrInvalid(settings_, axis, index) < 0 ||
        percent < -100 || percent > 100) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("夹子电机轴、序号或速度无效"));
      return;
    }
    QMetaObject::invokeMethod(
        clamp_, "setMotorSpeed", Qt::QueuedConnection, Q_ARG(int, axis),
        Q_ARG(int, index), Q_ARG(int, percent));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.clamp.stopMotor")) {
    const int axis = data.value(QStringLiteral("axis")).toInt(-1);
    const int index = data.value(QStringLiteral("index")).toInt(-1);
    if (motorIndexOrInvalid(settings_, axis, index) < 0) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("夹子电机轴或序号无效"));
      return;
    }
    QMetaObject::invokeMethod(clamp_, "stopMotor", Qt::QueuedConnection,
                              Q_ARG(int, axis), Q_ARG(int, index));
    respond(sessionId, requestId, true);
    return;
  }
  if (operation == QStringLiteral("robot.clamp.requestPosition")) {
    const int axis = data.value(QStringLiteral("axis")).toInt(-1);
    const int index = data.value(QStringLiteral("index")).toInt(-1);
    if (motorIndexOrInvalid(settings_, axis, index) < 0) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("夹子电机轴或序号无效"));
      return;
    }
    if (!crawling::ClampMotorController::supportsPosition(
            settings_.clampMotors[axis][index])) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("position_unsupported"),
              QStringLiteral("该夹子电机协议不支持位置反馈"));
      return;
    }
    QMetaObject::invokeMethod(clamp_, "requestPosition",
                              Qt::QueuedConnection, Q_ARG(int, axis),
                              Q_ARG(int, index));
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.clamp.moveToPosition")) {
    const int axis = data.value(QStringLiteral("axis")).toInt(-1);
    const int index = data.value(QStringLiteral("index")).toInt(-1);
    const double degrees =
        data.value(QStringLiteral("degrees")).toDouble(qQNaN());
    if (motorIndexOrInvalid(settings_, axis, index) < 0 ||
        !std::isfinite(degrees) || std::abs(degrees) > 1000000.0) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("invalid_argument"),
              QStringLiteral("夹子电机目标位置参数无效"));
      return;
    }
    if (!crawling::ClampMotorController::supportsPosition(
            settings_.clampMotors[axis][index])) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("position_unsupported"),
              QStringLiteral("该夹子电机协议不支持位置控制"));
      return;
    }
    QMetaObject::invokeMethod(
        clamp_, "moveToPosition", Qt::QueuedConnection, Q_ARG(int, axis),
        Q_ARG(int, index), Q_ARG(double, degrees));
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("queued"), true}});
    return;
  }
  if (operation == QStringLiteral("robot.clamp.positions")) {
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("positions"),
                         clampPositionsToJson(clampPositions_)}});
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的夹子操作：%1").arg(operation));
}

void RobotPeripheralHostService::applyClampSettings(
    const crawling::DriveSettings& settings, bool persist) {
  settings_.clampCommunicationMode = settings.clampCommunicationMode;
  settings_.clampSerialPort = settings.clampSerialPort;
  settings_.clampSerialBaudRate = settings.clampSerialBaudRate;
  settings_.clampCanBitrate = settings.clampCanBitrate;
  settings_.clampNodeId = settings.clampNodeId;
  settings_.clampXMotorId = settings.clampXMotorId;
  settings_.clampYMotorId = settings.clampYMotorId;
  settings_.clampZMotorId = settings.clampZMotorId;
  settings_.clampXMotorSign = settings.clampXMotorSign;
  settings_.clampYMotorSign = settings.clampYMotorSign;
  settings_.clampZMotorSign = settings.clampZMotorSign;
  settings_.clampMotors = settings.clampMotors;
  settings_.clampMotorSyncEnabled = settings.clampMotorSyncEnabled;
  clampPositions_.clear();
  if (persist) {
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    settings_.save(persistent);
    persistent.sync();
    if (persistent.status() != QSettings::NoError) {
      appendLog(QStringLiteral("夹子配置写入主机设置文件失败"));
    }
  }
  if (clamp_) {
    const crawling::DriveSettings copy = settings_;
    QMetaObject::invokeMethod(
        clamp_,
        [controller = clamp_, copy] { controller->setSettings(copy); },
        Qt::QueuedConnection);
  }
  emit eventReady(ownerSessionId_,
                  QStringLiteral("robot.clamp.settingsChanged"),
                  QJsonObject{{QStringLiteral("settings"),
                               driveSettingsToJson(settings_)}});
}

void RobotPeripheralHostService::stopClampHardware() {
  if (!clamp_) return;
  QMetaObject::invokeMethod(clamp_, "stop", Qt::QueuedConnection);
  QMetaObject::invokeMethod(clamp_, "disconnectDevice", Qt::QueuedConnection);
  clampStatus_ = QStringLiteral("已请求断开夹子电机");
}


void RobotPeripheralHostService::handleCorrectionRequest(
    quint64 sessionId, quint64 requestId, const QString& operation,
    const QJsonObject& data) {
  if (operation == QStringLiteral("robot.autoCorrection.status")) {
    publishCorrectionStatus();
    respond(sessionId, requestId, true, snapshot());
    return;
  }
  if (operation == QStringLiteral("robot.autoCorrection.start")) {
    startAutoCorrection(sessionId, requestId, data);
    return;
  }
  if (operation == QStringLiteral("robot.autoCorrection.stop")) {
    const bool wasRunning = correctionActive_ || correctionStartPending_;
    stopAutoCorrection(QStringLiteral("operator_stop"), true);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("wasRunning"), wasRunning}});
    return;
  }
  if (operation == QStringLiteral("robot.autoCorrection.pause")) {
    if (!correctionActive_) {
      respond(sessionId, requestId, false, {},
              QStringLiteral("correction_not_active"),
              QStringLiteral("自动纠偏尚未运行，无法切换暂停状态"));
      return;
    }
    const bool paused =
        data.value(QStringLiteral("paused")).toBool(false);
    setAutoCorrectionPaused(paused);
    respond(sessionId, requestId, true,
            QJsonObject{{QStringLiteral("paused"), paused}});
    return;
  }
  respond(sessionId, requestId, false, {},
          QStringLiteral("unsupported_operation"),
          QStringLiteral("不支持的自动纠偏操作：%1").arg(operation));
}

void RobotPeripheralHostService::startAutoCorrection(
    quint64 sessionId, quint64 requestId, const QJsonObject& data) {
  if (!correction_ || !sensor_ || !correctionThread_ ||
      !sensorThread_ || !correctionThread_->isRunning() ||
      !sensorThread_->isRunning()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("correction_service_unavailable"),
            QStringLiteral("自动纠偏后台线程尚未就绪"));
    return;
  }
  if (correctionActive_ || correctionStartPending_) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("correction_busy"),
            QStringLiteral("自动纠偏已经在运行或正在启动"));
    return;
  }
  if (!driveConnected_ || !drive_ || !drive_->isDriveConnected()) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("drive_not_connected"),
            QStringLiteral("底盘尚未连接，不能启动自动纠偏"));
    return;
  }
  if (lastTelemetry_.state != crawling::DriveState::Enabled) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("drive_not_enabled"),
            QStringLiteral("底盘尚未使能，不能启动自动纠偏"));
    return;
  }
  if (!laserConnected_) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("laser_not_connected"),
            QStringLiteral("激光轮廓相机尚未连接，不能启动自动纠偏"));
    return;
  }

  const QJsonObject settingsJson =
      data.contains(QStringLiteral("settings"))
          ? data.value(QStringLiteral("settings")).toObject()
          : data;
  crawling::LaserCorrectionSettings requested;
  QString error;
  if (!laserCorrectionSettingsFromJson(settingsJson, &requested, &error)) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_settings"), error);
    return;
  }

  // The operator may tune perception and gains, but physical geometry and the
  // hard speed envelope remain authoritative on the robot PC.
  requested.wheelRadiusM = settings_.wheelRadiusM;
  requested.trackWidthM = settings_.trackWidthM;
  requested.minimumInnerWheelRatio =
      qBound(0.0, requested.minimumInnerWheelRatio,
             settings_.minimumInnerWheelRatio);
  const double hostSpeedLimit =
      std::min(settings_.maximumLinearSpeedMps,
               settings_.maximumWheelSpeedMps);
  if (!std::isfinite(hostSpeedLimit) || hostSpeedLimit <= 0.0) {
    respond(sessionId, requestId, false, {},
            QStringLiteral("invalid_host_limits"),
            QStringLiteral("主机速度限制无效，拒绝启动自动纠偏"));
    return;
  }
  requested.targetSpeedMps =
      std::min(requested.targetSpeedMps, hostSpeedLimit * 0.8);
  requested.segmentLengthM =
      qBound(0.001, requested.segmentLengthM, 1.0);
  requested.imageTimeoutMs =
      qBound(100, requested.imageTimeoutMs, 5000);

  correctionStartPending_ = true;
  correctionActive_ = false;
  correctionPaused_ = false;
  lastCorrectionStatus_ = crawling::LaserCorrectionStatus();
  lastCorrectionStatus_.active = false;
  lastCorrectionStatus_.paused = false;
  lastCorrectionStatus_.phase = QStringLiteral("preparing");
  lastCorrectionStatus_.reason =
      QStringLiteral("正在准备激光轮廓流");
  QMetaObject::invokeMethod(
      correction_, "setSettings", Qt::QueuedConnection,
      Q_ARG(crawling::LaserCorrectionSettings, requested));
  QMetaObject::invokeMethod(correction_, "setPaused", Qt::QueuedConnection,
                            Q_ARG(bool, false));
  QMetaObject::invokeMethod(correction_, "setEnabled", Qt::QueuedConnection,
                            Q_ARG(bool, false));
  QMetaObject::invokeMethod(sensor_, "prepareCorrectionProfile",
                            Qt::QueuedConnection);
  emit eventReady(
      sessionId, QStringLiteral("robot.autoCorrection.starting"),
      QJsonObject{{QStringLiteral("phase"), QStringLiteral("preparing")},
                  {QStringLiteral("targetSpeedMps"),
                   requested.targetSpeedMps},
                  {QStringLiteral("segmentLengthM"),
                   requested.segmentLengthM}});
  respond(sessionId, requestId, true,
          QJsonObject{{QStringLiteral("state"),
                       QStringLiteral("preparing")}});
  appendLog(QStringLiteral("收到远程自动纠偏启动请求，正在准备激光轮廓流"));
}

void RobotPeripheralHostService::stopAutoCorrection(const QString& reason,
                                                     bool notify) {
  const quint64 owner = ownerSessionId_;
  const bool wasPending = correctionStartPending_;
  const bool wasActive = correctionActive_;
  emit driveCorrectionCommand(0.0, 0.0);
  correctionStartPending_ = false;
  correctionActive_ = false;
  correctionPaused_ = false;
  if (correction_ && correctionThread_ && correctionThread_->isRunning()) {
    QMetaObject::invokeMethod(correction_, "setPaused", Qt::QueuedConnection,
                              Q_ARG(bool, false));
    QMetaObject::invokeMethod(correction_, "setEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, false));
  }
  if (sensor_ && sensorThread_ && sensorThread_->isRunning()) {
    QMetaObject::invokeMethod(sensor_, "restoreOriginalPreview",
                              Qt::QueuedConnection);
  }
  lastCorrectionStatus_.active = false;
  lastCorrectionStatus_.paused = false;
  lastCorrectionStatus_.phase = QStringLiteral("stopped");
  lastCorrectionStatus_.reason = reason;
  publishCorrectionStatus();
  if (notify && owner != 0 && (wasPending || wasActive)) {
    emit eventReady(owner, QStringLiteral("robot.autoCorrection.stopped"),
                    QJsonObject{{QStringLiteral("reason"), reason}});
  }
}

void RobotPeripheralHostService::setAutoCorrectionPaused(bool paused) {
  if (!correctionActive_ || correctionPaused_ == paused) return;
  correctionPaused_ = paused;
  lastCorrectionStatus_.paused = paused;
  if (paused) emit driveCorrectionCommand(0.0, 0.0);
  if (correction_ && correctionThread_ && correctionThread_->isRunning()) {
    QMetaObject::invokeMethod(correction_, "setPaused", Qt::QueuedConnection,
                              Q_ARG(bool, paused));
  }
  emit eventReady(ownerSessionId_,
                  QStringLiteral("robot.autoCorrection.paused"),
                  QJsonObject{{QStringLiteral("paused"), paused}});
  publishCorrectionStatus();
}

void RobotPeripheralHostService::publishCorrectionStatus() {
  lastCorrectionStatus_.active =
      lastCorrectionStatus_.active || correctionActive_;
  lastCorrectionStatus_.paused =
      correctionPaused_ || lastCorrectionStatus_.paused;
  const QJsonObject status = laserCorrectionStatusToJson(lastCorrectionStatus_);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastCorrectionStatusMs_ < kCorrectionStatusIntervalMs) return;
  lastCorrectionStatusMs_ = now;
  if (ownerSessionId_ == 0) return;
  if (mediaReceptionActive_) {
    const QByteArray payload =
        QJsonDocument(status).toJson(QJsonDocument::Compact);
    emit mediaReady(kMediaStreamCorrectionStatus,
                    MediaStreamKind::CorrectionStatus, payload);
  }
  emit eventReady(ownerSessionId_,
                  QStringLiteral("robot.autoCorrection.status"),
                  QJsonObject{{QStringLiteral("status"), status}});
}

QString RobotPeripheralHostService::clampStatusText() const {
  if (!clampStatus_.isEmpty()) return clampStatus_;
  return clampConnected_ ? QStringLiteral("已连接")
                         : QStringLiteral("未连接");
}

void RobotPeripheralHostService::appendLog(const QString& message) {
  if (message.isEmpty()) return;
  emit logMessage(ownerSessionId_, message);
  if (ownerSessionId_ != 0) {
    emit eventReady(ownerSessionId_, QStringLiteral("robot.log"),
                    QJsonObject{{QStringLiteral("message"), message}});
  }
}

}  // namespace pa1664::remote
