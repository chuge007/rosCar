#pragma once

#include <QJsonDocument>
#include <QString>
#include <QtGlobal>

namespace PulseLoadGuard {

// The PA-1664 becomes unstable before the vendor mapper's absolute limit.
// Keep the proven 12848 pulses/s application limit as operating headroom.
constexpr qint64 MaximumEffectivePulseRate = 12848;

struct Estimate
{
    qint64 pulsesPerFrame = 1;
    int requestedFrameRate = 1;
    int safeFrameRate = 1;

    qint64 effectivePulseRate() const
    {
        return qint64(requestedFrameRate) * pulsesPerFrame;
    }
    bool limited() const { return safeFrameRate < requestedFrameRate; }
};

Estimate fromConfiguration(const QJsonDocument &document);
Estimate fromSdk(int requestedFrameRate = -1);

// Updates global.frame_rate in-memory before Client::loadConfig() ever sees
// an unsafe multi-group configuration.
bool makeConfigurationSafe(QJsonDocument *document, QString *description = nullptr);

// Final protection for parameter edits and acquisition start.
bool enforceSdkLimit(QString *description = nullptr);

} // namespace PulseLoadGuard
