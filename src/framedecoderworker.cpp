#include "framedecoderworker.h"
#include "devicecontroller.h"
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <climits>

namespace {
constexpr qint64 kGroupScanUiEmitIntervalMs = 20;
}

FrameDecoderWorker::FrameDecoderWorker(DeviceController *source)
    : m_source(source)
{
}

void FrameDecoderWorker::startPolling()
{
    if (m_pollTimer)
        return;
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    m_pollTimer->setInterval(33);
    connect(m_pollTimer, &QTimer::timeout,
            this, &FrameDecoderWorker::processLatestPacket);
    m_pollTimer->start();
    m_telemetryClock.start();
    m_groupScanEmitClock.start();
}

bool FrameDecoderWorker::triggerGeometryChanged(const GroupScanSettings &left,
                                                const GroupScanSettings &right)
{
    return left.groupOffsetWords != right.groupOffsetWords
           || left.beamCount != right.beamCount
           || left.pointCount != right.pointCount
           || (left.scanModes & 0x6) != (right.scanModes & 0x6)
           || left.selectedBeam != right.selectedBeam
           || left.encoderPrecision != right.encoderPrecision;
}

void FrameDecoderWorker::updateSettings(QVector<GroupScanSettings> groups,
                                        int displayGroupId)
{
    QHash<int, GroupScanSettings> previous;
    for (const GroupScanSettings &group : qAsConst(m_groups))
        previous.insert(group.groupId, group);

    QSet<int> presentGroups;
    bool flushQueue = false;
    bool hasEncoderScans = false;
    for (GroupScanSettings &group : groups) {
        group.groupOffsetWords = qMax(0, group.groupOffsetWords);
        group.beamCount = qMax(1, group.beamCount);
        group.pointCount = qMax(1, group.pointCount);
        group.scanModes &= 0x7;
        group.selectedBeam = qBound(0, group.selectedBeam, group.beamCount - 1);
        group.amplitudeScale = qMax(1, group.amplitudeScale);
        group.encoderPrecision = qMax(1, group.encoderPrecision);
        presentGroups.insert(group.groupId);
        hasEncoderScans |= (group.scanModes & 0x6) != 0;

        const auto old = previous.constFind(group.groupId);
        if (old == previous.constEnd()
            || triggerGeometryChanged(old.value(), group)) {
            m_triggerStates.remove(group.groupId);
            flushQueue = true;
        }
    }
    for (auto it = m_triggerStates.begin(); it != m_triggerStates.end();) {
        if (!presentGroups.contains(it.key())) {
            it = m_triggerStates.erase(it);
            flushQueue = true;
        } else {
            ++it;
        }
    }

    m_groups = std::move(groups);
    m_displayGroupId = displayGroupId;
    m_hasEncoderScans = hasEncoderScans;
    if (m_source) {
        if (flushQueue) {
            m_source->setCScanPacketCollection(false);
            m_pendingUiScanFrames.clear();
            m_pendingUiScanDeviceId = -1;
        }
        m_source->setCScanPacketCollection(m_hasEncoderScans);
    }
    if (m_pollTimer)
        m_pollTimer->setInterval(m_hasEncoderScans ? 5 : 33);
}

const GroupScanSettings *FrameDecoderWorker::displaySettings() const
{
    for (const GroupScanSettings &group : m_groups) {
        if (group.groupId == m_displayGroupId)
            return &group;
    }
    // During group replacement the requested display group can disappear for
    // one settings update. Keep consuming/displaying packets with the first
    // valid geometry instead of making the A-scan appear to stop.
    return m_groups.isEmpty() ? nullptr : &m_groups.constFirst();
}

void FrameDecoderWorker::processLatestPacket()
{
    if (m_hasEncoderScans)
        processEncoderTriggeredPackets();
    if (!m_displayClock.isValid() || m_displayClock.elapsed() >= 33) {
        m_displayClock.restart();
        processDisplayPacket();
    }
    emitTelemetryIfNeeded();
}

