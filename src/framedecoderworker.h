#pragma once

#include "packetdecoder.h"
#include "rawpacket.h"
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVector>

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
    int amplitudeScale = 4096;
    double rangeStart = 0.0;
    double rangeEnd = 50.0;
    DecodedFrame frame;
};
Q_DECLARE_METATYPE(GroupScanFrame)
using GroupScanFrameBatch = QVector<GroupScanFrame>;
Q_DECLARE_METATYPE(GroupScanFrameBatch)

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
    void recordingPacketReady(QByteArray packet, int deviceId);

private:
    struct TriggerState {
        qint64 originA = 0;
        qint64 originB = 0;
        qint64 lastBColumn = -1;
        int lastBScore = -1;
        qint64 lastCColumn = -1;
        qint64 lastCRow = -1;
        int direction = 0;
        bool hasOrigin = false;
    };

    void processLatestPacket();
    void processDisplayPacket();
    void processEncoderTriggeredPackets();
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
};
