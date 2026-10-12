#include "parameterpresetstore.h"

#include "client.h"
#include "pulseloadguard.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <exception>

namespace {

QString configPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("default_config.json"));
}

QString presetsPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("parameter_presets.json"));
}

bool readJson(const QString &path, QJsonDocument *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    *document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document->isObject()) {
        if (error)
            *error = QStringLiteral("配置文件格式错误 %1：%2").arg(path, parseError.errorString());
        return false;
    }
    return true;
}

bool writeJson(const QString &path, const QJsonDocument &document, QString *error)
{
    QSaveFile file(path);
    const QByteArray bytes = document.toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QJsonObject makePreset(const QString &name, int frameRate, const QJsonObject &parameters)
{
    QJsonObject preset;
    preset.insert(QStringLiteral("name"), name);
    preset.insert(QStringLiteral("frame_rate"), qMax(1, frameRate));
    preset.insert(QStringLiteral("parameters"), parameters);
    return preset;
}

bool readPresets(QJsonDocument *document, QString *error)
{
    return readJson(presetsPath(), document, error);
}

bool writePresets(QJsonDocument document, QString *error)
{
    return writeJson(presetsPath(), document, error);
}

QJsonObject presetObject(const QJsonDocument &document, int presetId)
{
    return document.object().value(QStringLiteral("presets")).toObject()
        .value(QString::number(presetId)).toObject();
}

bool buildHardwareDocument(const QJsonObject &preset, QJsonDocument *hardware,
                           QString *limitMessage, QString *error)
{
    if (preset.isEmpty() || preset.value(QStringLiteral("parameters")).toObject().isEmpty()) {
        if (error)
            *error = QStringLiteral("参数预设为空，不能下发到硬件");
        return false;
    }
    QJsonObject root = hardware->object();
    QJsonObject global = root.value(QStringLiteral("global")).toObject();
    global.insert(QStringLiteral("frame_rate"),
                  qMax(1, preset.value(QStringLiteral("frame_rate")).toInt(
                              global.value(QStringLiteral("frame_rate")).toInt(1))));
    root.insert(QStringLiteral("global"), global);
    QJsonObject groups;
    groups.insert(QString::number(ParameterPresetStore::HardwareGroupId),
                  preset.value(QStringLiteral("parameters")).toObject());
    root.insert(QStringLiteral("groups"), groups);
    root.insert(QStringLiteral("currentGroup"), ParameterPresetStore::HardwareGroupId);
    hardware->setObject(root);
    PulseLoadGuard::makeConfigurationSafe(hardware, limitMessage);
    return true;
}

bool snapshotSdk(QJsonDocument *snapshot, QString *error)
{
    // Do not use QTemporaryFile here.  The vendor SDK also implements
    // saveConfig() with a temporary/rename sequence; giving it an already
    // created temporary target intermittently leaves either a zero-byte file
    // or a complete JSON file while returning false on Windows.
    const QDir appDir(QCoreApplication::applicationDirPath());
    QString lastFailure;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const QString path = appDir.filePath(
            QStringLiteral("preset_snapshot_%1.json")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QFile::remove(path);

        bool sdkReportedSaved = false;
        try {
            sdkReportedSaved = Client::getInstance().saveConfig(path.toStdString());
        } catch (...) {
            sdkReportedSaved = false;
        }

        // Some SDK builds finish the file just after saveConfig() returns and
        // can report false even though a complete snapshot was produced.  The
        // JSON itself is the source of truth; accept it only after parsing.
        for (int poll = 0; poll < 12; ++poll) {
            if (QFileInfo(path).size() > 0) {
                QJsonDocument candidate;
                QString parseFailure;
                if (readJson(path, &candidate, &parseFailure)) {
                    const QJsonObject groups = candidate.object()
                                                   .value(QStringLiteral("groups"))
                                                   .toObject();
                    if (!groups.isEmpty()) {
                        *snapshot = candidate;
                        QFile::remove(path);
                        return true;
                    }
                    lastFailure = QStringLiteral("SDK 快照没有工作组参数");
                } else {
                    lastFailure = parseFailure;
                }
            }
            QThread::msleep(20);
        }
        QFile::remove(path);
        if (!sdkReportedSaved && lastFailure.isEmpty())
            lastFailure = QStringLiteral("SDK 报告保存失败且未生成有效 JSON");
    }

    if (error)
        *error = QStringLiteral("SDK 无法保存当前硬件参数：%1")
                     .arg(lastFailure.isEmpty() ? QStringLiteral("连续三次快照失败")
                                                : lastFailure);
    return false;
}

} // namespace