void FrameDecoderWorker::processDisplayPacket()
{
    QElapsedTimer decodeClock;
    decodeClock.start();
    const GroupScanSettings *settings = displaySettings();
    if (!settings)
        return;
    RawPacketBuffer packet;
    if (!m_source || !m_source->takeLatestDisplayPacket(packet))
        return;
    if (!packet.isValid()) {
        emit decodeFailed(QStringLiteral("收到无效的原始数据缓冲区"));
        return;
    }
    const QByteArray packetView = QByteArray::fromRawData(packet.bytes.data(),
                                                          packet.length);
    DecodedFrame frame;
    QString error;
    bool decoded = false;
    if ((settings->scanModes & 0x1) != 0) {
        decoded = PacketDecoder::decodeEScan(packetView,
                                             settings->groupOffsetWords,
                                             settings->beamCount,
                                             settings->pointCount,
                                             settings->selectedBeam,
                                             double(settings->amplitudeScale),
                                             frame, &error);
    } else {
        decoded = PacketDecoder::decodeSelectedBeam(packetView,
                                                    settings->groupOffsetWords,
                                                    settings->beamCount,
                                                    settings->pointCount,
                                                    settings->selectedBeam,
                                                    frame, &error);
    }
    if (!decoded) {
        emit decodeFailed(error);
        return;
    }
    if (m_recordingEnabled && !m_hasEncoderScans)
        emit recordingPacketReady(packet);
    emit frameDecoded(std::move(frame), settings->groupId, packet.deviceId);
    m_lastDisplayDecodeDurationUs = decodeClock.nsecsElapsed() / 1000;
}

