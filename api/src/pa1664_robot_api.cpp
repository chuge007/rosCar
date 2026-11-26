#include "pa1664_robot_api.h"

#include "drive_types.h"
#include "robot_app_logger.h"
#include "services/remote_workbench_session.h"
#include "services/workbench_data_codec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pa1664 {
namespace {

using remote::RemoteWorkbenchSession;
using remote::WorkbenchConnectionState;

std::string toUtf8(const QString& text) { return text.toStdString(); }

QString fromUtf8(const std::string& text) { return QString::fromStdString(text); }

std::string jsonText(const QJsonObject& object) {
  if (object.isEmpty()) return std::string("{}");
  return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}

bool parseJsonObject(const std::string& text, QJsonObject* object) {
  if (!object) return false;
  const QByteArray bytes = QByteArray::fromStdString(text);
  if (bytes.trimmed().isEmpty()) {
    *object = QJsonObject();
    return true;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    return false;
  *object = document.object();
  return true;
}

bool parseJsonArray(const std::string& text, QJsonArray* array) {
  if (!array) return false;
  const QByteArray bytes = QByteArray::fromStdString(text);
  if (bytes.trimmed().isEmpty()) {
    *array = QJsonArray();
    return true;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isArray())
    return false;
  *array = document.array();
  return true;
}

// 协议里 state 是 0..5 的整数，顺序与 crawling::DriveState 完全一致。
DriveState mapDriveState(int raw) {
  if (raw < static_cast<int>(crawling::DriveState::Disconnected) ||
      raw > static_cast<int>(crawling::DriveState::EmergencyStop)) {
    return DriveState::Unknown;
  }
  return static_cast<DriveState>(raw);
}

Result makeOk(std::string message) {
  Result result;
  result.ok = true;
  result.code = ErrorCode::None;
  result.message = std::move(message);
  return result;
}

Result makeError(ErrorCode code, std::string message) {
  Result result;
  result.ok = false;
  result.code = code;
  result.message = std::move(message);
  return result;
}

RobotStatus statusFromJson(const QJsonObject& json) {
  RobotStatus status;
  const int rawState = json.value(QStringLiteral("state")).toInt(-1);
  status.state = mapDriveState(rawState);
  if (status.state != DriveState::Unknown) {
    status.stateText = toUtf8(crawling::driveStateText(
        static_cast<crawling::DriveState>(rawState)));
  }
  status.reason = toUtf8(json.value(QStringLiteral("reason")).toString());
  status.commandFresh =
      json.value(QStringLiteral("commandFresh")).toBool(false);
  status.feedbackFresh =
      json.value(QStringLiteral("feedbackFresh")).toBool(false);
  status.targetLinearMps =
      json.value(QStringLiteral("targetLinearMps")).toDouble(0.0);
  status.targetAngularRadps =
      json.value(QStringLiteral("targetAngularRadps")).toDouble(0.0);
  status.appliedLinearMps =
      json.value(QStringLiteral("appliedLinearMps")).toDouble(0.0);
  status.appliedAngularRadps =
      json.value(QStringLiteral("appliedAngularRadps")).toDouble(0.0);
  status.leftWheelMps = json.value(QStringLiteral("left"))
                            .toObject()
                            .value(QStringLiteral("wheelSpeedMps"))
                            .toDouble(0.0);
  status.rightWheelMps = json.value(QStringLiteral("right"))
                             .toObject()
                             .value(QStringLiteral("wheelSpeedMps"))
                             .toDouble(0.0);
  return status;
}

CorrectionStatus correctionFromJson(const QJsonObject& json) {
  CorrectionStatus status;
  status.active = json.value(QStringLiteral("active")).toBool(false);
  status.paused = json.value(QStringLiteral("paused")).toBool(false);
  status.phase = toUtf8(json.value(QStringLiteral("phase")).toString());
  status.reason = toUtf8(json.value(QStringLiteral("reason")).toString());
  status.seamStateDescription =
      toUtf8(json.value(QStringLiteral("seamStateDescription")).toString());
  status.confidence =
      json.value(QStringLiteral("confidence")).toDouble(0.0);
  status.linearCommandMps =
      json.value(QStringLiteral("linearCommandMps")).toDouble(0.0);
  status.angularCommandRadps =
      json.value(QStringLiteral("angularCommandRadps")).toDouble(0.0);
  status.cycleCount = json.value(QStringLiteral("cycleCount")).toInt(0);
  return status;
}

UltrasoundStatus ultrasoundStatusFromJson(const QJsonObject& json) {
  UltrasoundStatus status;
  const QJsonObject device = json.value(QStringLiteral("device")).toObject();
  const QJsonObject source = device.isEmpty() ? json : device;
  status.serviceConnected =
      source.value(QStringLiteral("serviceConnected")).toBool(false);
  status.hardwareOnline =
      source.value(QStringLiteral("hardwareOnline")).toBool(false);
  status.capturing = source.value(QStringLiteral("capturing")).toBool(false);
  status.captureReady =
      source.value(QStringLiteral("captureReady")).toBool(false);
  status.pointCount =
      qMax(1, source.value(QStringLiteral("pointCount")).toInt(1024));
  status.beamCount =
      qMax(1, source.value(QStringLiteral("beamCount")).toInt(1));
  status.maxAmplitudeValue =
      qMax(1, source.value(QStringLiteral("maxAmplitudeValue")).toInt(4096));
  status.currentGroup =
      source.value(QStringLiteral("currentGroup")).toInt(0);
  status.totalFrames =
      source.value(QStringLiteral("totalFrames")).toDouble(0.0);
  return status;
}

PixelFormat mapPixelFormat(QImage::Format format) {
  switch (format) {
    case QImage::Format_Grayscale8:
      return PixelFormat::Grayscale8;
    case QImage::Format_RGB888:
      return PixelFormat::RGB888;
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
      return PixelFormat::RGBA8888;
    case QImage::Format_Indexed8:
      return PixelFormat::Indexed8;
    default:
      return PixelFormat::Unknown;
  }
}

QString axisName(int axis) {
  return QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
}

QJsonObject decoderToJson(const UltrasoundDecoderConfig& config) {
  return QJsonObject{
      {QStringLiteral("groupId"), config.groupId},
      {QStringLiteral("beamCount"), config.beamCount},
      {QStringLiteral("pointCount"), config.pointCount},
      {QStringLiteral("amplitudeScale"), config.amplitudeScale},
      {QStringLiteral("selectedBeam"), config.selectedBeam},
      {QStringLiteral("scanModes"), config.scanModes},
      {QStringLiteral("rangeStart"), config.rangeStart},
      {QStringLiteral("rangeEnd"), config.rangeEnd}};
}

UltrasoundDecoderConfig decoderFromJson(const QJsonObject& json) {
  UltrasoundDecoderConfig config;
  config.groupId = json.value(QStringLiteral("groupId")).toInt(config.groupId);
  config.beamCount = json.value(QStringLiteral("beamCount")).toInt(config.beamCount);
  config.pointCount =
      json.value(QStringLiteral("pointCount")).toInt(config.pointCount);
  config.amplitudeScale =
      json.value(QStringLiteral("amplitudeScale")).toInt(config.amplitudeScale);
  config.selectedBeam =
      json.value(QStringLiteral("selectedBeam")).toInt(config.selectedBeam);
  config.scanModes = json.value(QStringLiteral("scanModes")).toInt(config.scanModes);
  config.rangeStart =
      json.value(QStringLiteral("rangeStart")).toDouble(config.rangeStart);
  config.rangeEnd =
      json.value(QStringLiteral("rangeEnd")).toDouble(config.rangeEnd);
  return config;
}

}  // namespace

// ---------------------------------------------------------------------------
// drive_settings.ini
// ---------------------------------------------------------------------------

namespace {

// 解析一个已存在的 ini 文件。
DriveConfig parseDriveConfigFile(const QString& file) {
  DriveConfig out;
  out.path = toUtf8(file);
  if (!QFileInfo::exists(file)) {
    out.error = "配置文件不存在：" + out.path;
    return out;
  }
  QSettings store(file, QSettings::IniFormat);
  if (store.status() != QSettings::NoError) {
    out.error = "配置文件无法解析：" + out.path;
    return out;
  }

  store.beginGroup(QStringLiteral("drive"));
  const QStringList scalarKeys = store.childKeys();
  for (const QString& key : scalarKeys)
    out.raw[toUtf8(key)] = toUtf8(store.value(key).toString());

  out.schemaVersion =
      store.value(QStringLiteral("settingsSchemaVersion"), 0).toInt();
  out.wheelRadiusM = store.value(QStringLiteral("wheelRadiusM"),
                                 out.wheelRadiusM).toDouble();
  out.trackWidthM = store.value(QStringLiteral("trackWidthM"),
                                out.trackWidthM).toDouble();
  out.motorOutputToWheelRatio =
      store.value(QStringLiteral("motorOutputToWheelRatio"),
                  out.motorOutputToWheelRatio).toDouble();
  out.maximumWheelSpeedMps =
      store.value(QStringLiteral("maximumWheelSpeedMps"),
                  out.maximumWheelSpeedMps).toDouble();
  out.maximumLinearSpeedMps =
      store.value(QStringLiteral("maximumLinearSpeedMps"),
                  out.maximumLinearSpeedMps).toDouble();
  out.maximumAngularSpeedRadps =
      store.value(QStringLiteral("maximumAngularSpeedRadps"),
                  out.maximumAngularSpeedRadps).toDouble();
  out.maximumLinearAccelerationMps2 =
      store.value(QStringLiteral("maximumLinearAccelerationMps2"),
                  out.maximumLinearAccelerationMps2).toDouble();
  out.maximumAngularAccelerationRadps2 =
      store.value(QStringLiteral("maximumAngularAccelerationRadps2"),
                  out.maximumAngularAccelerationRadps2).toDouble();
  out.commandTimeoutMs =
      store.value(QStringLiteral("commandTimeoutMs"), out.commandTimeoutMs).toInt();
  out.feedbackTimeoutMs =
      store.value(QStringLiteral("feedbackTimeoutMs"), out.feedbackTimeoutMs)
          .toInt();
  out.armingTimeoutMs =
      store.value(QStringLiteral("armingTimeoutMs"), out.armingTimeoutMs).toInt();

  out.wheelMode = toUtf8(store.value(QStringLiteral("wheelCommunicationMode"),
                                     QStringLiteral("can"))
                             .toString());
  out.wheelCanPort =
      toUtf8(store.value(QStringLiteral("wheelCanSerialPort")).toString());
  out.wheelCanBaudRate =
      store.value(QStringLiteral("wheelCanSerialBaudRate"),
                  out.wheelCanBaudRate).toInt();
  out.wheelCanBitrate =
      store.value(QStringLiteral("wheelCanBitrate"), out.wheelCanBitrate).toInt();
  out.leftPort =
      toUtf8(store.value(QStringLiteral("leftMotorSerialPort")).toString());
  out.rightPort =
      toUtf8(store.value(QStringLiteral("rightMotorSerialPort")).toString());
  out.leftBaudRate =
      store.value(QStringLiteral("leftMotorBaudRate"), out.leftBaudRate).toInt();
  out.rightBaudRate =
      store.value(QStringLiteral("rightMotorBaudRate"), out.rightBaudRate).toInt();
  out.leftMotorId =
      store.value(QStringLiteral("leftMotorId"), out.leftMotorId).toInt();
  out.rightMotorId =
      store.value(QStringLiteral("rightMotorId"), out.rightMotorId).toInt();
  out.leftMotorSign =
      store.value(QStringLiteral("leftMotorSign"), out.leftMotorSign).toInt();
  out.rightMotorSign =
      store.value(QStringLiteral("rightMotorSign"), out.rightMotorSign).toInt();

  const char* axisArrays[] = {"clampXMotors", "clampYMotors", "clampZMotors"};
  const char* legacyIds[] = {"clampXMotorId", "clampYMotorId", "clampZMotorId"};
  const char* legacySigns[] = {"clampXMotorSign", "clampYMotorSign",
                               "clampZMotorSign"};
  // schema 13 起用数组描述每轴电机数量；声明过该标记的文件里 size=0 表示
  // "该轴没有电机"，不能再回退成旧版单电机，否则会凭空多出一台。
  const bool axesConfigured =
      store.value(QStringLiteral("clampAxesConfigured"), false).toBool();
  for (int axis = 0; axis < 3; ++axis) {
    const QString arrayKey = QString::fromLatin1(axisArrays[axis]);
    const int count = store.beginReadArray(arrayKey);
    if (count > 0) {
      for (int index = 0; index < count; ++index) {
        store.setArrayIndex(index);
        ClampMotorConfig motor;
        motor.axis = axis;
        motor.index = index;
        motor.name = toUtf8(axisName(axis)) + std::to_string(index + 1);
        motor.alias = toUtf8(store.value(QStringLiteral("alias")).toString());
        motor.mode = toUtf8(store.value(QStringLiteral("mode")).toString());
        motor.port =
            toUtf8(store.value(QStringLiteral("serialPort")).toString());
        motor.baudRate =
            store.value(QStringLiteral("serialBaudRate"), motor.baudRate).toInt();
        motor.canBitrate =
            store.value(QStringLiteral("canBitrate"), motor.canBitrate).toInt();
        motor.motorId =
            store.value(QStringLiteral("motorId"), index + 1).toInt();
        motor.sign = store.value(QStringLiteral("sign"), motor.sign).toInt();
        motor.gearRatio =
            store.value(QStringLiteral("gearRatio"), motor.gearRatio).toDouble();
        motor.upperLimit =
            store.value(QStringLiteral("upperLimit"), 0.0).toDouble();
        motor.lowerLimit =
            store.value(QStringLiteral("lowerLimit"), 0.0).toDouble();
        motor.upperLimitEnabled =
            store.value(QStringLiteral("upperLimitEnabled"), false).toBool();
        motor.lowerLimitEnabled =
            store.value(QStringLiteral("lowerLimitEnabled"), false).toBool();
        out.clampMotors.push_back(motor);
      }
      store.endArray();
      continue;
    }
    store.endArray();
    if (axesConfigured) continue;  // 该轴显式配置为 0 台电机
    // 旧版配置（schema 13 之前）只有单台电机键，做一次等价回退。
    ClampMotorConfig motor;
    motor.axis = axis;
    motor.index = 0;
    motor.name = toUtf8(axisName(axis)) + "1";
    motor.mode = toUtf8(store
                            .value(QStringLiteral("clampCommunicationMode"),
                                   QStringLiteral("tt_canopen"))
                            .toString());
    motor.port =
        toUtf8(store.value(QStringLiteral("clampSerialPort")).toString());
    motor.baudRate =
        store.value(QStringLiteral("clampSerialBaudRate"), motor.baudRate).toInt();
    motor.canBitrate =
        store.value(QStringLiteral("clampCanBitrate"), motor.canBitrate).toInt();
    motor.motorId =
        store.value(QString::fromLatin1(legacyIds[axis]), axis + 1).toInt();
    motor.sign =
        store.value(QString::fromLatin1(legacySigns[axis]), 1).toInt();
    out.clampMotors.push_back(motor);
  }

  for (int slot = 0; slot < 2; ++slot) {
    DriveConfig::CameraConfig camera;
    camera.slot = slot;
    camera.source =
        toUtf8(store
                   .value(QStringLiteral("cameraSource%1").arg(slot),
                          QStringLiteral("usb"))
                   .toString());
    camera.deviceIndex =
        store.value(QStringLiteral("usbCameraDeviceIndex%1").arg(slot), -1)
            .toInt();
    camera.url = toUtf8(
        store.value(QStringLiteral("networkCameraUrl%1").arg(slot)).toString());
    camera.fps = store.value(QStringLiteral("usbCameraFps%1").arg(slot), 15)
                     .toInt();
    camera.autoConnect =
        store.value(QStringLiteral("usbCameraAutoConnect%1").arg(slot), true)
            .toBool();
    camera.flipHorizontal =
        store.value(QStringLiteral("usbCameraFlipHorizontal%1").arg(slot), false)
            .toBool();
    camera.flipVertical =
        store.value(QStringLiteral("usbCameraFlipVertical%1").arg(slot), false)
            .toBool();
    out.cameras.push_back(camera);
  }
  store.endGroup();

  out.loaded = true;
  return out;
}

// 主机 robot.snapshot 里带的 settings.iniBase64：落地成临时 ini 再复用解析。
DriveConfig driveConfigFromIniBytes(const QByteArray& ini,
                                    const std::string& label) {
  DriveConfig failed;
  failed.path = label;
  if (ini.isEmpty()) {
    failed.error = "主机未返回底盘配置";
    return failed;
  }
  QTemporaryFile temp;
  if (!temp.open()) {
    failed.error = "无法创建临时配置文件";
    return failed;
  }
  if (temp.write(ini) != ini.size() || !temp.flush()) {
    failed.error = "无法写入临时配置文件";
    return failed;
  }
  const QString path = temp.fileName();
  temp.close();
  DriveConfig parsed = parseDriveConfigFile(path);
  parsed.path = label;
  return parsed;
}

}  // namespace

DriveConfig loadDriveConfig(const std::string& path) {
  QString file = fromUtf8(path);
  if (file.trimmed().isEmpty()) {
    const QDir base(QCoreApplication::applicationDirPath());
    file = base.filePath(QStringLiteral("config/drive_settings.ini"));
  }
  return parseDriveConfigFile(file);
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

class RobotApi::Impl {
 public:
  explicit Impl(Config value) : config(std::move(value)) {
    // 复用上位机的日志实现：<exe 目录>/logs/robot_console.log。
    crawling::AppLogger::initialize();
    loadProfileFromDisk();
    wireSession();
  }

  ~Impl() {
    // 先断开本方回调，再停会话：RemoteWorkbenchSession 在析构/stop 时会发
    // stateChanged，若回调对象已销毁会踩空。
    if (enableTimer) enableTimer->stop();
    QObject::disconnect(&session, nullptr, nullptr, nullptr);
    session.stop();
  }

  void start() {
    if (started) return;
    started = true;
    session.start(fromUtf8(config.host), config.controlPort);
  }

  void stop() {
    started = false;
    decoderAppliedThisSession = false;
    ultrasoundSnapshotDone = false;
    session.stop();
  }

  bool isOnline() const { return session.isReady(); }

  Result enable(bool enabled) {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，使能请求未发送");
    if (!session.hasCapability(
            QString::fromLatin1(remote::kCapabilityRobotControlV1))) {
      return makeError(ErrorCode::HostRejected,
                       "小车主机未声明 robot.control.v1 能力，无法使能");
    }
    const QJsonObject data{{QStringLiteral("enabled"), enabled}};
    if (session.request(QStringLiteral("robot.enable"), data) == 0)
      return makeError(ErrorCode::Transport, "使能请求被拒绝（发送缓冲已满或会话失效）");
    return makeOk(enabled ? "使能请求已发送，等待主机状态变为 Enabled"
                          : "撤销使能请求已发送");
  }

  Result emergencyStop() {
    // 双通道：UDP 锁存包（80 ms 内独立生效）+ TCP robot.emergencyStop。
    session.emergencyStop();
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline,
                       "急停已在本机锁存，但控制链路未就绪，仅实时通道可能生效");
    return makeOk("急停已发送（UDP 锁存 + TCP 请求）");
  }

  Result reset() { return submitSimple(QStringLiteral("robot.reset"), "复位请求已发送"); }

  Result clearAlarm() {
    return submitSimple(QStringLiteral("robot.clearAlarm"), "清报警请求已发送");
  }

  Result connectChassis() {
    return submitSimple(QStringLiteral("robot.connect"),
                        "底盘连接请求已发送（沿用主机已保存的驱动配置）");
  }

  Result disconnectChassis() {
    return submitSimple(QStringLiteral("robot.disconnect"), "底盘断开请求已发送");
  }

  Result takeControl() {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，无法取得控制权");
    pendingEnable = true;
    if (session.request(QStringLiteral("robot.connect")) == 0) {
      pendingEnable = false;
      return makeError(ErrorCode::Transport, "底盘连接请求被拒绝");
    }
    return makeOk("已申请底盘连接，主机应答后 1.2 s 自动使能");
  }

  Result requestSnapshot() {
    return submitSimple(QStringLiteral("robot.snapshot"),
                        "底盘快照请求已发送，结果见响应回调");
  }

  Result requestDriveConfig() {
    return submitSimple(QStringLiteral("robot.snapshot"),
                        "底盘配置请求已发送，结果见 setDriveConfigHandler");
  }

  Result driveRatio(double linearRatio, double angularRatio) {
    if (!std::isfinite(linearRatio) || !std::isfinite(angularRatio))
      return makeError(ErrorCode::InvalidArgument, "速度比例必须是有限数");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，运动指令未发送");
    const double linear =
        std::clamp(linearRatio, -1.0, 1.0) * config.maxLinearSpeedMps;
    const double angular =
        std::clamp(angularRatio, -1.0, 1.0) * config.maxAngularSpeedRadps;
    session.setMotion(linear, angular, config.motionLeaseMs);
    lastLinearMps = linear;
    lastAngularRadps = angular;
    return makeOk("运动指令已下发（持续生效，直到 stopMotion/急停/断链）");
  }

  Result moveVelocity(double linearMps, double angularRadps) {
    if (!std::isfinite(linearMps) || !std::isfinite(angularRadps))
      return makeError(ErrorCode::InvalidArgument, "线速度/角速度必须是有限数");
    if (std::abs(linearMps) > config.maxLinearSpeedMps + 1e-9)
      return makeError(ErrorCode::InvalidArgument,
                       "线速度超出本机配置上限（maxLinearSpeedMps），请先修改 Config");
    if (std::abs(angularRadps) > config.maxAngularSpeedRadps + 1e-9)
      return makeError(ErrorCode::InvalidArgument,
                       "角速度超出本机配置上限（maxAngularSpeedRadps），请先修改 Config");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，运动指令未发送");
    if (std::abs(linearMps) < 1e-9 && std::abs(angularRadps) < 1e-9)
      return stopMotion();
    session.setMotion(linearMps, angularRadps, config.motionLeaseMs);
    lastLinearMps = linearMps;
    lastAngularRadps = angularRadps;
    return makeOk("速度指令已下发（持续生效）");
  }

  Result stopMotion() {
    session.clearMotion();
    lastLinearMps = 0.0;
    lastAngularRadps = 0.0;
    if (!session.isReady())
      return makeOk("已清零本机运动指令（链路未就绪，主机侧由运动租约自动停车）");
    return makeOk("已发出零速停车指令");
  }

  Result stopMotionOnHost() {
    return submitSimple(QStringLiteral("robot.motion.stop"),
                        "主机侧运动指令已清零");
  }

  // ---- 夹爪总线 ---------------------------------------------------------
  Result clampConnect() {
    return submitSimple(QStringLiteral("robot.clamp.connect"),
                        "夹爪连接请求已发送（沿用主机已保存的夹爪配置）");
  }

  Result clampDisconnect() {
    return submitSimple(QStringLiteral("robot.clamp.disconnect"),
                        "夹爪断开请求已发送（会先停全部电机）");
  }

  Result requestClampStatus() {
    return submitSimple(QStringLiteral("robot.clamp.status"),
                        "夹爪状态请求已发送");
  }

  Result requestClampSettings() {
    return submitSimple(QStringLiteral("robot.clamp.settings.get"),
                        "夹爪配置请求已发送");
  }

  // ---- 摄像头扫描 / 下行分辨率 -----------------------------------------
  Result scanCameras() {
    return submitSimple(
        QStringLiteral("robot.camera.scan"),
        "摄像头扫描请求已发送（结果见 robot.camera.devices 事件）");
  }

  Result setCameraDisplaySize(int slot, int width, int height) {
    if (slot < 0 || slot > 1)
      return makeError(ErrorCode::InvalidArgument, "摄像头通道只支持 0 或 1");
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
      return makeError(ErrorCode::InvalidArgument, "摄像头分辨率无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，摄像头指令未发送");
    const QJsonObject data{{QStringLiteral("slot"), slot},
                           {QStringLiteral("width"), width},
                           {QStringLiteral("height"), height}};
    if (session.request(QStringLiteral("robot.camera.displaySize"), data) == 0)
      return makeError(ErrorCode::Transport, "显示尺寸设置被拒绝");
    return makeOk("摄像头显示尺寸已下发");
  }

  // ---- 自动纠偏 ---------------------------------------------------------
  Result startAutoCorrection(const std::string& settingsJson) {
    QJsonObject settings;
    if (!parseJsonObject(settingsJson, &settings))
      return makeError(ErrorCode::InvalidArgument,
                       "纠偏参数不是合法的 JSON 对象");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，纠偏未启动");
    const QJsonObject data{{QStringLiteral("settings"), settings}};
    if (session.request(QStringLiteral("robot.autoCorrection.start"), data) == 0)
      return makeError(ErrorCode::Transport, "纠偏启动请求被拒绝");
    return makeOk("纠偏启动请求已发送（主机先准备激光轮廓流）");
  }

  Result stopAutoCorrection() {
    return submitSimple(QStringLiteral("robot.autoCorrection.stop"),
                        "纠偏停止请求已发送");
  }

  Result pauseAutoCorrection(bool paused) {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，纠偏暂停未发送");
    const QJsonObject data{{QStringLiteral("paused"), paused}};
    if (session.request(QStringLiteral("robot.autoCorrection.pause"), data) == 0)
      return makeError(ErrorCode::Transport, "纠偏暂停请求被拒绝");
    return makeOk(paused ? "纠偏暂停请求已发送" : "纠偏继续请求已发送");
  }

  Result requestAutoCorrectionStatus() {
    return submitSimple(QStringLiteral("robot.autoCorrection.status"),
                        "纠偏状态请求已发送");
  }

  // ---- 夹爪电机 ---------------------------------------------------------
  Result clampSetSpeed(int axis, int index, int percent) {
    if (axis < 0 || axis > 2 || index < 0 || index > 15)
      return makeError(ErrorCode::InvalidArgument,
                       "夹爪电机轴/序号越界（axis 0..2，index 0..15）");
    if (percent < -100 || percent > 100)
      return makeError(ErrorCode::InvalidArgument, "夹爪速度百分比必须在 -100..100");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，夹爪指令未发送");
    const QJsonObject data{{QStringLiteral("axis"), axis},
                           {QStringLiteral("index"), index},
                           {QStringLiteral("percent"), percent}};
    if (session.request(QStringLiteral("robot.clamp.setMotorSpeed"), data) == 0)
      return makeError(ErrorCode::Transport, "夹爪调速请求被拒绝");
    return makeOk("夹爪调速指令已下发");
  }

  Result clampStopMotor(int axis, int index) {
    if (axis < 0 || axis > 2 || index < 0 || index > 15)
      return makeError(ErrorCode::InvalidArgument, "夹爪电机轴/序号越界");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，夹爪指令未发送");
    const QJsonObject data{{QStringLiteral("axis"), axis},
                           {QStringLiteral("index"), index}};
    if (session.request(QStringLiteral("robot.clamp.stopMotor"), data) == 0)
      return makeError(ErrorCode::Transport, "夹爪停止请求被拒绝");
    return makeOk("夹爪单电机停止指令已下发");
  }

  Result clampStop() {
    return submitSimple(QStringLiteral("robot.clamp.stop"), "夹爪全停指令已下发");
  }

  Result clampMoveToPosition(int axis, int index, double degrees) {
    if (axis < 0 || axis > 2 || index < 0 || index > 15)
      return makeError(ErrorCode::InvalidArgument, "夹爪电机轴/序号越界");
    if (!std::isfinite(degrees) || std::abs(degrees) > 1000000.0)
      return makeError(ErrorCode::InvalidArgument, "目标位置无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，夹爪指令未发送");
    const QJsonObject data{{QStringLiteral("axis"), axis},
                           {QStringLiteral("index"), index},
                           {QStringLiteral("degrees"), degrees}};
    if (session.request(QStringLiteral("robot.clamp.moveToPosition"), data) == 0)
      return makeError(ErrorCode::Transport, "夹爪位置指令被拒绝");
    return makeOk("夹爪位置指令已下发（需电机协议支持位置控制）");
  }

  Result clampRequestPosition(int axis, int index) {
    if (axis < 0 || axis > 2 || index < 0 || index > 15)
      return makeError(ErrorCode::InvalidArgument, "夹爪电机轴/序号越界");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，夹爪指令未发送");
    const QJsonObject data{{QStringLiteral("axis"), axis},
                           {QStringLiteral("index"), index}};
    if (session.request(QStringLiteral("robot.clamp.requestPosition"), data) == 0)
      return makeError(ErrorCode::Transport, "夹爪位置请求被拒绝");
    return makeOk("夹爪位置查询已发送");
  }

  Result clampRequestPositions() {
    return submitSimple(QStringLiteral("robot.clamp.positions"),
                        "夹爪位置快照请求已发送");
  }

  // ---- 摄像头 -----------------------------------------------------------
  Result connectCamera(int slot, int deviceIndex, int fps, bool flipHorizontal,
                       bool flipVertical) {
    if (slot < 0 || slot > 1)
      return makeError(ErrorCode::InvalidArgument, "摄像头通道只支持 0 或 1");
    if (deviceIndex < 0)
      return makeError(ErrorCode::InvalidArgument, "USB 摄像头设备编号无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，摄像头指令未发送");
    const QJsonObject data{
        {QStringLiteral("slot"), slot},
        {QStringLiteral("source"), QStringLiteral("usb")},
        {QStringLiteral("deviceIndex"), deviceIndex},
        {QStringLiteral("fps"), qBound(1, fps, 60)},
        {QStringLiteral("flipHorizontal"), flipHorizontal},
        {QStringLiteral("flipVertical"), flipVertical}};
    if (session.request(QStringLiteral("robot.camera.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "摄像头连接请求被拒绝");
    return makeOk("摄像头连接请求已发送（图像经媒体通道回传）");
  }

  Result connectNetworkCamera(int slot, const std::string& url,
                              bool flipHorizontal, bool flipVertical) {
    if (slot < 0 || slot > 1)
      return makeError(ErrorCode::InvalidArgument, "摄像头通道只支持 0 或 1");
    if (url.empty())
      return makeError(ErrorCode::InvalidArgument, "网络摄像头地址为空");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，摄像头指令未发送");
    const QJsonObject data{
        {QStringLiteral("slot"), slot},
        {QStringLiteral("source"), QStringLiteral("network")},
        {QStringLiteral("url"), fromUtf8(url)},
        {QStringLiteral("flipHorizontal"), flipHorizontal},
        {QStringLiteral("flipVertical"), flipVertical}};
    if (session.request(QStringLiteral("robot.camera.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "摄像头连接请求被拒绝");
    return makeOk("网络摄像头连接请求已发送");
  }

  Result disconnectCamera(int slot) {
    if (slot < 0 || slot > 1)
      return makeError(ErrorCode::InvalidArgument, "摄像头通道只支持 0 或 1");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，摄像头指令未发送");
    const QJsonObject data{{QStringLiteral("slot"), slot}};
    if (session.request(QStringLiteral("robot.camera.disconnect"), data) == 0)
      return makeError(ErrorCode::Transport, "摄像头断开请求被拒绝");
    return makeOk("摄像头断开请求已发送");
  }

  Result disconnectAllCameras() {
    return submitSimple(QStringLiteral("robot.camera.disconnectAll"),
                        "全部摄像头断开请求已发送");
  }

  Result requestCameraStatus() {
    return submitSimple(QStringLiteral("robot.camera.status"),
                        "摄像头状态请求已发送");
  }

  // ---- 超声 -------------------------------------------------------------
  Result ultrasoundConnect(const std::string& address, int deviceId) {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，超声指令未发送");
    const QJsonObject data{
        {QStringLiteral("address"),
         fromUtf8(address.empty() ? std::string("127.0.0.1") : address)},
        {QStringLiteral("deviceId"), deviceId}};
    if (session.request(QStringLiteral("ultrasound.device.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "超声连接请求被拒绝");
    return makeOk("超声设备连接请求已发送（SDK 在小车主机侧）");
  }

  Result ultrasoundDisconnect() {
    return submitSimple(QStringLiteral("ultrasound.device.disconnect"),
                        "超声断开请求已发送");
  }

  Result ultrasoundStartAcquisition() {
    return submitSimple(QStringLiteral("ultrasound.acquisition.start"),
                        "超声采集启动请求已发送");
  }

  Result ultrasoundStopAcquisition() {
    return submitSimple(QStringLiteral("ultrasound.acquisition.stop"),
                        "超声采集停止请求已发送");
  }

  Result requestUltrasoundStatus() {
    return submitSimple(QStringLiteral("ultrasound.status"),
                        "超声状态请求已发送");
  }

  Result ultrasoundParametersSettled(bool resumeCapture) {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，请求未发送");
    const QJsonObject data{{QStringLiteral("resumeCapture"), resumeCapture}};
    if (session.request(QStringLiteral("ultrasound.parameters.settled"), data) == 0)
      return makeError(ErrorCode::Transport, "参数稳定请求被拒绝");
    return makeOk("已请求主机在参数生效后恢复采集");
  }

  Result ultrasoundParameterSnapshot() {
    return submitSimple(QStringLiteral("ultrasound.parameters.snapshot"),
                        "超声参数快照请求已发送");
  }

  Result ultrasoundGetParameter(const std::string& method,
                                const std::string& argsJson) {
    if (method.empty() || method.rfind("get", 0) != 0)
      return makeError(ErrorCode::InvalidArgument,
                       "参数读取方法必须以 get 开头");
    QJsonArray args;
    if (!parseJsonArray(argsJson, &args))
      return makeError(ErrorCode::InvalidArgument, "argsJson 不是合法的 JSON 数组");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，参数请求未发送");
    const QJsonArray requests{QJsonObject{
        {QStringLiteral("method"), fromUtf8(method)},
        {QStringLiteral("args"), args}}};
    const QJsonObject data{{QStringLiteral("requests"), requests}};
    if (session.request(QStringLiteral("ultrasound.parameters.getMany"), data) == 0)
      return makeError(ErrorCode::Transport, "参数读取请求被拒绝");
    return makeOk("参数读取请求已发送");
  }

  Result ultrasoundSetParameter(const std::string& setMethod,
                                const std::string& argsJson,
                                const std::string& readbackMethod,
                                const std::string& readbackArgsJson) {
    if (setMethod.rfind("set", 0) != 0)
      return makeError(ErrorCode::InvalidArgument,
                       "参数写入方法必须以 set 开头");
    QString readback = fromUtf8(readbackMethod);
    if (readback.isEmpty())
      readback = QStringLiteral("get") + fromUtf8(setMethod).mid(3);
    QJsonArray args;
    if (!parseJsonArray(argsJson, &args))
      return makeError(ErrorCode::InvalidArgument, "argsJson 不是合法的 JSON 数组");
    // SDK 约定：setter 为 (值..., groupId)，getter 只带 groupId。因此自动
    // 推导回读参数时保留最后一个（组号），单个参数的 setter 回读不带参数。
    QJsonArray readbackArgs;
    if (!readbackArgsJson.empty()) {
      if (!parseJsonArray(readbackArgsJson, &readbackArgs))
        return makeError(ErrorCode::InvalidArgument,
                         "readbackArgsJson 不是合法的 JSON 数组");
    } else if (args.size() >= 2) {
      readbackArgs.append(args.at(args.size() - 1));
    }
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，参数请求未发送");
    const QJsonObject data{{QStringLiteral("method"), fromUtf8(setMethod)},
                           {QStringLiteral("args"), args},
                           {QStringLiteral("readbackMethod"), readback},
                           {QStringLiteral("readbackArgs"), readbackArgs}};
    if (session.request(QStringLiteral("ultrasound.parameter.set"), data) == 0)
      return makeError(ErrorCode::Transport, "参数写入请求被拒绝");
    return makeOk("参数写入请求已发送（主机写入后会回读校验）");
  }

  Result ultrasoundApplyParameters(const std::string& changesJson) {
    QJsonArray changes;
    if (!parseJsonArray(changesJson, &changes) || changes.isEmpty())
      return makeError(ErrorCode::InvalidArgument,
                       "changesJson 必须是非空 JSON 数组");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，参数请求未发送");
    const QJsonObject data{{QStringLiteral("changes"), changes}};
    if (session.request(QStringLiteral("ultrasound.parameters.apply"), data) == 0)
      return makeError(ErrorCode::Transport, "参数批量写入请求被拒绝");
    return makeOk("参数批量写入请求已发送");
  }

  Result configureUltrasoundDecoder(const UltrasoundDecoderConfig& cfg) {
    if (cfg.groupId < 0 || cfg.beamCount <= 0 || cfg.pointCount <= 0 ||
        cfg.selectedBeam < 0 || cfg.selectedBeam >= cfg.beamCount ||
        cfg.amplitudeScale <= 0 || cfg.rangeEnd <= cfg.rangeStart)
      return makeError(ErrorCode::InvalidArgument, "解码工作组配置无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，解码配置未发送");
    lastDecoder = cfg;
    const QJsonObject group{
        {QStringLiteral("groupId"), cfg.groupId},
        {QStringLiteral("groupOffsetWords"), 0},
        {QStringLiteral("beamCount"), cfg.beamCount},
        {QStringLiteral("pointCount"), cfg.pointCount},
        {QStringLiteral("scanModes"), cfg.scanModes},
        {QStringLiteral("selectedBeam"), cfg.selectedBeam},
        {QStringLiteral("amplitudeScale"), cfg.amplitudeScale},
        {QStringLiteral("encoderPrecisionA"), 10},
        {QStringLiteral("encoderPrecisionB"), 10},
        {QStringLiteral("encoderGeometryRevision"), 0},
        {QStringLiteral("rangeStart"), cfg.rangeStart},
        {QStringLiteral("rangeEnd"), cfg.rangeEnd}};
    const QJsonObject data{{QStringLiteral("groups"), QJsonArray{group}},
                           {QStringLiteral("displayGroupId"), cfg.groupId}};
    if (session.request(QStringLiteral("ultrasound.decoder.settings"), data) == 0)
      return makeError(ErrorCode::Transport, "解码配置请求被拒绝");
    // 主机不持久化解码器配置，本地记一份，下次连上自动下发。
    profile.decoder = cfg;
    profile.hasDecoder = true;
    if (profileOptions.mirrorRemote) saveProfileToDisk();
    return makeOk("超声解码配置已下发");
  }

  // ---- 通用 -------------------------------------------------------------
  std::uint64_t genericRequest(const std::string& operation,
                               const std::string& dataJson) {
    if (operation.empty()) {
      log(LogLevel::Error, "operation 不能为空");
      return 0;
    }
    QJsonObject data;
    if (!parseJsonObject(dataJson, &data)) {
      log(LogLevel::Error, "dataJson 不是合法的 JSON 对象：" + operation);
      return 0;
    }
    // 会话未就绪时 RemoteWorkbenchSession::request() 会同步触发一次
    // errorCode=not_connected 的响应回调，调用方从这里就能拿到原因。
    const quint64 requestId = session.request(fromUtf8(operation), data);
    if (requestId == 0 && session.isReady())
      log(LogLevel::Error, "请求被拒绝（发送缓冲已满或会话失效）：" + operation);
    return requestId;
  }

  Result submitSimple(const QString& operation, std::string okMessage) {
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，请求未发送");
    if (session.request(operation) == 0)
      return makeError(ErrorCode::Transport, "请求被拒绝（发送缓冲已满或会话失效）");
    return makeOk(std::move(okMessage));
  }

  void log(LogLevel level, const std::string& message) {
    if (logHandler) logHandler(level, message);
  }

  void wireSession() {
    // takeControl() 的延时使能：父对象挂在 session 上，跟随其线程与生命周期。
    enableTimer = new QTimer(&session);
    enableTimer->setSingleShot(true);
    QObject::connect(enableTimer, &QTimer::timeout, &session,
                     [this] { enable(true); });

    QObject::connect(&session, &RemoteWorkbenchSession::stateChanged, &session,
                     [this](WorkbenchConnectionState state,
                            const QString& detail) {
                       statusText = toUtf8(detail);
                       if (connectionHandler) {
                         connectionHandler(
                             state == WorkbenchConnectionState::Online,
                             toUtf8(detail));
                       }
                     });

    QObject::connect(&session, &RemoteWorkbenchSession::responseReceived,
                     &session,
                     [this](const QString& operation, quint64, bool ok,
                            const QJsonObject& data, const QString& errorCode,
                            const QString& errorText) {
                       if (operation == QStringLiteral("robot.connect")) {
                         // takeControl()：底盘连接被主机接受后延时使能。
                         if (ok && pendingEnable) {
                           pendingEnable = false;
                           enableTimer->start(1200);
                         } else if (!ok) {
                           pendingEnable = false;
                         }
                       }
                       if (ok && operation.startsWith(QStringLiteral("ultrasound.")) &&
                           data.contains(QStringLiteral("device"))) {
                         publishUltrasoundStatus(ultrasoundStatusFromJson(data));
                       }
                       if (ok && operation == QStringLiteral("robot.snapshot")) {
                         mirrorDriveSettings(data);
                         if (driveConfigHandler) {
                           const QJsonObject settings =
                               data.value(QStringLiteral("settings")).toObject();
                           const QByteArray ini = QByteArray::fromBase64(
                               settings.value(QStringLiteral("iniBase64"))
                                   .toString()
                                   .toLatin1());
                           driveConfigHandler(driveConfigFromIniBytes(
                               ini, "<robot_host>/config/drive_settings.ini"));
                         }
                       }
                       if (ok && operation ==
                                     QStringLiteral("ultrasound.parameters.snapshot")) {
                         mirrorUltrasoundParameters(
                             data.value(QStringLiteral("values")).toObject());
                       }
                       if (ok && operation ==
                                     QStringLiteral("ultrasound.parameter.set")) {
                         updateUltrasoundParameter(
                             data.value(QStringLiteral("readbackKey")).toString(),
                             data.value(QStringLiteral("readbackValue")));
                       }
                       if (ok && operation ==
                                     QStringLiteral("ultrasound.parameters.apply")) {
                         mirrorReadbacks(
                             data.value(QStringLiteral("readbacks")).toObject());
                       }
                       if (ok && operation ==
                                     QStringLiteral("ultrasound.scan.precision.get")) {
                         scanPrecisionMmPerCell_ =
                             data.value(QStringLiteral("mmPerCell")).toDouble(0.0);
                       }
                       if (ok && operation == QStringLiteral(
                                                "ultrasound.encoder.calibration.get")) {
                         encoderCountsPerMm_[0] =
                             data.value(QStringLiteral("countsPerMmA")).toDouble(0.0);
                         encoderCountsPerMm_[1] =
                             data.value(QStringLiteral("countsPerMmB")).toDouble(0.0);
                       }
                       if (!ok) {
                         lastErrorCode = toUtf8(errorCode);
                         lastErrorText = toUtf8(errorText);
                         if (logHandler) {
                           logHandler(LogLevel::Error,
                                      "请求失败 " + toUtf8(operation) + " [" +
                                          lastErrorCode + "] " + lastErrorText);
                         }
                       }
                       if (responseHandler) {
                         responseHandler(toUtf8(operation), ok,
                                         toUtf8(errorCode), toUtf8(errorText),
                                         jsonText(data));
                       }
                     });

    QObject::connect(&session, &RemoteWorkbenchSession::telemetryReceived,
                     &session, [this](const QJsonObject& telemetry) {
                       const RobotStatus status = statusFromJson(telemetry);
                       lastStatus = status;
                       if (statusHandler) statusHandler(status);
                     });

    QObject::connect(&session, &RemoteWorkbenchSession::eventReceived, &session,
                     [this](const QString& name, const QJsonObject& data) {
                       if (name.startsWith(QStringLiteral("robot.autoCorrection.")))
                         publishCorrection(correctionFromJson(data));
                       else if (name == QStringLiteral("ultrasound.status"))
                         publishUltrasoundStatus(ultrasoundStatusFromJson(data));
                       if (eventHandler)
                         eventHandler(toUtf8(name), jsonText(data));
                     });

    QObject::connect(&session,
                     &RemoteWorkbenchSession::mediaMessageReceived, &session,
                     [this](quint16 streamId, remote::MediaStreamKind kind,
                            quint64, quint64, const QByteArray& payload) {
                       if (kind == remote::MediaStreamKind::UsbCameraImage)
                         handleCameraPayload(streamId, payload);
                       else if (kind == remote::MediaStreamKind::UltrasoundFrame)
                         handleUltrasoundFrame(streamId, payload);
                       else if (kind ==
                                remote::MediaStreamKind::LaserProfileObservation)
                         handleLaserProfilePayload(streamId, payload);
                     });

    QObject::connect(&session, &RemoteWorkbenchSession::bulkMessageReceived,
                     &session,
                     [this](remote::BulkStreamKind kind, quint64,
                            const QByteArray& payload) {
                       if (kind == remote::BulkStreamKind::UltrasoundScanBatch)
                         handleUltrasoundBatch(payload);
                     });

    QObject::connect(&session, &RemoteWorkbenchSession::protocolError, &session,
                     [this](const QString& detail) {
                       log(LogLevel::Error, toUtf8(detail));
                     });
  }

  void publishUltrasoundStatus(const UltrasoundStatus& status) {
    const bool becameReady =
        status.hardwareOnline && !lastUltrasoundStatus.hardwareOnline;
    lastUltrasoundStatus = status;
    // 设备刚从掉线恢复：主机侧解码器配置可能已丢，允许重新下发一次。
    if (becameReady) {
      decoderAppliedThisSession = false;
      ultrasoundSnapshotDone = false;
    }
    maybeAutoApplyDecoder(status);
    maybeAutoSnapshotUltrasound(status);
    if (ultrasoundStatusHandler) ultrasoundStatusHandler(status);
  }

  // ---- 本地配置镜像 -----------------------------------------------------
  QString profileFilePath() const {
    if (!profileOptions.path.empty()) return fromUtf8(profileOptions.path);
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("config/pa1664_profile.json"));
  }

  void loadProfileFromDisk() {
    if (!profileOptions.enabled) return;
    QFile file(profileFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return;
    const QJsonObject root = document.object();

    const QJsonObject drive = root.value(QStringLiteral("drive")).toObject();
    profile.driveSchema = drive.value(QStringLiteral("schema")).toInt(0);
    profile.driveIniBase64 = drive.value(QStringLiteral("iniBase64")).toString();
    profile.driveValid =
        profile.driveSchema > 0 && !profile.driveIniBase64.isEmpty();

    const QJsonObject ultrasound =
        root.value(QStringLiteral("ultrasound")).toObject();
    profile.ultrasoundParams =
        ultrasound.value(QStringLiteral("params")).toObject();
    profile.ultrasoundValid = !profile.ultrasoundParams.isEmpty();
    const QJsonObject decoder =
        ultrasound.value(QStringLiteral("decoder")).toObject();
    if (!decoder.isEmpty()) {
      profile.decoder = decoderFromJson(decoder);
      profile.hasDecoder = true;
    }
    profile.savedAt = root.value(QStringLiteral("savedAt")).toString();
  }

  bool saveProfileToDisk() {
    if (!profileOptions.enabled) return false;
    const QString path = profileFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject drive;
    drive.insert(QStringLiteral("schema"), profile.driveSchema);
    drive.insert(QStringLiteral("iniBase64"), profile.driveIniBase64);
    QJsonObject ultrasound;
    ultrasound.insert(QStringLiteral("params"), profile.ultrasoundParams);
    ultrasound.insert(QStringLiteral("decoder"), decoderToJson(profile.decoder));

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("host"), fromUtf8(config.host));
    root.insert(QStringLiteral("savedAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("drive"), drive);
    root.insert(QStringLiteral("ultrasound"), ultrasound);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const bool written = file.write(bytes) == bytes.size();
    file.close();
    return written;
  }

  // 主机回传的底盘配置原文（iniBase64）直接留档：下次原样回传即可，
  // 不需要在操作端重新序列化 ini，避免键名/转义与主机实现分叉。
  void mirrorDriveSettings(const QJsonObject& data) {
    const QJsonObject settings = data.value(QStringLiteral("settings")).toObject();
    const QString ini = settings.value(QStringLiteral("iniBase64")).toString();
    const int schema = settings.value(QStringLiteral("schema")).toInt(0);
    if (schema <= 0 || ini.isEmpty()) return;
    const bool changed =
        schema != profile.driveSchema || ini != profile.driveIniBase64;
    profile.driveSchema = schema;
    profile.driveIniBase64 = ini;
    profile.driveValid = true;
    if (changed && profileOptions.mirrorRemote) saveProfileToDisk();
  }

  void mirrorUltrasoundParameters(const QJsonObject& values) {
    if (values.isEmpty()) return;
    profile.ultrasoundParams = values;
    profile.ultrasoundValid = true;
    if (profileOptions.mirrorRemote) saveProfileToDisk();
  }

  void updateUltrasoundParameter(const QString& key, const QJsonValue& value) {
    if (key.isEmpty() || value.isUndefined()) return;
    profile.ultrasoundParams.insert(key, value);
    profile.ultrasoundValid = true;
    if (profileOptions.mirrorRemote) saveProfileToDisk();
  }

  void mirrorReadbacks(const QJsonObject& readbacks) {
    if (readbacks.isEmpty()) return;
    for (auto it = readbacks.begin(); it != readbacks.end(); ++it)
      profile.ultrasoundParams.insert(it.key(), it.value());
    profile.ultrasoundValid = true;
    if (profileOptions.mirrorRemote) saveProfileToDisk();
  }

  // 远端不保存解码器配置：每次连上都按「远端几何 + 本地偏好」重新下发，
  // 否则主机重启后一帧 A 扫都收不到，而且没有任何错误提示。
  void maybeAutoApplyDecoder(const UltrasoundStatus& status) {
    if (!profileOptions.autoApplyDecoder || decoderAppliedThisSession) return;
    // 设备不在线时主机侧 SDK 调用会阻塞，下发只会得到超时，必须等就绪。
    if (!status.hardwareOnline) return;
    // 解码器配置只改主机内存里的解码分组，不碰 SDK 采集，所以采集中也要下发：
    // 主机重启后这份配置是空的，不下发就永远解不出 A 扫（且没有任何报错）。
    if (status.pointCount <= 0 || status.beamCount <= 0) return;
    UltrasoundDecoderConfig decoder =
        profile.hasDecoder ? profile.decoder : UltrasoundDecoderConfig{};
    decoder.groupId = status.currentGroup;
    decoder.pointCount = status.pointCount;
    decoder.beamCount = status.beamCount;
    decoder.amplitudeScale = status.maxAmplitudeValue;
    if (decoder.selectedBeam < 0 || decoder.selectedBeam >= decoder.beamCount)
      decoder.selectedBeam = 0;
    if (decoder.rangeEnd <= decoder.rangeStart) {
      decoder.rangeStart = 0.0;
      decoder.rangeEnd = 50.0;
    }
    decoderAppliedThisSession = true;
    configureUltrasoundDecoder(decoder);
  }

  // 设备就绪后自动读一次全量参数，把远端真实值写入本地镜像，
  // 调用方不需要自己在状态回调里再发 snapshot。
  void maybeAutoSnapshotUltrasound(const UltrasoundStatus& status) {
    if (!profileOptions.autoSnapshotUltrasound || ultrasoundSnapshotDone)
      return;
    if (!status.hardwareOnline) return;
    // 采集进行中绝不自动拉全量参数：一次要跑 80+ 次 SDK 读，会和 USB 数据
    // 接收抢 SDK 锁，把主机超声服务堵死（表现为 decoder/参数请求全部超时，
    // 超声看起来像“掉线”，工作台连上也卡）。需要参数时调用方自己先停采。
    if (status.capturing) return;
    ultrasoundSnapshotDone = true;
    ultrasoundParameterSnapshot();
  }

  Result applyDriveProfileToHost() {
    if (!profile.driveValid)
      return makeError(ErrorCode::InvalidArgument, "本地没有保存底盘配置");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，底盘配置未下发");
    const QJsonObject settings{
        {QStringLiteral("schema"), profile.driveSchema},
        {QStringLiteral("iniBase64"), profile.driveIniBase64}};
    const QJsonObject data{{QStringLiteral("settings"), settings}};
    if (session.request(QStringLiteral("robot.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "底盘配置下发被拒绝");
    return makeOk("底盘配置已下发（串口/PCAN 端口由主机保留）");
  }

  // ---- 超声预设 / 扫查精度 / 编码器标定 ---------------------------------
  Result requestUltrasoundPresets() {
    return submitSimple(QStringLiteral("ultrasound.presets.list"),
                        "超声预设列表请求已发送");
  }

  Result switchUltrasoundPreset(int presetId) {
    if (presetId < 0)
      return makeError(ErrorCode::InvalidArgument, "预设 ID 无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，预设未切换");
    const QJsonObject data{{QStringLiteral("presetId"), presetId}};
    if (session.request(QStringLiteral("ultrasound.presets.switch"), data) == 0)
      return makeError(ErrorCode::Transport, "预设切换请求被拒绝");
    return makeOk("超声预设切换请求已发送（主机会重配硬件参数）");
  }

  Result captureUltrasoundPreset(int presetId) {
    if (presetId < 0)
      return makeError(ErrorCode::InvalidArgument, "预设 ID 无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，预设未保存");
    const QJsonObject data{{QStringLiteral("presetId"), presetId}};
    if (session.request(QStringLiteral("ultrasound.presets.capture"), data) == 0)
      return makeError(ErrorCode::Transport, "预设保存请求被拒绝");
    return makeOk("已请求主机把当前硬件参数存为该预设");
  }

  Result requestUltrasoundScanPrecision() {
    return submitSimple(QStringLiteral("ultrasound.scan.precision.get"),
                        "扫查精度请求已发送");
  }

  Result setUltrasoundScanPrecision(int presetId, double mmPerCell) {
    if (presetId < 0 || !std::isfinite(mmPerCell) || mmPerCell <= 0.0)
      return makeError(ErrorCode::InvalidArgument, "预设 ID 或扫查精度无效");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，精度未保存");
    const QJsonObject data{{QStringLiteral("presetId"), presetId},
                           {QStringLiteral("mmPerCell"), mmPerCell}};
    if (session.request(QStringLiteral("ultrasound.scan.precision.set"), data) == 0)
      return makeError(ErrorCode::Transport, "扫查精度保存请求被拒绝");
    return makeOk("扫查精度已保存到主机预设");
  }

  Result requestUltrasoundEncoderCalibration() {
    return submitSimple(QStringLiteral("ultrasound.encoder.calibration.get"),
                        "编码器标定请求已发送");
  }

  Result setUltrasoundEncoderCalibration(const std::string& json) {
    QJsonObject data;
    if (!parseJsonObject(json, &data) || data.isEmpty())
      return makeError(ErrorCode::InvalidArgument,
                       "编码器标定参数必须是非空 JSON 对象");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，标定未保存");
    if (session.request(QStringLiteral("ultrasound.encoder.calibration.set"),
                        data) == 0)
      return makeError(ErrorCode::Transport, "编码器标定保存请求被拒绝");
    return makeOk("编码器标定已保存到主机");
  }

  Result ultrasoundResetEncoder() {
    return submitSimple(QStringLiteral("ultrasound.encoder.reset"),
                        "编码器复位请求已发送");
  }

  Result ultrasoundRefreshGeometry() {
    return submitSimple(QStringLiteral("ultrasound.geometry.refresh"),
                        "几何刷新请求已发送");
  }

  // ---- 外围设备 ---------------------------------------------------------
  Result scanLaserDevices() {
    return submitSimple(
        QStringLiteral("robot.laser.scan"),
        "激光相机扫描请求已发送（结果见 robot.laser.devices 事件）");
  }

  Result connectLaser(const std::string& serialNumber) {
    if (serialNumber.empty())
      return makeError(ErrorCode::InvalidArgument, "激光相机序列号为空");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，激光相机未连接");
    const QJsonObject data{
        {QStringLiteral("serialNumber"), fromUtf8(serialNumber)}};
    if (session.request(QStringLiteral("robot.laser.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "激光相机连接请求被拒绝");
    return makeOk("激光相机连接请求已发送");
  }

  Result disconnectLaser() {
    return submitSimple(QStringLiteral("robot.laser.disconnect"),
                        "激光相机断开请求已发送");
  }

  Result requestLaserStatus() {
    return submitSimple(QStringLiteral("robot.laser.status"),
                        "激光相机状态请求已发送");
  }

  Result connectImu(const std::string& port, int baudRate, int outputDivider) {
    if (port.empty())
      return makeError(ErrorCode::InvalidArgument, "IMU 串口为空");
    if (baudRate < 1200 || baudRate > 4000000)
      return makeError(ErrorCode::InvalidArgument, "IMU 波特率超出 1200..4000000");
    if (outputDivider < 1 || outputDivider > 200)
      return makeError(ErrorCode::InvalidArgument, "IMU 分频超出 1..200");
    if (!session.isReady())
      return makeError(ErrorCode::NotOnline, "未连接小车主机，IMU 未连接");
    const QJsonObject data{{QStringLiteral("port"), fromUtf8(port)},
                           {QStringLiteral("baudRate"), baudRate},
                           {QStringLiteral("outputDivider"), outputDivider}};
    if (session.request(QStringLiteral("robot.imu.connect"), data) == 0)
      return makeError(ErrorCode::Transport, "IMU 连接请求被拒绝");
    return makeOk("IMU 连接请求已发送");
  }

  Result disconnectImu() {
    return submitSimple(QStringLiteral("robot.imu.disconnect"),
                        "IMU 断开请求已发送");
  }

  Result requestImuStatus() {
    return submitSimple(QStringLiteral("robot.imu.status"), "IMU 状态请求已发送");
  }

  Result requestPeripheralsSnapshot() {
    return submitSimple(QStringLiteral("robot.peripherals.snapshot"),
                        "外围设备快照请求已发送");
  }

  Result requestSerialPorts() {
    return submitSimple(QStringLiteral("robot.peripherals.serialPorts"),
                        "串口列表请求已发送");
  }

  void publishCorrection(const CorrectionStatus& status) {
    lastCorrection = status;
    if (correctionHandler) correctionHandler(status);
  }

  void handleCameraPayload(quint16 streamId, const QByteArray& payload) {
    if (!cameraHandler) return;
    if (streamId < remote::kMediaStreamUsbCameraBase ||
        streamId >= remote::kMediaStreamUsbCameraBase + 2)
      return;
    QImage image;
    if (!remote::decodeImagePayload(payload, &image, nullptr) || image.isNull())
      return;
    CameraFrame frame;
    frame.slot = int(streamId - remote::kMediaStreamUsbCameraBase);
    frame.width = image.width();
    frame.height = image.height();
    frame.stride = int(image.bytesPerLine());
    frame.sizeBytes = int(image.sizeInBytes());
    frame.format = mapPixelFormat(image.format());
    frame.data = image.constBits();
    cameraHandler(frame);
  }

  // 激光完整轮廓：kind=10 / streamId=0x0204 / PAD1 type=6。
  // 点集展平成 x,y,z 交错数组后以只读视图回调（与 A 扫同样的免拷贝风格）。
  void handleLaserProfilePayload(quint16 streamId, const QByteArray& payload) {
    if (!laserProfileHandler) return;
    if (streamId != remote::kMediaStreamProfileObservation) return;
    QVector<QVector3D> points;
    crawling::LaserGapDetection detection;
    quint32 frameNumber = 0;
    qint64 receivedAtEpochMs = 0;
    if (!remote::decodeProfileObservation(payload, &points, &detection,
                                          &frameNumber, &receivedAtEpochMs,
                                          nullptr))
      return;
    if (points.isEmpty()) return;
    std::vector<float> coordinates;
    coordinates.reserve(size_t(points.size()) * 3);
    for (const QVector3D& point : points) {
      coordinates.push_back(point.x());
      coordinates.push_back(point.y());
      coordinates.push_back(point.z());
    }
    LaserProfileFrame frame;
    frame.pointCount = int(points.size());
    frame.coordinates = coordinates.data();
    frame.frameNumber = frameNumber;
    frame.receivedAtEpochMs = receivedAtEpochMs;
    frame.valid = detection.valid;
    frame.baselineSupported = detection.baselineSupported;
    frame.normalizedCenter = detection.normalizedCenter;
    frame.absoluteCenterRatio = detection.absoluteCenterRatio;
    frame.confidence = detection.confidence;
    frame.gapStartPx = detection.gapStartPx;
    frame.gapEndPx = detection.gapEndPx;
    frame.detectionReason = toUtf8(detection.profileDetectionReason);
    laserProfileHandler(frame);
  }

  void handleUltrasoundFrame(quint16 streamId, const QByteArray& payload) {
    if (!ultrasoundFrameHandler) return;
    if (streamId < remote::kMediaStreamUltrasoundLiveBase ||
        streamId >= remote::kMediaStreamUltrasoundLiveBase + 256)
      return;
    DecodedFrame decoded;
    if (!remote::decodeDecodedFrame(payload, &decoded, nullptr)) return;
    emitAScan(int(streamId - remote::kMediaStreamUltrasoundLiveBase), decoded, 0,
              lastDecoder.rangeStart, lastDecoder.rangeEnd);
  }

  void handleUltrasoundBatch(const QByteArray& payload) {
    if (!ultrasoundFrameHandler) return;
    GroupScanFrameBatch batch;
    if (!remote::decodeGroupScanBatch(payload, &batch, nullptr)) return;
    for (const GroupScanFrame& item : batch) {
      emitAScan(item.groupId, item.frame, item.triggeredModes, item.rangeStart,
                item.rangeEnd, item.bColumn, item.cColumn, item.cRow);
    }
  }

  // 把解码后的 QVector<QVector<qint16>> 直接以指针视图交给调用方，避免
  // 每条波束一次全量拷贝（A 扫 20 Hz × 1024 点 × N 波束）。
  void emitAScan(int groupId, const DecodedFrame& decoded, int scanModes,
                 double rangeStart, double rangeEnd, int bColumn = -1,
                 int cColumn = -1, int cRow = -1) {
    if (!ultrasoundFrameHandler || decoded.beams.isEmpty()) return;
    std::vector<const std::int16_t*> beamPointers;
    beamPointers.reserve(size_t(decoded.beams.size()));
    int pointCount = 0;
    for (const QVector<qint16>& beam : decoded.beams) {
      beamPointers.push_back(
          reinterpret_cast<const std::int16_t*>(beam.constData()));
      pointCount = qMax(pointCount, int(beam.size()));
    }
    std::vector<GateMeasurement> gates;
    if (!decoded.measurements.isEmpty()) {
      gates.reserve(size_t(decoded.measurements.size()));
      for (const BeamMeasurement& item : decoded.measurements) {
        GateMeasurement gate;
        gate.amplitudeA = item.amplitudeA;
        gate.amplitudeB = item.amplitudeB;
        gate.amplitudeC = item.amplitudeC;
        gate.amplitudeI = item.amplitudeI;
        gate.positionA = item.positionA;
        gate.positionB = item.positionB;
        gate.positionC = item.positionC;
        gate.positionI = item.positionI;
        gates.push_back(gate);
      }
    }

    UltrasoundAScanFrame view;
    view.groupId = groupId;
    view.beamCount = int(decoded.beams.size());
    view.pointCount = pointCount;
    view.sourceBeamCount = decoded.sourceBeamCount > 0
                               ? decoded.sourceBeamCount
                               : int(decoded.beams.size());
    view.selectedSourceBeam = decoded.selectedSourceBeam;
    view.scanModes = scanModes;
    view.frameNumber = std::uint32_t(decoded.tail.frameNumber);
    view.timestamp = decoded.tail.timestamp;
    view.rangeStartMm = rangeStart;
    view.rangeEndMm = rangeEnd;
    view.bColumn = bColumn;
    view.cColumn = cColumn;
    view.cRow = cRow;
    // 原始包尾：5 路编码器计数与各类错误标志，直接来自设备包。
    for (int index = 0; index < 5; ++index)
      view.encoders[index] = std::int32_t(decoded.tail.encoder[index]);
    view.encoderTriggerError = decoded.tail.encoderTriggerError;
    view.encoderSyncError = decoded.tail.encoderSyncError;
    view.frameError = decoded.tail.frameError;
    view.scannerIo = decoded.tail.scannerIo;
    view.multiFrame = decoded.tail.multiFrame;
    view.beams = beamPointers.data();
    view.measurementCount = int(gates.size());
    view.measurements = gates.empty() ? nullptr : gates.data();
    ultrasoundFrameHandler(view);
  }

  Config config;
  RemoteWorkbenchSession session;
  QTimer* enableTimer = nullptr;
  bool started = false;
  bool pendingEnable = false;

  ProfileOptions profileOptions;
  struct ProfileState {
    bool driveValid = false;
    int driveSchema = 0;
    QString driveIniBase64;
    bool ultrasoundValid = false;
    bool hasDecoder = false;
    QJsonObject ultrasoundParams;
    UltrasoundDecoderConfig decoder;
    QString savedAt;
  };
  ProfileState profile;
  bool decoderAppliedThisSession = false;
  bool ultrasoundSnapshotDone = false;
  double scanPrecisionMmPerCell_ = 0.0;
  double encoderCountsPerMm_[2] = {0.0, 0.0};
  double lastLinearMps = 0.0;
  double lastAngularRadps = 0.0;
  std::string statusText;
  std::string lastErrorCode;
  std::string lastErrorText;
  RobotStatus lastStatus;
  UltrasoundStatus lastUltrasoundStatus;
  CorrectionStatus lastCorrection;
  UltrasoundDecoderConfig lastDecoder;

  std::function<void(bool, const std::string&)> connectionHandler;
  std::function<void(const RobotStatus&)> statusHandler;
  std::function<void(const std::string&, bool, const std::string&,
                     const std::string&, const std::string&)>
      responseHandler;
  std::function<void(const std::string&, const std::string&)> eventHandler;
  std::function<void(const DriveConfig&)> driveConfigHandler;
  std::function<void(const CameraFrame&)> cameraHandler;
  std::function<void(const LaserProfileFrame&)> laserProfileHandler;
  std::function<void(const UltrasoundAScanFrame&)> ultrasoundFrameHandler;
  std::function<void(const CorrectionStatus&)> correctionHandler;
  std::function<void(const UltrasoundStatus&)> ultrasoundStatusHandler;
  std::function<void(LogLevel, const std::string&)> logHandler;
};

// ---------------------------------------------------------------------------
// RobotApi
// ---------------------------------------------------------------------------

RobotApi::RobotApi(Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RobotApi::~RobotApi() = default;

const Config& RobotApi::config() const { return impl_->config; }

void RobotApi::applyDriveConfig(const DriveConfig& drive) {
  if (!drive.loaded) return;
  impl_->config.maxLinearSpeedMps = drive.maximumLinearSpeedMps;
  impl_->config.maxAngularSpeedRadps = drive.maximumAngularSpeedRadps;
}

void RobotApi::start() { impl_->start(); }

void RobotApi::stop() { impl_->stop(); }

bool RobotApi::isOnline() const { return impl_->isOnline(); }

bool RobotApi::mediaReady() const { return impl_->session.mediaReady(); }

bool RobotApi::bulkReady() const { return impl_->session.bulkReady(); }

std::string RobotApi::statusText() const { return impl_->statusText; }

Result RobotApi::connectChassis() { return impl_->connectChassis(); }

Result RobotApi::disconnectChassis() { return impl_->disconnectChassis(); }

Result RobotApi::enable(bool enabled) { return impl_->enable(enabled); }

Result RobotApi::emergencyStop() { return impl_->emergencyStop(); }

Result RobotApi::reset() { return impl_->reset(); }

Result RobotApi::clearAlarm() { return impl_->clearAlarm(); }

Result RobotApi::takeControl() { return impl_->takeControl(); }

Result RobotApi::requestSnapshot() { return impl_->requestSnapshot(); }

Result RobotApi::requestDriveConfig() { return impl_->requestDriveConfig(); }

Result RobotApi::forward(double speedRatio) {
  return impl_->driveRatio(speedRatio, 0.0);
}

Result RobotApi::backward(double speedRatio) {
  return impl_->driveRatio(-speedRatio, 0.0);
}

// 差速混合器中正角速度表示右转（左轮为外侧轮），因此左转取负角速度。
Result RobotApi::turnLeft(double speedRatio) {
  return impl_->driveRatio(0.0, -speedRatio);
}

Result RobotApi::turnRight(double speedRatio) {
  return impl_->driveRatio(0.0, speedRatio);
}

Result RobotApi::moveVelocity(double linearMps, double angularRadps) {
  return impl_->moveVelocity(linearMps, angularRadps);
}

Result RobotApi::stopMotion() { return impl_->stopMotion(); }

Result RobotApi::stopMotionOnHost() { return impl_->stopMotionOnHost(); }

Result RobotApi::clampConnect() { return impl_->clampConnect(); }

Result RobotApi::clampDisconnect() { return impl_->clampDisconnect(); }

Result RobotApi::requestClampStatus() { return impl_->requestClampStatus(); }

Result RobotApi::requestClampSettings() { return impl_->requestClampSettings(); }

Result RobotApi::scanCameras() { return impl_->scanCameras(); }

Result RobotApi::setCameraDisplaySize(int slot, int width, int height) {
  return impl_->setCameraDisplaySize(slot, width, height);
}

Result RobotApi::requestUltrasoundPresets() {
  return impl_->requestUltrasoundPresets();
}

Result RobotApi::switchUltrasoundPreset(int presetId) {
  return impl_->switchUltrasoundPreset(presetId);
}

Result RobotApi::captureUltrasoundPreset(int presetId) {
  return impl_->captureUltrasoundPreset(presetId);
}

Result RobotApi::requestUltrasoundScanPrecision() {
  return impl_->requestUltrasoundScanPrecision();
}

Result RobotApi::setUltrasoundScanPrecision(int presetId, double mmPerCell) {
  return impl_->setUltrasoundScanPrecision(presetId, mmPerCell);
}

double RobotApi::scanPrecisionMmPerCell() const {
  return impl_->scanPrecisionMmPerCell_;
}

double RobotApi::encoderCountsPerMm(int axis) const {
  if (axis < 0 || axis > 1) return 0.0;
  return impl_->encoderCountsPerMm_[axis];
}

Result RobotApi::requestUltrasoundEncoderCalibration() {
  return impl_->requestUltrasoundEncoderCalibration();
}

Result RobotApi::setUltrasoundEncoderCalibration(const std::string& json) {
  return impl_->setUltrasoundEncoderCalibration(json);
}

Result RobotApi::ultrasoundResetEncoder() {
  return impl_->ultrasoundResetEncoder();
}

Result RobotApi::ultrasoundRefreshGeometry() {
  return impl_->ultrasoundRefreshGeometry();
}

Result RobotApi::scanLaserDevices() { return impl_->scanLaserDevices(); }

Result RobotApi::connectLaser(const std::string& serialNumber) {
  return impl_->connectLaser(serialNumber);
}

Result RobotApi::disconnectLaser() { return impl_->disconnectLaser(); }

Result RobotApi::requestLaserStatus() { return impl_->requestLaserStatus(); }

Result RobotApi::connectImu(const std::string& port, int baudRate,
                            int outputDivider) {
  return impl_->connectImu(port, baudRate, outputDivider);
}

Result RobotApi::disconnectImu() { return impl_->disconnectImu(); }

Result RobotApi::requestImuStatus() { return impl_->requestImuStatus(); }

Result RobotApi::requestPeripheralsSnapshot() {
  return impl_->requestPeripheralsSnapshot();
}

Result RobotApi::requestSerialPorts() { return impl_->requestSerialPorts(); }

Result RobotApi::startAutoCorrection(const std::string& settingsJson) {
  return impl_->startAutoCorrection(settingsJson);
}

Result RobotApi::stopAutoCorrection() { return impl_->stopAutoCorrection(); }

Result RobotApi::pauseAutoCorrection(bool paused) {
  return impl_->pauseAutoCorrection(paused);
}

Result RobotApi::requestAutoCorrectionStatus() {
  return impl_->requestAutoCorrectionStatus();
}

Result RobotApi::clampSetSpeed(int axis, int index, int percent) {
  return impl_->clampSetSpeed(axis, index, percent);
}

Result RobotApi::clampStopMotor(int axis, int index) {
  return impl_->clampStopMotor(axis, index);
}

Result RobotApi::clampStop() { return impl_->clampStop(); }

Result RobotApi::clampMoveToPosition(int axis, int index, double degrees) {
  return impl_->clampMoveToPosition(axis, index, degrees);
}

Result RobotApi::clampRequestPosition(int axis, int index) {
  return impl_->clampRequestPosition(axis, index);
}

Result RobotApi::clampRequestPositions() {
  return impl_->clampRequestPositions();
}

Result RobotApi::connectCamera(int slot, int deviceIndex, int fps,
                               bool flipHorizontal, bool flipVertical) {
  return impl_->connectCamera(slot, deviceIndex, fps, flipHorizontal,
                              flipVertical);
}

Result RobotApi::connectNetworkCamera(int slot, const std::string& url,
                                      bool flipHorizontal, bool flipVertical) {
  return impl_->connectNetworkCamera(slot, url, flipHorizontal, flipVertical);
}

Result RobotApi::disconnectCamera(int slot) {
  return impl_->disconnectCamera(slot);
}

Result RobotApi::disconnectAllCameras() {
  return impl_->disconnectAllCameras();
}

Result RobotApi::requestCameraStatus() { return impl_->requestCameraStatus(); }

Result RobotApi::ultrasoundConnect(const std::string& address, int deviceId) {
  return impl_->ultrasoundConnect(address, deviceId);
}

Result RobotApi::ultrasoundDisconnect() { return impl_->ultrasoundDisconnect(); }

Result RobotApi::ultrasoundStartAcquisition() {
  return impl_->ultrasoundStartAcquisition();
}

Result RobotApi::ultrasoundStopAcquisition() {
  return impl_->ultrasoundStopAcquisition();
}

Result RobotApi::requestUltrasoundStatus() {
  return impl_->requestUltrasoundStatus();
}

Result RobotApi::ultrasoundParametersSettled(bool resumeCapture) {
  return impl_->ultrasoundParametersSettled(resumeCapture);
}

Result RobotApi::ultrasoundParameterSnapshot() {
  return impl_->ultrasoundParameterSnapshot();
}

Result RobotApi::ultrasoundGetParameter(const std::string& method,
                                        const std::string& argsJson) {
  return impl_->ultrasoundGetParameter(method, argsJson);
}

Result RobotApi::ultrasoundSetParameter(const std::string& setMethod,
                                        const std::string& argsJson,
                                        const std::string& readbackMethod,
                                        const std::string& readbackArgsJson) {
  return impl_->ultrasoundSetParameter(setMethod, argsJson, readbackMethod,
                                       readbackArgsJson);
}

Result RobotApi::ultrasoundApplyParameters(const std::string& changesJson) {
  return impl_->ultrasoundApplyParameters(changesJson);
}

Result RobotApi::configureUltrasoundDecoder(
    const UltrasoundDecoderConfig& config) {
  return impl_->configureUltrasoundDecoder(config);
}

std::uint64_t RobotApi::request(const std::string& operation,
                                const std::string& dataJson) {
  return impl_->genericRequest(operation, dataJson);
}

void RobotApi::setConnectionHandler(
    std::function<void(bool, const std::string&)> handler) {
  impl_->connectionHandler = std::move(handler);
}

void RobotApi::setStatusHandler(
    std::function<void(const RobotStatus&)> handler) {
  impl_->statusHandler = std::move(handler);
}

void RobotApi::setResponseHandler(
    std::function<void(const std::string&, bool, const std::string&,
                       const std::string&, const std::string&)> handler) {
  impl_->responseHandler = std::move(handler);
}

void RobotApi::setEventHandler(
    std::function<void(const std::string&, const std::string&)> handler) {
  impl_->eventHandler = std::move(handler);
}

void RobotApi::setDriveConfigHandler(
    std::function<void(const DriveConfig&)> handler) {
  impl_->driveConfigHandler = std::move(handler);
}

void RobotApi::setCameraFrameHandler(
    std::function<void(const CameraFrame&)> handler) {
  impl_->cameraHandler = std::move(handler);
}

void RobotApi::setUltrasoundFrameHandler(
    std::function<void(const UltrasoundAScanFrame&)> handler) {
  impl_->ultrasoundFrameHandler = std::move(handler);
}

void RobotApi::setLaserProfileHandler(
    std::function<void(const LaserProfileFrame&)> handler) {
  impl_->laserProfileHandler = std::move(handler);
}

void RobotApi::setCorrectionStatusHandler(
    std::function<void(const CorrectionStatus&)> handler) {
  impl_->correctionHandler = std::move(handler);
}

void RobotApi::setUltrasoundStatusHandler(
    std::function<void(const UltrasoundStatus&)> handler) {
  impl_->ultrasoundStatusHandler = std::move(handler);
}

void RobotApi::setLogHandler(
    std::function<void(LogLevel, const std::string&)> handler) {
  impl_->logHandler = std::move(handler);
}

// ---------------------------------------------------------------------------
// 本地配置镜像
// ---------------------------------------------------------------------------

void RobotApi::setProfileOptions(const ProfileOptions& options) {
  impl_->profileOptions = options;
  if (options.enabled) impl_->loadProfileFromDisk();
}

ProfileOptions RobotApi::profileOptions() const { return impl_->profileOptions; }

std::string RobotApi::profilePath() const {
  return toUtf8(impl_->profileFilePath());
}

bool RobotApi::hasDriveProfile() const { return impl_->profile.driveValid; }

bool RobotApi::hasUltrasoundProfile() const {
  return impl_->profile.ultrasoundValid;
}

Result RobotApi::reloadProfile() {
  impl_->loadProfileFromDisk();
  if (!impl_->profile.driveValid && !impl_->profile.ultrasoundValid)
    return makeError(ErrorCode::InvalidArgument, "本地配置不存在或无法解析");
  return makeOk("本地配置已载入");
}

Result RobotApi::saveProfile() {
  if (!impl_->saveProfileToDisk())
    return makeError(ErrorCode::Transport, "本地配置写入失败");
  return makeOk("本地配置已保存");
}

Result RobotApi::applyDecoderProfile() {
  if (!impl_->profile.hasDecoder)
    return makeError(ErrorCode::InvalidArgument, "本地没有保存解码器配置");
  return impl_->configureUltrasoundDecoder(impl_->profile.decoder);
}

Result RobotApi::applyDriveProfileToHost() {
  return impl_->applyDriveProfileToHost();
}

}  // namespace pa1664