bool ParameterPresetStore::prepare(QString *message, QString *error)
{
    // Remove crash/SDK-failure leftovers from earlier sessions.  Active
    // snapshots use UUID names and are never shared between calls.
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    for (const QString &name : applicationDir.entryList(
             {QStringLiteral("preset_snapshot_*.json")}, QDir::Files))
        QFile::remove(applicationDir.filePath(name));

    QJsonDocument hardware;
    if (!readJson(configPath(), &hardware, error))
        return false;

    QJsonDocument presetDocument;
    const bool alreadyExists = QFile::exists(presetsPath());
    if (alreadyExists && !readPresets(&presetDocument, error))
        return false;

    QJsonObject presetRoot = presetDocument.isObject() ? presetDocument.object() : QJsonObject();
    QJsonObject presetEntries = presetRoot.value(QStringLiteral("presets")).toObject();
    const QJsonObject hardwareRoot = hardware.object();
    const QJsonObject hardwareGroups = hardwareRoot.value(QStringLiteral("groups")).toObject();
    int currentPreset = presetRoot.value(QStringLiteral("currentPreset")).toInt(
        hardwareRoot.value(QStringLiteral("currentGroup")).toInt(0));
    const int frameRate = hardwareRoot.value(QStringLiteral("global")).toObject()
                              .value(QStringLiteral("frame_rate")).toInt(1);

    if (presetEntries.isEmpty()) {
        for (auto it = hardwareGroups.constBegin(); it != hardwareGroups.constEnd(); ++it) {
            bool ok = false;
            const int id = it.key().toInt(&ok);
            if (!ok)
                continue;
            presetEntries.insert(QString::number(id),
                                 makePreset(QStringLiteral("预设 %1").arg(id),
                                            frameRate, it.value().toObject()));
        }
    }
    if (presetEntries.isEmpty()) {
        if (error)
            *error = QStringLiteral("默认配置中没有可迁移的工作组参数");
        return false;
    }
    if (!presetEntries.contains(QString::number(currentPreset))) {
        bool ok = false;
        currentPreset = presetEntries.constBegin().key().toInt(&ok);
        if (!ok)
            currentPreset = 0;
    }

    presetRoot.insert(QStringLiteral("version"), 1);
    presetRoot.insert(QStringLiteral("currentPreset"), currentPreset);
    presetRoot.insert(QStringLiteral("presets"), presetEntries);
    presetDocument.setObject(presetRoot);

    QString limitMessage;
    if (!buildHardwareDocument(presetEntries.value(QString::number(currentPreset)).toObject(),
                               &hardware, &limitMessage, error))
        return false;
    if (!writePresets(presetDocument, error) || !writeJson(configPath(), hardware, error))
        return false;

    if (message) {
        *message = QStringLiteral("参数预设模式已启用：保存 %1 个软件预设，硬件固定使用工作组 0")
                       .arg(presetEntries.size());
        if (!limitMessage.isEmpty())
            *message += QStringLiteral("；") + limitMessage;
    }
    return true;
}

QList<ParameterPresetInfo> ParameterPresetStore::presets(QString *error)
{
    QJsonDocument document;
    QList<ParameterPresetInfo> result;
    if (!readPresets(&document, error))
        return result;
    const QJsonObject entries = document.object().value(QStringLiteral("presets")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (!ok)
            continue;
        const QString name = it.value().toObject().value(QStringLiteral("name"))
                                 .toString(QStringLiteral("预设 %1").arg(id));
        result.append(ParameterPresetInfo{id, name});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.id < b.id;
    });
    return result;
}

int ParameterPresetStore::currentPresetId()
{
    QJsonDocument document;
    if (!readPresets(&document, nullptr))
        return 0;
    return document.object().value(QStringLiteral("currentPreset")).toInt(0);
}

bool ParameterPresetStore::setCurrentPresetId(int presetId, QString *error)
{
    QJsonDocument document;
    if (!readPresets(&document, error))
        return false;
    QJsonObject root = document.object();
    if (!root.value(QStringLiteral("presets")).toObject().contains(QString::number(presetId))) {
        if (error)
            *error = QStringLiteral("参数预设 %1 不存在").arg(presetId);
        return false;
    }
    root.insert(QStringLiteral("currentPreset"), presetId);
    document.setObject(root);
    return writePresets(document, error);
}

