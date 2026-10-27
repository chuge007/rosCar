#include "robot_json_codec.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QSettings>
#include <QTemporaryFile>

#include <cmath>

namespace pa1664::remote {
namespace {

QJsonObject motorToJson(const crawling::MotorFeedback& motor) {
  QJsonObject value;
  value.insert(QStringLiteral("valid"), motor.valid);
  value.insert(QStringLiteral("wheelSpeedMps"), motor.wheelSpeedMps);
  value.insert(QStringLiteral("wheelPositionRad"), motor.wheelPositionRad);
  value.insert(QStringLiteral("motorSpeedDps"), motor.motorSpeedDps);
  value.insert(QStringLiteral("motorControlValue"), motor.motorControlValue);
  value.insert(QStringLiteral("torqueCurrentA"), motor.torqueCurrentA);
  value.insert(QStringLiteral("temperatureC"), motor.temperatureC);
  value.insert(QStringLiteral("receivedAtMs"),
               static_cast<double>(motor.receivedAtMs));
  return value;
}

void readMotor(const QJsonObject& value, crawling::MotorFeedback* motor) {
  if (!motor) return;
  motor->valid = value.value(QStringLiteral("valid")).toBool(motor->valid);
  motor->wheelSpeedMps = value.value(QStringLiteral("wheelSpeedMps"))
                             .toDouble(motor->wheelSpeedMps);
  motor->wheelPositionRad = value.value(QStringLiteral("wheelPositionRad"))
                                .toDouble(motor->wheelPositionRad);
  motor->motorSpeedDps = value.value(QStringLiteral("motorSpeedDps"))
                             .toDouble(motor->motorSpeedDps);
  motor->motorControlValue = value.value(QStringLiteral("motorControlValue"))
                                 .toDouble(motor->motorControlValue);
  motor->torqueCurrentA = value.value(QStringLiteral("torqueCurrentA"))
                              .toDouble(motor->torqueCurrentA);
  motor->temperatureC = value.value(QStringLiteral("temperatureC"))
                            .toInt(motor->temperatureC);
  motor->receivedAtMs = static_cast<qint64>(
      value.value(QStringLiteral("receivedAtMs"))
          .toDouble(static_cast<double>(motor->receivedAtMs)));
}

}  // namespace

QJsonObject driveSettingsToJson(const crawling::DriveSettings& settings) {
  // QSettings only works with file paths, so round-trip through a temp file.
  QTemporaryFile temp;
  if (!temp.open()) return {};
  const QString iniPath = temp.fileName();
  temp.close();
  {
    QSettings stored(iniPath, QSettings::IniFormat);
    settings.save(stored);
    stored.sync();
    if (stored.status() != QSettings::NoError) return {};
  }
  QFile iniFile(iniPath);
  if (!iniFile.open(QIODevice::ReadOnly)) return {};
  const QByteArray ini = iniFile.readAll();
  iniFile.close();
  QJsonObject json;
  json.insert(QStringLiteral("schema"),
              crawling::DriveSettings::kCurrentSettingsSchemaVersion);
  json.insert(QStringLiteral("iniBase64"),
              QString::fromLatin1(ini.toBase64()));
  return json;
}

bool driveSettingsFromJson(const QJsonObject& json,
                           crawling::DriveSettings* settings,
                           QString* error) {
  if (error) error->clear();
  if (!settings) {
    if (error) *error = QStringLiteral("输出设置为空");
    return false;
  }
  if (json.value(QStringLiteral("schema")).toInt() !=
      crawling::DriveSettings::kCurrentSettingsSchemaVersion) {
    if (error) *error = QStringLiteral("底盘配置版本不兼容");
    return false;
  }
  const QByteArray ini = QByteArray::fromBase64(
      json.value(QStringLiteral("iniBase64")).toString().toLatin1());
  if (ini.isEmpty() || ini.size() > 512 * 1024) {
    if (error) *error = QStringLiteral("底盘配置内容无效");
    return false;
  }
  QTemporaryFile temp;
  if (!temp.open() || temp.write(ini) != ini.size() || !temp.flush()) {
    if (error) *error = QStringLiteral("无法读取底盘配置");
    return false;
  }
  const QString iniPath = temp.fileName();
  temp.close();
  QSettings stored(iniPath, QSettings::IniFormat);
  if (!stored.contains(QStringLiteral("drive/settingsSchemaVersion"))) {
    if (error) *error = QStringLiteral("底盘配置缺少版本字段");
    return false;
  }
  *settings = crawling::DriveSettings::load(stored);
  const QString validation = settings->validationError();
  if (!validation.isEmpty()) {
    if (error) *error = validation;
    return false;
  }
  return true;
}

QJsonObject driveTelemetryToJson(const crawling::DriveTelemetry& telemetry) {
  QJsonObject json;
  json.insert(QStringLiteral("state"),
              static_cast<int>(telemetry.state));
  json.insert(QStringLiteral("stateKey"), driveStateKey(telemetry.state));
  json.insert(QStringLiteral("reason"), telemetry.reason);
  json.insert(QStringLiteral("commandFresh"), telemetry.commandFresh);
  json.insert(QStringLiteral("feedbackFresh"), telemetry.feedbackFresh);
  json.insert(QStringLiteral("targetLinearMps"), telemetry.targetLinearMps);
  json.insert(QStringLiteral("targetAngularRadps"), telemetry.targetAngularRadps);
  json.insert(QStringLiteral("appliedLinearMps"), telemetry.appliedLinearMps);
  json.insert(QStringLiteral("appliedAngularRadps"), telemetry.appliedAngularRadps);
  json.insert(QStringLiteral("leftTargetMps"), telemetry.leftTargetMps);
  json.insert(QStringLiteral("rightTargetMps"), telemetry.rightTargetMps);
  json.insert(QStringLiteral("left"), motorToJson(telemetry.left));
  json.insert(QStringLiteral("right"), motorToJson(telemetry.right));
  json.insert(QStringLiteral("synchronizationError"),
              telemetry.synchronizationError);
  json.insert(QStringLiteral("synchronizationCorrectionMps"),
              telemetry.synchronizationCorrectionMps);
  return json;
}

bool driveTelemetryFromJson(const QJsonObject& json,
                            crawling::DriveTelemetry* telemetry,
                            QString* error) {
  if (error) error->clear();
  if (!telemetry) {
    if (error) *error = QStringLiteral("输出遥测为空");
    return false;
  }
  const int state = json.value(QStringLiteral("state")).toInt(-1);
  if (state < static_cast<int>(crawling::DriveState::Disconnected) ||
      state > static_cast<int>(crawling::DriveState::EmergencyStop)) {
    if (error) *error = QStringLiteral("底盘状态字段无效");
    return false;
  }
  telemetry->state = static_cast<crawling::DriveState>(state);
  telemetry->reason = json.value(QStringLiteral("reason")).toString();
  telemetry->commandFresh =
      json.value(QStringLiteral("commandFresh")).toBool(false);
  telemetry->feedbackFresh =
      json.value(QStringLiteral("feedbackFresh")).toBool(false);
  telemetry->targetLinearMps =
      json.value(QStringLiteral("targetLinearMps")).toDouble();
  telemetry->targetAngularRadps =
      json.value(QStringLiteral("targetAngularRadps")).toDouble();
  telemetry->appliedLinearMps =
      json.value(QStringLiteral("appliedLinearMps")).toDouble();
  telemetry->appliedAngularRadps =
      json.value(QStringLiteral("appliedAngularRadps")).toDouble();
  telemetry->leftTargetMps =
      json.value(QStringLiteral("leftTargetMps")).toDouble();
  telemetry->rightTargetMps =
      json.value(QStringLiteral("rightTargetMps")).toDouble();
  readMotor(json.value(QStringLiteral("left")).toObject(), &telemetry->left);
  readMotor(json.value(QStringLiteral("right")).toObject(), &telemetry->right);
  telemetry->synchronizationError =
      json.value(QStringLiteral("synchronizationError")).toDouble();
  telemetry->synchronizationCorrectionMps =
      json.value(QStringLiteral("synchronizationCorrectionMps")).toDouble();
  return true;
}

QJsonObject imuSampleToJson(const crawling::ImuSample& sample) {
  return QJsonObject{
      {QStringLiteral("rollRad"), sample.rollRad},
      {QStringLiteral("pitchRad"), sample.pitchRad},
      {QStringLiteral("yawRad"), sample.yawRad},
      {QStringLiteral("gyroXRadps"), sample.gyroXRadps},
      {QStringLiteral("gyroYRadps"), sample.gyroYRadps},
      {QStringLiteral("gyroZRadps"), sample.gyroZRadps},
      {QStringLiteral("accelerationXMps2"), sample.accelerationXMps2},
      {QStringLiteral("accelerationYMps2"), sample.accelerationYMps2},
      {QStringLiteral("accelerationZMps2"), sample.accelerationZMps2}};
}

bool imuSampleFromJson(const QJsonObject& json, crawling::ImuSample* sample,
                       QString* error) {
  if (error) error->clear();
  if (!sample) {
    if (error) *error = QStringLiteral("IMU 输出为空");
    return false;
  }
  crawling::ImuSample value;
  const std::pair<const char*, double*> fields[] = {
      {"rollRad", &value.rollRad},
      {"pitchRad", &value.pitchRad},
      {"yawRad", &value.yawRad},
      {"gyroXRadps", &value.gyroXRadps},
      {"gyroYRadps", &value.gyroYRadps},
      {"gyroZRadps", &value.gyroZRadps},
      {"accelerationXMps2", &value.accelerationXMps2},
      {"accelerationYMps2", &value.accelerationYMps2},
      {"accelerationZMps2", &value.accelerationZMps2}};
  for (const auto& field : fields) {
    const QJsonValue item = json.value(QString::fromLatin1(field.first));
    if (!item.isDouble() || !std::isfinite(item.toDouble())) {
      if (error)
        *error = QStringLiteral("IMU 字段无效：%1")
                     .arg(QString::fromLatin1(field.first));
      return false;
    }
    *field.second = item.toDouble();
  }
  *sample = value;
  return true;
}

namespace {

QJsonObject profileTuningToJson(const crawling::ProfileWeldTuning& tuning) {
  return QJsonObject{
      {QStringLiteral("smoothingRadius"), tuning.smoothingRadius},
      {QStringLiteral("baselineEdgeRatio"), tuning.baselineEdgeRatio},
      {QStringLiteral("growNoiseSigma"), tuning.growNoiseSigma},
      {QStringLiteral("seedNoiseSigma"), tuning.seedNoiseSigma},
      {QStringLiteral("minimumWidthRatio"), tuning.minimumWidthRatio},
      {QStringLiteral("maximumWidthRatio"), tuning.maximumWidthRatio},
      {QStringLiteral("minimumSeedCount"), tuning.minimumSeedCount},
      {QStringLiteral("minimumSupportRatio"), tuning.minimumSupportRatio},
      {QStringLiteral("maximumCandidateHoleRatio"),
       tuning.maximumCandidateHoleRatio},
      {QStringLiteral("maximumInternalHoleRatio"),
       tuning.maximumInternalHoleRatio},
      {QStringLiteral("shoulderRatio"), tuning.shoulderRatio}};
}

void readProfileTuning(const QJsonObject& json,
                       crawling::ProfileWeldTuning* tuning) {
  if (!tuning) return;
  tuning->smoothingRadius = json.value(QStringLiteral("smoothingRadius"))
                                .toInt(tuning->smoothingRadius);
  tuning->baselineEdgeRatio = json.value(QStringLiteral("baselineEdgeRatio"))
                                  .toDouble(tuning->baselineEdgeRatio);
  tuning->growNoiseSigma = json.value(QStringLiteral("growNoiseSigma"))
                               .toDouble(tuning->growNoiseSigma);
  tuning->seedNoiseSigma = json.value(QStringLiteral("seedNoiseSigma"))
                               .toDouble(tuning->seedNoiseSigma);
  tuning->minimumWidthRatio = json.value(QStringLiteral("minimumWidthRatio"))
                                  .toDouble(tuning->minimumWidthRatio);
  tuning->maximumWidthRatio = json.value(QStringLiteral("maximumWidthRatio"))
                                  .toDouble(tuning->maximumWidthRatio);
  tuning->minimumSeedCount = json.value(QStringLiteral("minimumSeedCount"))
                                 .toInt(tuning->minimumSeedCount);
  tuning->minimumSupportRatio = json.value(QStringLiteral("minimumSupportRatio"))
                                    .toDouble(tuning->minimumSupportRatio);
  tuning->maximumCandidateHoleRatio =
      json.value(QStringLiteral("maximumCandidateHoleRatio"))
          .toDouble(tuning->maximumCandidateHoleRatio);
  tuning->maximumInternalHoleRatio =
      json.value(QStringLiteral("maximumInternalHoleRatio"))
          .toDouble(tuning->maximumInternalHoleRatio);
  tuning->shoulderRatio = json.value(QStringLiteral("shoulderRatio"))
                              .toDouble(tuning->shoulderRatio);
}

QJsonObject profileTemplateToJson(
    const crawling::ProfileWeldTemplate& profileTemplate) {
  QJsonArray shape;
  constexpr int kMaximumTemplateShapePoints = 4096;
  const int count = qMin(profileTemplate.normalizedShape.size(),
                         kMaximumTemplateShapePoints);
  for (int index = 0; index < count; ++index)
    shape.append(profileTemplate.normalizedShape.at(index));
  return QJsonObject{
      {QStringLiteral("name"), profileTemplate.name},
      {QStringLiteral("shape"), shape},
      {QStringLiteral("widthRatio"), profileTemplate.widthRatio},
      {QStringLiteral("peakRatio"), profileTemplate.peakRatio},
      {QStringLiteral("minimumWidthScale"),
       profileTemplate.minimumWidthScale},
      {QStringLiteral("maximumWidthScale"),
       profileTemplate.maximumWidthScale},
      {QStringLiteral("minimumSimilarity"),
       profileTemplate.minimumSimilarity},
      {QStringLiteral("enabled"), profileTemplate.enabled}};
}

crawling::ProfileWeldTemplate profileTemplateFromJson(const QJsonObject& json) {
  crawling::ProfileWeldTemplate value;
  value.name = json.value(QStringLiteral("name")).toString();
  const QJsonArray shape = json.value(QStringLiteral("shape")).toArray();
  constexpr int kMaximumTemplateShapePoints = 4096;
  value.normalizedShape.reserve(qMin(shape.size(),
                                     kMaximumTemplateShapePoints));
  for (int index = 0;
       index < shape.size() && index < kMaximumTemplateShapePoints; ++index) {
    const double sample = shape.at(index).toDouble();
    if (!std::isfinite(sample)) continue;
    value.normalizedShape.append(static_cast<float>(sample));
  }
  value.widthRatio = json.value(QStringLiteral("widthRatio")).toDouble();
  value.peakRatio =
      json.value(QStringLiteral("peakRatio")).toDouble(value.peakRatio);
  value.minimumWidthScale = json.value(QStringLiteral("minimumWidthScale"))
                                .toDouble(value.minimumWidthScale);
  value.maximumWidthScale = json.value(QStringLiteral("maximumWidthScale"))
                                .toDouble(value.maximumWidthScale);
  value.minimumSimilarity = json.value(QStringLiteral("minimumSimilarity"))
                                .toDouble(value.minimumSimilarity);
  value.enabled = json.value(QStringLiteral("enabled")).toBool(value.enabled);
  return value;
}

void readStatusDouble(const QJsonObject& json, const char* key, double* value) {
  if (value) *value = json.value(QString::fromLatin1(key)).toDouble(*value);
}

void readStatusBool(const QJsonObject& json, const char* key, bool* value) {
  if (value) *value = json.value(QString::fromLatin1(key)).toBool(*value);
}

void readStatusInt(const QJsonObject& json, const char* key, int* value) {
  if (value) *value = json.value(QString::fromLatin1(key)).toInt(*value);
}

}  // namespace

QJsonObject laserCorrectionSettingsToJson(
    const crawling::LaserCorrectionSettings& settings) {
  QJsonArray templates;
  constexpr int kMaximumTemplates = 128;
  const int templateCount = qMin(settings.detector.profileTemplates.size(),
                                 kMaximumTemplates);
  for (int index = 0; index < templateCount; ++index) {
    templates.append(
        profileTemplateToJson(settings.detector.profileTemplates.at(index)));
  }
  return QJsonObject{
      {QStringLiteral("targetSpeedMps"), settings.targetSpeedMps},
      {QStringLiteral("segmentLengthM"), settings.segmentLengthM},
      {QStringLiteral("proportionalGain"), settings.proportionalGain},
      {QStringLiteral("derivativeGain"), settings.derivativeGain},
      {QStringLiteral("imageTimeoutMs"), settings.imageTimeoutMs},
      {QStringLiteral("wheelRadiusM"), settings.wheelRadiusM},
      {QStringLiteral("trackWidthM"), settings.trackWidthM},
      {QStringLiteral("minimumInnerWheelRatio"),
       settings.minimumInnerWheelRatio},
      {QStringLiteral("maximumCurvatureRadPerM"),
       settings.maximumCurvatureRadPerM},
      {QStringLiteral("profileDetectionMode"),
       static_cast<int>(settings.detector.profileDetectionMode)},
      {QStringLiteral("profileTuning"),
       profileTuningToJson(settings.detector.profileTuning)},
      {QStringLiteral("profileTemplates"), templates}};
}

bool laserCorrectionSettingsFromJson(
    const QJsonObject& json, crawling::LaserCorrectionSettings* settings,
    QString* error) {
  if (error) error->clear();
  if (!settings) {
    if (error) *error = QStringLiteral("纠偏设置为空");
    return false;
  }
  crawling::LaserCorrectionSettings value = *settings;
  value.targetSpeedMps = json.value(QStringLiteral("targetSpeedMps"))
                             .toDouble(value.targetSpeedMps);
  value.segmentLengthM = json.value(QStringLiteral("segmentLengthM"))
                             .toDouble(value.segmentLengthM);
  value.proportionalGain = json.value(QStringLiteral("proportionalGain"))
                               .toDouble(value.proportionalGain);
  value.derivativeGain = json.value(QStringLiteral("derivativeGain"))
                             .toDouble(value.derivativeGain);
  value.imageTimeoutMs = json.value(QStringLiteral("imageTimeoutMs"))
                             .toInt(value.imageTimeoutMs);
  value.wheelRadiusM = json.value(QStringLiteral("wheelRadiusM"))
                           .toDouble(value.wheelRadiusM);
  value.trackWidthM = json.value(QStringLiteral("trackWidthM"))
                          .toDouble(value.trackWidthM);
  value.minimumInnerWheelRatio =
      json.value(QStringLiteral("minimumInnerWheelRatio"))
          .toDouble(value.minimumInnerWheelRatio);
  value.maximumCurvatureRadPerM =
      json.value(QStringLiteral("maximumCurvatureRadPerM"))
          .toDouble(value.maximumCurvatureRadPerM);
  const int mode = json.value(QStringLiteral("profileDetectionMode"))
                       .toInt(static_cast<int>(
                           value.detector.profileDetectionMode));
  if (mode < static_cast<int>(crawling::ProfileDetectionMode::BaselineShoulder) ||
      mode > static_cast<int>(crawling::ProfileDetectionMode::Hybrid)) {
    if (error) *error = QStringLiteral("轮廓检测模式无效");
    return false;
  }
  value.detector.profileDetectionMode =
      static_cast<crawling::ProfileDetectionMode>(mode);
  readProfileTuning(json.value(QStringLiteral("profileTuning")).toObject(),
                    &value.detector.profileTuning);
  value.detector.profileTemplates.clear();
  const QJsonArray templates =
      json.value(QStringLiteral("profileTemplates")).toArray();
  constexpr int kMaximumTemplates = 128;
  for (int index = 0;
       index < templates.size() && index < kMaximumTemplates; ++index) {
    if (templates.at(index).isObject()) {
      value.detector.profileTemplates.append(
          profileTemplateFromJson(templates.at(index).toObject()));
    }
  }
  if (!std::isfinite(value.targetSpeedMps) || value.targetSpeedMps <= 0.0 ||
      !std::isfinite(value.segmentLengthM) || value.segmentLengthM <= 0.0 ||
      !std::isfinite(value.proportionalGain) ||
      !std::isfinite(value.derivativeGain) ||
      !std::isfinite(value.wheelRadiusM) || value.wheelRadiusM <= 0.0 ||
      !std::isfinite(value.trackWidthM) || value.trackWidthM <= 0.0 ||
      !std::isfinite(value.minimumInnerWheelRatio) ||
      value.minimumInnerWheelRatio < 0.0 ||
      value.minimumInnerWheelRatio > 1.0 ||
      value.imageTimeoutMs < 50 || value.imageTimeoutMs > 5000) {
    if (error) *error = QStringLiteral("纠偏设置包含无效数值");
    return false;
  }
  *settings = value;
  return true;
}

QJsonObject laserCorrectionStatusToJson(
    const crawling::LaserCorrectionStatus& status) {
  return QJsonObject{
      {QStringLiteral("active"), status.active},
      {QStringLiteral("paused"), status.paused},
      {QStringLiteral("gapValid"), status.gapValid},
      {QStringLiteral("horizontalLaser"), status.horizontalLaser},
      {QStringLiteral("gapAbsoluteCenterRatio"),
       status.gapAbsoluteCenterRatio},
      {QStringLiteral("gapCenterRatio"), status.gapCenterRatio},
      {QStringLiteral("referenceGapAbsoluteCenterRatio"),
       status.referenceGapAbsoluteCenterRatio},
      {QStringLiteral("edgeBreakFallback"), status.edgeBreakFallback},
      {QStringLiteral("contourFallback"), status.contourFallback},
      {QStringLiteral("detectionHeld"), status.detectionHeld},
      {QStringLiteral("baselineSupported"), status.baselineSupported},
      {QStringLiteral("gapLateralM"), status.gapLateralM},
      {QStringLiteral("confidence"), status.confidence},
      {QStringLiteral("linearCommandMps"), status.linearCommandMps},
      {QStringLiteral("angularCommandRadps"), status.angularCommandRadps},
      {QStringLiteral("laserCenterErrorM"), status.laserCenterErrorM},
      {QStringLiteral("laserCenterCalibrated"),
       status.laserCenterCalibrated},
      {QStringLiteral("laserCenterOffset"), status.laserCenterOffset},
      {QStringLiteral("seamState"), static_cast<int>(status.seamState)},
      {QStringLiteral("seamStateDescription"),
       status.seamStateDescription},
      {QStringLiteral("phase"), status.phase},
      {QStringLiteral("reason"), status.reason},
      {QStringLiteral("supportingSamples"), status.supportingSamples},
      {QStringLiteral("collectedSamples"), status.collectedSamples},
      {QStringLiteral("cycleCount"), status.cycleCount}};
}

bool laserCorrectionStatusFromJson(const QJsonObject& json,
                                   crawling::LaserCorrectionStatus* status,
                                   QString* error) {
  if (error) error->clear();
  if (!status) {
    if (error) *error = QStringLiteral("纠偏状态输出为空");
    return false;
  }
  crawling::LaserCorrectionStatus value = *status;
  readStatusBool(json, "active", &value.active);
  readStatusBool(json, "paused", &value.paused);
  readStatusBool(json, "gapValid", &value.gapValid);
  readStatusBool(json, "horizontalLaser", &value.horizontalLaser);
  readStatusDouble(json, "gapAbsoluteCenterRatio",
                   &value.gapAbsoluteCenterRatio);
  readStatusDouble(json, "gapCenterRatio", &value.gapCenterRatio);
  readStatusDouble(json, "referenceGapAbsoluteCenterRatio",
                   &value.referenceGapAbsoluteCenterRatio);
  readStatusBool(json, "edgeBreakFallback", &value.edgeBreakFallback);
  readStatusBool(json, "contourFallback", &value.contourFallback);
  readStatusBool(json, "detectionHeld", &value.detectionHeld);
  readStatusBool(json, "baselineSupported", &value.baselineSupported);
  readStatusDouble(json, "gapLateralM", &value.gapLateralM);
  readStatusDouble(json, "confidence", &value.confidence);
  readStatusDouble(json, "linearCommandMps", &value.linearCommandMps);
  readStatusDouble(json, "angularCommandRadps", &value.angularCommandRadps);
  readStatusDouble(json, "laserCenterErrorM", &value.laserCenterErrorM);
  readStatusBool(json, "laserCenterCalibrated",
                 &value.laserCenterCalibrated);
  readStatusDouble(json, "laserCenterOffset", &value.laserCenterOffset);
  const int seamState = json.value(QStringLiteral("seamState"))
                            .toInt(static_cast<int>(value.seamState));
  value.seamState = static_cast<crawling::SeamLocalizationState>(seamState);
  value.seamStateDescription =
      json.value(QStringLiteral("seamStateDescription")).toString();
  value.phase = json.value(QStringLiteral("phase")).toString();
  value.reason = json.value(QStringLiteral("reason")).toString();
  readStatusInt(json, "supportingSamples", &value.supportingSamples);
  readStatusInt(json, "collectedSamples", &value.collectedSamples);
  readStatusInt(json, "cycleCount", &value.cycleCount);
  *status = value;
  return true;
}

QString driveStateKey(crawling::DriveState state) {
  switch (state) {
    case crawling::DriveState::Disconnected:
      return QStringLiteral("disconnected");
    case crawling::DriveState::Idle:
      return QStringLiteral("idle");
    case crawling::DriveState::Arming:
      return QStringLiteral("arming");
    case crawling::DriveState::Enabled:
      return QStringLiteral("enabled");
    case crawling::DriveState::Fault:
      return QStringLiteral("fault");
    case crawling::DriveState::EmergencyStop:
      return QStringLiteral("emergencyStop");
  }
  return QStringLiteral("unknown");
}

}  // namespace pa1664::remote
