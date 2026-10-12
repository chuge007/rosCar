#pragma once

#include <QList>
#include <QString>

struct ParameterPresetInfo
{
    int id = 0;
    QString name;
};

class ParameterPresetStore
{
public:
    static constexpr int HardwareGroupId = 0;

    // Migrates an existing multi-group default_config.json into software
    // presets and rewrites the hardware configuration to exactly one group.
    static bool prepare(QString *message = nullptr, QString *error = nullptr);

    static QList<ParameterPresetInfo> presets(QString *error = nullptr);
    static int currentPresetId();
    static bool setCurrentPresetId(int presetId, QString *error = nullptr);

    static bool captureCurrentHardware(int presetId, QString *error = nullptr);
    static bool applyPresetToHardware(int presetId, QString *message = nullptr,
                                      QString *error = nullptr);
    static int copyPreset(int sourcePresetId, QString *error = nullptr);
    static bool removePreset(int presetId, int *newCurrentPresetId = nullptr,
                             QString *error = nullptr);
    static QList<int> importConfiguration(const QString &path,
                                          QString *error = nullptr);

    // 编码器外接轮径/分辨率是应用级标定，不是硬件参数，单独存到
    // default_config.json 顶层 "encoder" 对象里，随软件重启保持。
    static void loadEncoderCalibration(double *wheelDiameterMm, int *resolution);
    static bool saveEncoderCalibration(double wheelDiameterMm, int resolution,
                                       QString *error = nullptr);
};