bool ParameterPresetStore::captureCurrentHardware(int presetId, QString *error)
{
    QJsonDocument existingHardware;
    if (!readJson(configPath(), &existingHardware, error))
        return false;
    QJsonDocument snapshot;
    if (!snapshotSdk(&snapshot, error))
        return false;

    // The vendor SDK serializes only fields it owns. Keep application-level
    // calibration alongside every regenerated hardware snapshot.
    const QJsonObject existingRoot = existingHardware.object();
    QJsonObject snapshotWithApplicationFields = snapshot.object();
    if (existingRoot.contains(QStringLiteral("encoder")))
        snapshotWithApplicationFields.insert(QStringLiteral("encoder"),
                                             existingRoot.value(QStringLiteral("encoder")));
    snapshot.setObject(snapshotWithApplicationFields);

    QJsonDocument presetsDocument;
    if (!readPresets(&presetsDocument, error))
        return false;

    QJsonObject root = presetsDocument.object();
    QJsonObject entries = root.value(QStringLiteral("presets")).toObject();
    QJsonObject oldPreset = entries.value(QString::number(presetId)).toObject();
    const QJsonObject snapshotRoot = snapshot.object();
    const QJsonObject groups = snapshotRoot.value(QStringLiteral("groups")).toObject();
    QJsonObject group = groups.value(QString::number(HardwareGroupId)).toObject();
    if (group.isEmpty() && !groups.isEmpty())
        group = groups.constBegin().value().toObject();
    if (group.isEmpty()) {
        if (error)
            *error = QStringLiteral("SDK 快照中没有硬件工作组 0");
        return false;
    }
    const int frameRate = snapshotRoot.value(QStringLiteral("global")).toObject()
                              .value(QStringLiteral("frame_rate")).toInt(1);
    entries.insert(QString::number(presetId),
                   makePreset(oldPreset.value(QStringLiteral("name"))
                                  .toString(QStringLiteral("预设 %1").arg(presetId)),
                              frameRate, group));
    root.insert(QStringLiteral("presets"), entries);
    presetsDocument.setObject(root);
    if (!writePresets(presetsDocument, error))
        return false;

    QString limitMessage;
    if (!buildHardwareDocument(entries.value(QString::number(presetId)).toObject(),
                               &snapshot, &limitMessage, error))
        return false;
    return writeJson(configPath(), snapshot, error);
}

bool ParameterPresetStore::applyPresetToHardware(int presetId, QString *message, QString *error)
{
    QJsonDocument presetsDocument;
    QJsonDocument hardware;
    if (!readPresets(&presetsDocument, error) || !readJson(configPath(), &hardware, error))
        return false;
    const QJsonObject preset = presetObject(presetsDocument, presetId);
    QString limitMessage;
    if (!buildHardwareDocument(preset, &hardware, &limitMessage, error)
        || !writeJson(configPath(), hardware, error))
        return false;

    Client &sdk = Client::getInstance();
    try {
        sdk.startCapture(false);
        // Remove every stale hardware group. The one desired group is recreated
        // from the normalized single-group JSON immediately afterwards.
        const QVector<int> known = sdk.getGroupsNo(false);
        for (int groupId : known) {
            if (groupId == HardwareGroupId)
                continue;
            try { sdk.removeGroup(groupId); } catch (...) {}
        }
        if (!sdk.loadConfig(configPath().toStdString())) {
            if (error)
                *error = QStringLiteral("SDK 加载参数预设失败");
            return false;
        }
        sdk.setCurrentGroup(HardwareGroupId);
        QString runtimeLimit;
        if (!PulseLoadGuard::enforceSdkLimit(&runtimeLimit)) {
            if (error)
                *error = QStringLiteral("SDK 未接受当前预设的安全 PRF");
            return false;
        }
        sdk.pushConfigToDevice();
        if (message) {
            *message = QStringLiteral("已将参数预设 %1 加载到唯一硬件工作组 0").arg(presetId);
            if (!limitMessage.isEmpty())
                *message += QStringLiteral("；") + limitMessage;
            if (!runtimeLimit.isEmpty())
                *message += QStringLiteral("；") + runtimeLimit;
        }
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QStringLiteral("切换参数预设时 SDK 异常：%1")
                         .arg(QString::fromLocal8Bit(e.what()));
    } catch (...) {
        if (error)
            *error = QStringLiteral("切换参数预设时 SDK 抛出未知异常");
    }
    return false;
}

