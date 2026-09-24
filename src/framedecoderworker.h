#pragma once

#include "packetdecoder.h"
#include "rawpacket.h"
#include <QElapsedTimer>
#include <QObject>

class DeviceController;
class QTimer;

class FrameDecoderWorker : public QObject
{
    Q_OBJECT
public:
    explicit FrameDecoderWorker(DeviceController *source);

public slots:
    void startPolling();
    void updateSettings(int groupOffsetWords, int beamCount, int pointCount,
                        int scanModes, int selectedBeam, int amplitudeScale,
                        int encoderPrecision);
    void setRecordingEnabled(bool enabled) { m_recordingEnabled = enabled; }

signals:
    void frameDecoded(DecodedFrame frame, int deviceId);
    void cScanBatchDecoded(DecodedFrameBatch frames, int deviceId);
    void decodeFailed(QString error);
    void recordingPacketReady(QByteArray packet, int deviceId);

private:
    void processLatestPacket();
    void processDisplayPacket();
    void processEncoderTriggeredPackets();
    void resetEncoderTrigger();
    DeviceController *m_source = nullptr;
    QTimer *m_pollTimer = nullptr;
    int m_groupOffsetWords = 0;
    int m_beamCount = 1;
    int m_pointCount = 1024;
    int m_scanModes = 0;
    int m_selectedBeam = 0;
    int m_amplitudeScale = 4096;
    int m_encoderPrecision = 10;
    qint64 m_encoderOrigin = 0;
    qint64 m_encoderOriginB = 0;
    qint64 m_lastBScanColumn = -1;
    qint64 m_lastCScanColumn = -1;
    qint64 m_lastCScanRow = -1;
    int m_encoderDirection = 0;
    bool m_hasEncoderOrigin = false;
    bool m_recordingEnabled = false;
    QElapsedTimer m_displayClock;
};
