#pragma once

#include "packetdecoder.h"
#include "rawpacket.h"
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QVector>
#include <array>

class DeviceController;
class QTimer;

struct GroupScanSettings
{
    int groupId = 0;
    int groupOffsetWords = 0;
    int beamCount = 1;
    int pointCount = 1024;
    int scanModes = 0;
    int selectedBeam = 0;
    int amplitudeScale = 4096;
    int encoderPrecision = 10;
    double rangeStart = 0.0;
    double rangeEnd = 50.0;
};
Q_DECLARE_METATYPE(GroupScanSettings)
Q_DECLARE_METATYPE(QVector<GroupScanSettings>)

struct GroupScanFrame
{
    int groupId = 0;
    int triggeredModes = 0;
    int bColumn = -1;
    int cColumn = 0;
    int cRow = 0;
    int cGateMask = 0xF;
    int amplitudeScale = 4096;
    double rangeStart = 0.0;
    double rangeEnd = 50.0;
    DecodedFrame frame;
};
Q_DECLARE_METATYPE(GroupScanFrame)
using GroupScanFrameBatch = QVector<GroupScanFrame>;
Q_DECLARE_METATYPE(GroupScanFrameBatch)

struct GroupScanFrameKey
{
    int groupId = 0;
    int mode = 0;
    int gate = 0;
    int column = -1;
    int x = 0;
    int y = 0;

    bool operator==(const GroupScanFrameKey &other) const
    {
        return groupId == other.groupId && mode == other.mode
               && gate == other.gate && column == other.column
               && x == other.x && y == other.y;
    }
};

inline size_t qHash(const GroupScanFrameKey &key, size_t seed = 0) noexcept
{
    seed = qHash(key.groupId, seed);
    seed = qHash(key.mode, seed);
    seed = qHash(key.gate, seed);
    seed = qHash(key.column, seed);
    seed = qHash(key.x, seed);
    return qHash(key.y, seed);
}

class FrameDecoderWorker : public QObject
{
    Q_OBJECT
public:
    explicit FrameDecoderWorker(DeviceController *source);

public slots:
    void startPolling();
    void updateSettings(QVector<GroupScanSettings> groups, int displayGroupId);
    void setRecordingEnabled(bool enabled) { m_recordingEnabled = enabled; }

signals:
    void frameDecoded(DecodedFrame frame, int groupId, int deviceId);
    void groupScanBatchDecoded(GroupScanFrameBatch frames, int deviceId);
    void decodeFailed(QString error);
    void recordingPacketReady(RawPacketBuffer packet);

private:
    struct TriggerState {
        qint64 originA = 0;
        qint64 originB = 0;
        qint64 lastBColumn = -1;
        int lastBScore = -1;
        qint64 lastCColumn = -1;
        qint64 lastCRow = -1;
        std::array<int, 4> lastCGateScores{{-1, -1, -1, -1}};
        int direction = 0;
        bool hasOrigin = false;
    };

    void processLatestPacket();
    void processDisplayPacket();
    void processEncoderTriggeredPackets();
    void flushPendingGroupScanBatch(bool force);
    void emitTelemetryIfNeeded();
    const GroupScanSettings *displaySettings() const;
    static bool triggerGeometryChanged(const GroupScanSettings &left,
                                       const GroupScanSettings &right);

    DeviceController *m_source = nullptr;
    QTimer *m_pollTimer = nullptr;
    QVector<GroupScanSettings> m_groups;
    QHash<int, TriggerState> m_triggerStates;
    int m_displayGroupId = 0;
    bool m_recordingEnabled = false;
    bool m_hasEncoderScans = false;
    QElapsedTimer m_displayClock;
    GroupScanFrameBatch m_pendingUiScanFrames;
    int m_pendingUiScanDeviceId = -1;
    QElapsedTimer m_groupScanEmitClock;
    QQueue<RawPacketBuffer> m_scanPackets;
    int m_scanPacketIndex = 0;
    qint64 m_lastScanBatchSize = 0;
    qint64 m_lastTakeDurationUs = 0;
    qint64 m_lastDecodeDurationUs = 0;
    qint64 m_lastEmitDurationUs = 0;
    qint64 m_lastDisplayDecodeDurationUs = 0;
    qint64 m_scanPacketsProcessed = 0;
    qint64 m_scanBatchesEmitted = 0;
    QElapsedTimer m_telemetryClock;
};