int ParameterPresetStore::copyPreset(int sourcePresetId, QString *error)
{
    QJsonDocument document;
    if (!readPresets(&document, error))
        return -1;
    QJsonObject root = document.object();
    QJsonObject entries = root.value(QStringLiteral("presets")).toObject();
    const QJsonObject source = entries.value(QString::number(sourcePresetId)).toObject();
    if (source.isEmpty()) {
        if (error)
            *error = QStringLiteral("源参数预设不存在");
        return -1;
    }
    int newId = 0;
    while (entries.contains(QString::number(newId)))
        ++newId;
    QJsonObject copy = source;
    copy.insert(QStringLiteral("name"), QStringLiteral("预设 %1").arg(newId));
    entries.insert(QString::number(newId), copy);
    root.insert(QStringLiteral("presets"), entries);
    document.setObject(root);
    return writePresets(document, error) ? newId : -1;
}

bool ParameterPresetStore::removePreset(int presetId, int *newCurrentPresetId, QString *error)
{
    QJsonDocument document;
    if (!readPresets(&document, error))
        return false;
    QJsonObject root = document.object();
    QJsonObject entries = root.value(QStringLiteral("presets")).toObject();
    if (entries.size() <= 1) {
        if (error)
            *error = QStringLiteral("至少必须保留一个参数预设");
        return false;
    }
    entries.remove(QString::number(presetId));
    int current = root.value(QStringLiteral("currentPreset")).toInt(0);
    if (current == presetId || !entries.contains(QString::number(current))) {
        bool ok = false;
        current = entries.constBegin().key().toInt(&ok);
        if (!ok)
            current = 0;
    }
    root.insert(QStringLiteral("currentPreset"), current);
    root.insert(QStringLiteral("presets"), entries);
    document.setObject(root);
    if (!writePresets(document, error))
        return false;
    if (newCurrentPresetId)
        *newCurrentPresetId = current;
    return true;
}

QList<int> ParameterPresetStore::importConfiguration(const QString &path, QString *error)
{
    QJsonDocument imported;
    QJsonDocument document;
    QList<int> added;
    if (!readJson(path, &imported, error) || !readPresets(&document, error))
        return added;
    const QJsonObject importedRoot = imported.object();
    const QJsonObject importedGroups = importedRoot.value(QStringLiteral("groups")).toObject();
    if (importedGroups.isEmpty()) {
        if (error)
            *error = QStringLiteral("导入配置中没有工作组参数");
        return added;
    }
    const int frameRate = importedRoot.value(QStringLiteral("global")).toObject()
                              .value(QStringLiteral("frame_rate")).toInt(1);
    QJsonObject root = document.object();
    QJsonObject entries = root.value(QStringLiteral("presets")).toObject();
    for (auto it = importedGroups.constBegin(); it != importedGroups.constEnd(); ++it) {
        int newId = 0;
        while (entries.contains(QString::number(newId)))
            ++newId;
        entries.insert(QString::number(newId),
                       makePreset(QStringLiteral("导入预设 %1").arg(newId),
                                  frameRate, it.value().toObject()));
        added.append(newId);
    }
    root.insert(QStringLiteral("presets"), entries);
    document.setObject(root);
    if (!writePresets(document, error))
        added.clear();
    return added;
}

void ParameterPresetStore::loadEncoderCalibration(double *wheelDiameterMm, int *resolution)
{
    *wheelDiameterMm = 500.0;
    *resolution = 4096;
    QJsonDocument document;
    if (!readJson(configPath(), &document, nullptr))
        return;
    const QJsonObject encoder = document.object().value(QStringLiteral("encoder")).toObject();
    if (encoder.contains(QStringLiteral("wheel_diameter_mm")))
        *wheelDiameterMm = qMax(0.001,
                                encoder.value(QStringLiteral("wheel_diameter_mm")).toDouble(500.0));
    if (encoder.contains(QStringLiteral("resolution")))
        *resolution = qBound(1, encoder.value(QStringLiteral("resolution")).toInt(4096), 1000000);
}

bool ParameterPresetStore::saveEncoderCalibration(double wheelDiameterMm, int resolution,
                                                  QString *error)
{
    QJsonDocument document;
    if (!readJson(configPath(), &document, error))
        return false;
    QJsonObject root = document.object();
    QJsonObject encoder = root.value(QStringLiteral("encoder")).toObject();
    encoder.insert(QStringLiteral("wheel_diameter_mm"), wheelDiameterMm);
    encoder.insert(QStringLiteral("resolution"), resolution);
    root.insert(QStringLiteral("encoder"), encoder);
    document.setObject(root);
    return writeJson(configPath(), document, error);
}
