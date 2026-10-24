#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "laser_correction_controller.h"
#include "rim302_protocol.h"

#include <QJsonObject>
#include <QString>

namespace pa1664::remote {

QJsonObject driveSettingsToJson(const crawling::DriveSettings& settings);
bool driveSettingsFromJson(const QJsonObject& json,
                           crawling::DriveSettings* settings,
                           QString* error = nullptr);

QJsonObject driveTelemetryToJson(const crawling::DriveTelemetry& telemetry);
bool driveTelemetryFromJson(const QJsonObject& json,
                            crawling::DriveTelemetry* telemetry,
                            QString* error = nullptr);

QString driveStateKey(crawling::DriveState state);

QJsonObject imuSampleToJson(const crawling::ImuSample& sample);
bool imuSampleFromJson(const QJsonObject& json, crawling::ImuSample* sample,
                       QString* error = nullptr);

QJsonObject laserCorrectionSettingsToJson(
    const crawling::LaserCorrectionSettings& settings);
bool laserCorrectionSettingsFromJson(
    const QJsonObject& json, crawling::LaserCorrectionSettings* settings,
    QString* error = nullptr);

QJsonObject laserCorrectionStatusToJson(
    const crawling::LaserCorrectionStatus& status);
bool laserCorrectionStatusFromJson(const QJsonObject& json,
                                   crawling::LaserCorrectionStatus* status,
                                   QString* error = nullptr);


}  // namespace pa1664::remote