void FrameDecoderWorker::processEncoderTriggeredPackets()
{
    if (!m_source || !m_hasEncoderScans)
        return;

    flushPendingGroupScanBatch(false);
    if (m_scanPacketIndex >= m_scanPackets.size()) {
        m_scanPackets.clear();
        m_scanPacketIndex = 0;
        QElapsedTimer takeClock;
        takeClock.start();
        const int taken = m_source->takeCScanPackets(m_scanPackets);
        m_lastTakeDurationUs = takeClock.nsecsElapsed() / 1000;
        if (taken <= 0) {
            emitTelemetryIfNeeded();
            return;
        }
    }

    GroupScanFrameBatch batch;
    batch.reserve(qMin(256, m_scanPackets.size() - m_scanPacketIndex));
    int batchDeviceId = -1;
    m_lastScanBatchSize = m_scanPackets.size() - m_scanPacketIndex;
    const auto flushBatch = [&]() {
        if (batch.isEmpty())
            return;
        if (!m_pendingUiScanFrames.isEmpty() &&
            m_pendingUiScanDeviceId != batchDeviceId) {
            flushPendingGroupScanBatch(true);
        }
        if (m_pendingUiScanFrames.isEmpty())
            m_pendingUiScanDeviceId = batchDeviceId;
        for (GroupScanFrame &frame : batch)
            m_pendingUiScanFrames.append(std::move(frame));
        batch = {};
        batch.reserve(256);
        if (m_pendingUiScanFrames.size() >= 256)
            flushPendingGroupScanBatch(true);
    };

    QElapsedTimer decodeClock;
    decodeClock.start();
    while (m_scanPacketIndex < m_scanPackets.size()
           && decodeClock.nsecsElapsed() < 6000000) {
        const RawPacketBuffer &packet = m_scanPackets.at(m_scanPacketIndex++);
        ++m_scanPacketsProcessed;
        if (!packet.isValid())
            continue;
        const QByteArray packetView = QByteArray::fromRawData(packet.bytes.data(),
                                                              packet.length);
        PacketTail tail;
        QString error;
        if (!PacketDecoder::decodePacketTail(packetView, tail, &error)) {
            emit decodeFailed(error);
            continue;
        }

        bool packetUsed = false;
        for (const GroupScanSettings &settings : qAsConst(m_groups)) {
            const bool bEnabled = (settings.scanModes & 0x2) != 0;
            const bool cEnabled = (settings.scanModes & 0x4) != 0;
            if (!bEnabled && !cEnabled)
                continue;

            TriggerState &state = m_triggerStates[settings.groupId];
            const qint64 encoderA = tail.encoder[0];
            const qint64 encoderB = tail.encoder[1];
            if (!state.hasOrigin) {
                state.originA = encoderA;
                state.originB = encoderB;
                state.hasOrigin = true;
            }

            int triggeredModes = 0;
            int bColumn = -1;
            int cColumn = 0;
            int cRow = 0;
            int cGateMask = 0;
            bool inspectBColumn = false;
            if (bEnabled) {
                const qint64 delta = encoderA - state.originA;
                if (state.direction == 0 && delta != 0)
                    state.direction = delta > 0 ? 1 : -1;
                const int direction = state.direction == 0 ? 1 : state.direction;
                const qint64 directedCounts = delta * direction;
                if (directedCounts >= 0) {
                    const qint64 column = directedCounts / settings.encoderPrecision;
                    bColumn = int(qMin<qint64>(column, INT_MAX));
                    // Inspect every waveform within the current spatial cell.
                    // Only a stronger complete waveform is emitted below, so a
                    // B-scan column and its click-to-review A-scan stay identical.
                    inspectBColumn = true;
                }
            }
            if (cEnabled) {
                const auto cellForDelta = [&settings](qint64 delta) {
                    return delta >= 0
                               ? delta / settings.encoderPrecision
                               : -((-delta + settings.encoderPrecision - 1)
                                   / settings.encoderPrecision);
                };
                const qint64 column = cellForDelta(encoderA - state.originA);
                const qint64 row = cellForDelta(encoderB - state.originB);
                if (column != state.lastCColumn || row != state.lastCRow) {
                    state.lastCColumn = column;
                    state.lastCRow = row;
                    state.lastCGateScores = {{-1, -1, -1, -1}};
                }
                cColumn = int(qBound<qint64>(qint64(INT_MIN), column,
                                              qint64(INT_MAX)));
                cRow = int(qBound<qint64>(qint64(INT_MIN), row,
                                          qint64(INT_MAX)));
            }
            BeamMeasurement measurement;
            if (inspectBColumn || cEnabled) {
                if (!PacketDecoder::decodeSelectedMeasurement(
                        packetView, settings.groupOffsetWords,
                        settings.beamCount, settings.pointCount,
                        settings.selectedBeam, measurement, &error)) {
                    emit decodeFailed(error);
                    continue;
                }
            }
            if (inspectBColumn) {
                const int score = int(qMax(qMax(measurement.amplitudeA,
                                                measurement.amplitudeB),
                                           qMax(measurement.amplitudeC,
                                                measurement.amplitudeI)));
                if (bColumn != state.lastBColumn) {
                    state.lastBColumn = bColumn;
                    state.lastBScore = score;
                    triggeredModes |= 0x2;
                } else if (score > state.lastBScore) {
                    state.lastBScore = score;
                    triggeredModes |= 0x2;
                }
            }
            if (cEnabled) {
                const int amplitudes[4] = {int(measurement.amplitudeA),
                                           int(measurement.amplitudeB),
                                           int(measurement.amplitudeC),
                                           int(measurement.amplitudeI)};
                for (int gate = 0; gate < 4; ++gate) {
                    if (amplitudes[gate] <= state.lastCGateScores[gate])
                        continue;
                    state.lastCGateScores[gate] = amplitudes[gate];
                    cGateMask |= 1 << gate;
                }
                if (cGateMask != 0)
                    triggeredModes |= 0x4;
            }
            if (triggeredModes == 0)
                continue;

            DecodedFrame frame;
            if (!PacketDecoder::decodeSelectedBeam(packetView,
                                                    settings.groupOffsetWords,
                                                    settings.beamCount,
                                                    settings.pointCount,
                                                    settings.selectedBeam,
                                                    frame, &error)) {
                emit decodeFailed(error);
                continue;
            }
            GroupScanFrame item;
            item.groupId = settings.groupId;
            item.triggeredModes = triggeredModes;
            item.bColumn = bColumn;
            item.cColumn = cColumn;
            item.cRow = cRow;
            item.cGateMask = cGateMask;
            item.amplitudeScale = settings.amplitudeScale;
            item.rangeStart = settings.rangeStart;
            item.rangeEnd = settings.rangeEnd;
            item.frame = std::move(frame);
            batch.append(std::move(item));
            packetUsed = true;
            batchDeviceId = packet.deviceId;

            if (batch.size() >= 256) {
                flushBatch();
            }
        }
        if (packetUsed && m_recordingEnabled)
            emit recordingPacketReady(packet);
    }
    flushBatch();
    flushPendingGroupScanBatch(false);
    m_lastDecodeDurationUs = decodeClock.nsecsElapsed() / 1000;
    if (m_scanPacketIndex >= m_scanPackets.size()) {
        m_scanPackets.clear();
        m_scanPacketIndex = 0;
    }
    emitTelemetryIfNeeded();
}

