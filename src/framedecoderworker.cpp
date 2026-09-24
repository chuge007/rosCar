#include "framedecoderworker.h"
#include "devicecontroller.h"
#include <QTimer>

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
    // Match the proven PCIe demo: acquisition may be much faster, but a
    // monitor needs a stable ~30 FPS newest-frame presentation cadence.
    m_pollTimer->setInterval(33);
    connect(m_pollTimer, &QTimer::timeout,
            this, &FrameDecoderWorker::processLatestPacket);
    m_pollTimer->start();
}

void FrameDecoderWorker::updateSettings(int groupOffsetWords, int beamCount,
                                        int pointCount, int scanModes,
                                        int selectedBeam, int amplitudeScale,
                                        int encoderPrecision)
{
    const int boundedScanModes = scanModes & 0x7;
    const int boundedBeam = qBound(0, selectedBeam, qMax(1, beamCount) - 1);
    const int boundedPrecision = qMax(1, encoderPrecision);
    const bool resetTrigger = (m_scanModes & 0x6) != (boundedScanModes & 0x6)
                              || m_selectedBeam != boundedBeam
                              || m_encoderPrecision != boundedPrecision;
    m_groupOffsetWords = qMax(0, groupOffsetWords);
    m_beamCount = qMax(1, beamCount);
    m_pointCount = qMax(1, pointCount);
    m_scanModes = boundedScanModes;
    m_selectedBeam = boundedBeam;
    m_amplitudeScale = qMax(1, amplitudeScale);
    m_encoderPrecision = boundedPrecision;
    if (resetTrigger)
        resetEncoderTrigger();
    if (m_source) {
        // B/C share the same ordered raw-packet FIFO and can run together.
        // Flush it when their combination/beam/precision changes so packets
        // decoded under the old geometry cannot enter the new scan.
        if (resetTrigger)
            m_source->setCScanPacketCollection(false);
        m_source->setCScanPacketCollection((m_scanModes & 0x6) != 0);
    }
    if (m_pollTimer)
        m_pollTimer->setInterval((m_scanModes & 0x6) != 0 ? 5 : 33);
}

void FrameDecoderWorker::processLatestPacket()
{
    if ((m_scanModes & 0x6) != 0)
        processEncoderTriggeredPackets();

    // Ordered encoder processing may run every 5 ms, while A/E presentation
    // remains capped near 30 FPS.  Thus enabling B or C no longer freezes A/E.
    if (!m_displayClock.isValid() || m_displayClock.elapsed() >= 33) {
        m_displayClock.restart();
        processDisplayPacket();
    }
}

void FrameDecoderWorker::processDisplayPacket()
{
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
    if ((m_scanModes & 0x1) != 0) {
        decoded = PacketDecoder::decodeEScan(packetView, m_groupOffsetWords,
                                             m_beamCount, m_pointCount,
                                             m_selectedBeam,
                                             double(m_amplitudeScale),
                                             frame, &error);
    } else {
        decoded = PacketDecoder::decodeSelectedBeam(packetView, m_groupOffsetWords,
                                                     m_beamCount, m_pointCount,
                                                     m_selectedBeam, frame, &error);
    }
    if (!decoded) {
        emit decodeFailed(error);
        return;
    }
    if (m_recordingEnabled && (m_scanModes & 0x6) == 0)
        emit recordingPacketReady(QByteArray(packet.bytes.data(), packet.length),
                                  packet.deviceId);
    emit frameDecoded(std::move(frame), packet.deviceId);
}

void FrameDecoderWorker::resetEncoderTrigger()
{
    m_encoderOrigin = 0;
    m_encoderOriginB = 0;
    m_lastBScanColumn = -1;
    m_lastCScanColumn = -1;
    m_lastCScanRow = -1;
    m_encoderDirection = 0;
    m_hasEncoderOrigin = false;
}

void FrameDecoderWorker::processEncoderTriggeredPackets()
{
    if (!m_source)
        return;
    const bool bScanEnabled = (m_scanModes & 0x2) != 0;
    const bool cScanEnabled = (m_scanModes & 0x4) != 0;
    if (!bScanEnabled && !cScanEnabled)
        return;
    QVector<RawPacketBuffer> packets;
    if (m_source->takeCScanPackets(packets, 2048) <= 0)
        return;

    DecodedFrameBatch batch;
    batch.reserve(qMin(256, packets.size()));
    int batchDeviceId = -1;
    for (const RawPacketBuffer &packet : packets) {
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
        const qint64 encoder = tail.encoder[0];
        if (!m_hasEncoderOrigin) {
            m_encoderOrigin = encoder;
            m_encoderOriginB = tail.encoder[1];
            m_hasEncoderOrigin = true;
        }
        bool triggered = false;
        if (bScanEnabled) {
            const qint64 delta = encoder - m_encoderOrigin;
            if (m_encoderDirection == 0 && delta != 0)
                m_encoderDirection = delta > 0 ? 1 : -1;
            const int direction = m_encoderDirection == 0 ? 1 : m_encoderDirection;
            const qint64 directedCounts = delta * direction;
            if (directedCounts >= 0) {
                const qint64 column = directedCounts / m_encoderPrecision;
                if (column != m_lastBScanColumn) {
                    m_lastBScanColumn = column;
                    triggered = true;
                }
            }
        }
        if (cScanEnabled) {
            auto cellForDelta = [this](qint64 delta) {
                return delta >= 0
                           ? delta / m_encoderPrecision
                           : -((-delta + m_encoderPrecision - 1)
                               / m_encoderPrecision);
            };
            const qint64 column = cellForDelta(encoder - m_encoderOrigin);
            const qint64 row = cellForDelta(qint64(tail.encoder[1]) - m_encoderOriginB);
            if (column != m_lastCScanColumn || row != m_lastCScanRow) {
                m_lastCScanColumn = column;
                m_lastCScanRow = row;
                triggered = true;
            }
        }
        if (!triggered)
            continue;

        DecodedFrame frame;
        if (!PacketDecoder::decodeSelectedBeam(packetView, m_groupOffsetWords,
                                                m_beamCount, m_pointCount,
                                                m_selectedBeam, frame, &error)) {
            emit decodeFailed(error);
            continue;
        }
        batchDeviceId = packet.deviceId;
        if (m_recordingEnabled)
            emit recordingPacketReady(QByteArray(packet.bytes.data(), packet.length),
                                      packet.deviceId);
        batch.append(std::move(frame));
        if (batch.size() >= 256) {
            emit cScanBatchDecoded(std::move(batch), batchDeviceId);
            batch = {};
            batch.reserve(256);
        }
    }
    if (!batch.isEmpty())
        emit cScanBatchDecoded(std::move(batch), batchDeviceId);
}
