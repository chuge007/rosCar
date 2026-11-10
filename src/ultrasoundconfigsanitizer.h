#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class UltrasoundConfigSanitizer final
{
public:
    static bool normalizeGroup(QJsonObject *group, const QString &location,
                               QStringList *repairs, QString *error = nullptr);
    static bool normalizeHardwareDocument(QJsonDocument *document,
                                          QStringList *repairs,
                                          QString *error = nullptr);
    static bool normalizePresetDocument(QJsonDocument *document,
                                        QStringList *repairs,
                                        QString *error = nullptr);

    // Preserve the user's original file before an automatic repair. The backup
    // is stored beside the application in config_backups/ and is never removed
    // automatically.
    static bool backupBeforeRepair(const QString &path, QString *backupPath,
                                   QString *error = nullptr);
};
