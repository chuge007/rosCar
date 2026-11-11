#include "ultrasoundconfigsanitizer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QtGlobal>
#include <cmath>

namespace {

bool integerValue(const QJsonValue &value, int *result)
{
    if (!result)
        return false;
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number))
            return false;
        *result = qRound(number);
        return true;
    }
    if (value.isString()) {
        bool ok = false;
        const int number = value.toString().trimmed().toInt(&ok);
        if (ok)
            *result = number;
        return ok;
    }
    if (value.isBool()) {
        *result = value.toBool() ? 1 : 0;
        return true;
    }
    return false;
}

QString valueDescription(const QJsonValue &value)
{
    if (value.isUndefined()) return QStringLiteral("缺失");
    if (value.isNull()) return QStringLiteral("null");
    if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                               : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
    if (value.isString()) return QStringLiteral("字符串“%1”").arg(value.toString());
    if (value.isArray()) return QStringLiteral("数组");
    if (value.isObject()) return QStringLiteral("对象");
    return QStringLiteral("未知类型");
}

bool normalizeInteger(QJsonObject *object, const QString &key, int minimum,
                      int maximum, int fallback, const QString &location,
                      QStringList *repairs, bool required)
{
    if (!object)
        return false;

    const bool existed = object->contains(key);
    const QJsonValue original = object->value(key);
    if (!existed && !required)
        return true;

    int value = fallback;
    const bool converted = integerValue(original, &value);
    value = qBound(minimum, converted ? value : fallback, maximum);

    const bool canonical = existed && original.isDouble()
                           && std::isfinite(original.toDouble())
                           && original.toDouble() == double(value);
    if (!canonical) {
        object->insert(key, value);
        if (repairs) {
            repairs->append(QStringLiteral("%1.%2：%3 → %4")
                                .arg(location, key, valueDescription(original))
                                .arg(value));
        }
    }
    return true;
}

void appendRepairs(QStringList *destination, const QStringList &source)
{
    if (destination)
        destination->append(source);
}

} // namespace

bool UltrasoundConfigSanitizer::normalizeGroup(QJsonObject *group,
                                               const QString &location,
                                               QStringList *repairs,
                                               QString *error)
{
    if (!group || group->isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 中没有有效的超声工作组参数").arg(location);
        return false;
    }

    // Some SDK diagnostics call this field "suppress", while its persisted
    // public name is "suppression". Migrate the alias and always serialize an
    // integer: the vendor range validator rejects strings/bools/null even when
    // their displayed value looks like zero.
    if (group->contains(QStringLiteral("suppress"))) {
        const QJsonValue aliasValue = group->value(QStringLiteral("suppress"));
        if (!group->contains(QStringLiteral("suppression")))
            group->insert(QStringLiteral("suppression"), aliasValue);
        group->remove(QStringLiteral("suppress"));
        if (repairs)
            repairs->append(QStringLiteral("%1.suppress：已迁移为 suppression")
                                .arg(location));
    }
    normalizeInteger(group, QStringLiteral("suppression"), 0, 100, 0,
                     location, repairs, true);

    // 配置 JSON（default_config.json / presets）中的通道字段用 0..7 的
    // 0-based 内部值持久化；而 SDK 公开接口 set/getTransmissionChannel 与
    // set/getReceptionChannel 是 1..8（见 parameterpanel.cpp 的 utChannels）。
    // 这里只对 JSON 持久化值做 0..7 钳制，不做 +1 换算。
    normalizeInteger(group, QStringLiteral("transmission_channel"), 0, 7, 0,
                     location, repairs, false);
    normalizeInteger(group, QStringLiteral("reception_channel"), 0, 7, 0,
                     location, repairs, false);
    normalizeInteger(group, QStringLiteral("dual_crystal_enable"), 0, 1, 0,
                     location, repairs, false);
    return true;
}

bool UltrasoundConfigSanitizer::normalizeHardwareDocument(QJsonDocument *document,
                                                          QStringList *repairs,
                                                          QString *error)
{
    if (!document || !document->isObject()) {
        if (error)
            *error = QStringLiteral("超声配置不是有效的 JSON 对象");
        return false;
    }

    QJsonObject root = document->object();
    QJsonObject groups = root.value(QStringLiteral("groups")).toObject();
    if (groups.isEmpty()) {
        if (error)
            *error = QStringLiteral("超声配置中没有工作组参数");
        return false;
    }

    QStringList localRepairs;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QJsonObject group = it.value().toObject();
        if (!normalizeGroup(&group, QStringLiteral("groups.%1").arg(it.key()),
                            &localRepairs, error))
            return false;
        it.value() = group;
    }
    root.insert(QStringLiteral("groups"), groups);
    document->setObject(root);
    appendRepairs(repairs, localRepairs);
    return true;
}

bool UltrasoundConfigSanitizer::normalizePresetDocument(QJsonDocument *document,
                                                        QStringList *repairs,
                                                        QString *error)
{
    if (!document || !document->isObject()) {
        if (error)
            *error = QStringLiteral("参数预设不是有效的 JSON 对象");
        return false;
    }

    QJsonObject root = document->object();
    QJsonObject presets = root.value(QStringLiteral("presets")).toObject();
    if (presets.isEmpty())
        return true;

    QStringList localRepairs;
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        QJsonObject preset = it.value().toObject();
        QJsonObject parameters = preset.value(QStringLiteral("parameters")).toObject();
        if (!normalizeGroup(&parameters,
                            QStringLiteral("presets.%1.parameters").arg(it.key()),
                            &localRepairs, error))
            return false;
        preset.insert(QStringLiteral("parameters"), parameters);
        it.value() = preset;
    }
    root.insert(QStringLiteral("presets"), presets);
    document->setObject(root);
    appendRepairs(repairs, localRepairs);
    return true;
}

bool UltrasoundConfigSanitizer::backupBeforeRepair(const QString &path,
                                                   QString *backupPath,
                                                   QString *error)
{
    const QFileInfo source(path);
    if (!source.exists())
        return true;

    const QDir applicationDir(QCoreApplication::applicationDirPath());
    const QString backupDirectory = applicationDir.filePath(QStringLiteral("config_backups"));
    if (!QDir().mkpath(backupDirectory)) {
        if (error)
            *error = QStringLiteral("无法创建配置备份目录：%1").arg(backupDirectory);
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString destination = QDir(backupDirectory).filePath(
        QStringLiteral("%1_before_auto_repair_%2.%3")
            .arg(source.completeBaseName(), timestamp,
                 source.suffix().isEmpty() ? QStringLiteral("json") : source.suffix()));
    if (!QFile::copy(path, destination)) {
        if (error)
            *error = QStringLiteral("无法备份待修复配置：%1").arg(path);
        return false;
    }
    if (backupPath)
        *backupPath = destination;
    return true;
}
