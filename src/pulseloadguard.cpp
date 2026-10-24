#include "pulseloadguard.h"

#include "client.h"
#include "services/ultrasound_parameter_service.h"
#include <QJsonObject>
#include <QtMath>
#include <cmath>

namespace {

int safeRate(qint64 pulsesPerFrame, int requested)
{
    pulsesPerFrame = qMax<qint64>(1, pulsesPerFrame);
    requested = qMax(1, requested);
    return qMin(requested,
                qMax(1, int(PulseLoadGuard::MaximumEffectivePulseRate
                            / pulsesPerFrame)));
}

int derivedPulseCount(int scanTypeValue, int firstElement, int lastElement,
                      int aperture, int elementStep, double angleMin, double angleMax,
                      double angleStep, int sdkBeamCount)
{
    int derived = 1;
    switch (scanTypeValue) {
    case 1: // LinearScan
    case 3: // UltrasonicMicroscopy: element stepping is also linear
        // first/last are the inclusive probe-element range. Each law consumes
        // one aperture and then advances by elementStep.
        derived = qMax(0, qAbs(lastElement - firstElement) + 1 - qMax(1, aperture))
                      / qMax(1, qAbs(elementStep)) + 1;
        break;
    case 2: { // SectorialScan
        const double step = qAbs(angleStep);
        if (step > 0.000001)
            derived = int(std::floor(qAbs(angleMax - angleMin) / step + 0.000001)) + 1;
        break;
    }
    case 4: // Conventional UT
    default:
        derived = 1;
        break;
    }
    // getBeamCounts() is useful after the SDK has completed mapping, while the
    // derived value protects the pre-push window where it can still report 1.
    return qMax(1, qMax(derived, sdkBeamCount));
}

int configGroupPulseCount(const QJsonObject &group)
{
    return derivedPulseCount(group.value(QStringLiteral("scan_type")).toInt(4),
                             group.value(QStringLiteral("first_element")).toInt(0),
                             group.value(QStringLiteral("last_element")).toInt(0),
                             group.value(QStringLiteral("aperture")).toInt(1),
                             group.value(QStringLiteral("step")).toInt(1),
                             group.value(QStringLiteral("excitation_angle_1")).toDouble(0.0),
                             group.value(QStringLiteral("excitation_angle_2")).toDouble(0.0),
                             group.value(QStringLiteral("angle_step")).toDouble(1.0),
                             0);
}

} // namespace

namespace PulseLoadGuard {

Estimate fromConfiguration(const QJsonDocument &document)
{
    Estimate estimate;
    if (!document.isObject())
        return estimate;

    const QJsonObject root = document.object();
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    estimate.requestedFrameRate = qMax(1, global.value(QStringLiteral("frame_rate")).toInt(1));
    estimate.pulsesPerFrame = 0;
    const QJsonObject groups = root.value(QStringLiteral("groups")).toObject();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        estimate.pulsesPerFrame += configGroupPulseCount(it.value().toObject());
    estimate.pulsesPerFrame = qMax<qint64>(1, estimate.pulsesPerFrame);
    estimate.safeFrameRate = safeRate(estimate.pulsesPerFrame,
                                      estimate.requestedFrameRate);
    return estimate;
}

Estimate fromSdk(int requestedFrameRate)
{
    Estimate estimate;
    auto &sdk = pa1664::remote::UltrasoundParameterService::instance();
    estimate.requestedFrameRate = requestedFrameRate > 0
                                      ? requestedFrameRate
                                      : qMax(1, sdk.getFrameRate());
    estimate.pulsesPerFrame = 0;
    for (int groupId : sdk.getGroupsNo(true)) {
        int sdkBeams = 0;
        int derived = 1;
        try {
            sdkBeams = sdk.getBeamCounts(groupId);
            derived = derivedPulseCount(int(sdk.getScanType(groupId)),
                                        sdk.getBeamFirstElement(groupId),
                                        sdk.getBeamLastElement(groupId),
                                        sdk.getBeamAperture(groupId),
                                        sdk.getBeamElementStep(groupId),
                                        sdk.getBeamAngleMin(groupId),
                                        sdk.getBeamAngleMax(groupId),
                                        sdk.getBeamAngleStep(groupId),
                                        sdkBeams);
        } catch (...) {
            derived = qMax(1, sdkBeams);
        }
        estimate.pulsesPerFrame += derived;
    }
    estimate.pulsesPerFrame = qMax<qint64>(1, estimate.pulsesPerFrame);
    estimate.safeFrameRate = safeRate(estimate.pulsesPerFrame,
                                      estimate.requestedFrameRate);
    return estimate;
}

bool makeConfigurationSafe(QJsonDocument *document, QString *description)
{
    if (!document || !document->isObject())
        return false;
    const Estimate estimate = fromConfiguration(*document);
    if (!estimate.limited())
        return false;

    QJsonObject root = document->object();
    QJsonObject global = root.value(QStringLiteral("global")).toObject();
    global.insert(QStringLiteral("frame_rate"), estimate.safeFrameRate);
    root.insert(QStringLiteral("global"), global);
    document->setObject(root);
    if (description) {
        *description = QStringLiteral("多工作组有效脉冲为 %1/帧，PRF 已在配置推送前由 %2 Hz 限制为 %3 Hz")
                           .arg(estimate.pulsesPerFrame)
                           .arg(estimate.requestedFrameRate)
                           .arg(estimate.safeFrameRate);
    }
    return true;
}

bool enforceSdkLimit(QString *description)
{
    auto &sdk = pa1664::remote::UltrasoundParameterService::instance();
    const Estimate estimate = fromSdk();
    if (!estimate.limited())
        return true;
    if (!sdk.setFrameRate(estimate.safeFrameRate))
        return false;
    if (description) {
        *description = QStringLiteral("有效脉冲 %1/帧，PRF 已由 %2 Hz 限制为 %3 Hz（负载上限 %4）")
                           .arg(estimate.pulsesPerFrame)
                           .arg(estimate.requestedFrameRate)
                           .arg(estimate.safeFrameRate)
                           .arg(MaximumEffectivePulseRate);
    }
    return true;
}

} // namespace PulseLoadGuard