void FrameDecoderWorker::flushPendingGroupScanBatch(bool force)
{
    if (m_pendingUiScanFrames.isEmpty())
        return;
    if (!force && m_pendingUiScanFrames.size() < 256 &&
        m_groupScanEmitClock.isValid() &&
        m_groupScanEmitClock.elapsed() < kGroupScanUiEmitIntervalMs) {
        return;
    }
    QElapsedTimer emitClock;
    emitClock.start();
    emit groupScanBatchDecoded(std::move(m_pendingUiScanFrames),
                               m_pendingUiScanDeviceId);
    m_lastEmitDurationUs = emitClock.nsecsElapsed() / 1000;
    ++m_scanBatchesEmitted;
    m_pendingUiScanFrames = {};
    m_pendingUiScanDeviceId = -1;
    m_groupScanEmitClock.restart();
}

void FrameDecoderWorker::emitTelemetryIfNeeded()
{
    if (!m_telemetryClock.isValid())
        m_telemetryClock.start();
    const qint64 elapsed = m_telemetryClock.elapsed();
    if (elapsed < 1000 || !m_source)
        return;
    m_telemetryClock.restart();
    const qsizetype backlog = qMax<qsizetype>(
        0, m_scanPackets.size() - m_scanPacketIndex);
    qInfo().noquote()
        << QStringLiteral("SCAN_DECODER_TELEMETRY "
                          "batches=%1 packets=%2 backlog=%3 batch=%4 "
                          "takeUs=%5 decodeUs=%6 emitUs=%7 displayUs=%8 "
                          "queueAgeMaxMs=%9 ingressedDrop=%10 queueBytes=%11 "
                          "queueDepth=%12 ingestLockMaxUs=%13 epoch=%14")
               .arg(m_scanBatchesEmitted)
               .arg(m_scanPacketsProcessed)
               .arg(backlog)
               .arg(m_lastScanBatchSize)
               .arg(m_lastTakeDurationUs)
               .arg(m_lastDecodeDurationUs)
               .arg(m_lastEmitDurationUs)
               .arg(m_lastDisplayDecodeDurationUs)
               .arg(m_source->cScanMaxAgeMs())
               .arg(m_source->cScanDroppedPackets())
               .arg(m_source->cScanQueueBytes())
               .arg(m_source->cScanQueueDepth())
               .arg(m_source->callbackLockWaitMaxUs())
               .arg(m_source->captureEpoch());
}
