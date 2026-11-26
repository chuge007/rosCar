#include "packetdecoder.h"

#include "scanpalette.h"

#include <QColor>
#include <array>
#include <cstring>

namespace {
#pragma pack(push, 1)
struct RawPacketTail
{
    quint16 flags1;
    quint16 flags2;
    qint32 encoderA;
    qint32 encoderB;
    qint32 encoderC;
    qint32 encoderD;
    qint32 encoderE;
    quint64 timestamp;
};

struct RawMeasurement
{
    quint64 word0;
    quint64 word1;
    quint64 word2;
    quint64 word3;
};
#pragma pack(pop)

static quint64 maskBits(int bits)
{
    return bits == 64 ? ~quint64(0) : ((quint64(1) << bits) - 1);
}

static quint64 bits(quint64 value, int start, int count)
{
    return (value >> start) & maskBits(count);
}

static BeamMeasurement decodeMeasurement(const qint16 *source)
{
    RawMeasurement raw{};
    std::memcpy(&raw, source, sizeof(raw));
    BeamMeasurement m{};
    m.amplitudeB = bits(raw.word0, 0, 16);
    m.positionB = bits(raw.word0, 16, 24);
    m.amplitudeA = bits(raw.word0, 40, 16);
    m.positionA = bits(raw.word0, 56, 8) | (bits(raw.word1, 0, 16) << 8);
    m.amplitudeI = bits(raw.word1, 16, 16);
    m.positionI = bits(raw.word1, 32, 16);
    m.amplitudeC = bits(raw.word1, 56, 8) << 8;
    m.positionC = bits(raw.word2, 0, 24);
    return m;
}

static void decodeTail(const QByteArray &packet, PacketTail &tail)
{
    RawPacketTail rawTail{};
    std::memcpy(&rawTail, packet.constData() + packet.size() - sizeof(rawTail), sizeof(rawTail));
    tail = {};
    tail.frameError = (rawTail.flags1 & 0x1) != 0;
    tail.scannerIo = (rawTail.flags1 >> 1) & 0x7;
    tail.multiFrame = (rawTail.flags1 >> 4) & 0x0fff;
    tail.frameNumber = rawTail.flags2 & 0x0fff;
    tail.encoderTriggerError = ((rawTail.flags2 >> 12) & 0x3) != 0;
    tail.encoderSyncError = ((rawTail.flags2 >> 14) & 0x3) != 0;
    tail.encoder[0] = rawTail.encoderA;
    tail.encoder[1] = rawTail.encoderB;
    tail.encoder[2] = rawTail.encoderC;
    tail.encoder[3] = rawTail.encoderD;
    tail.encoder[4] = rawTail.encoderE;
    tail.timestamp = rawTail.timestamp;
}

static bool validatePacket(const QByteArray &packet, int groupOffsetWords,
                           int beamCount, int pointCount, QString *error)
{
    if (beamCount <= 0 || pointCount <= 0 || groupOffsetWords < 0) {
        if (error) *error = QStringLiteral("无效的采集几何参数");
        return false;
    }
    const qsizetype strideWords = pointCount + 16;
    const qsizetype requiredWords = groupOffsetWords + qsizetype(beamCount) * strideWords;
    const qsizetype requiredBytes = requiredWords * qsizetype(sizeof(qint16)) + sizeof(RawPacketTail);
    if (packet.size() < requiredBytes) {
        if (error) *error = QStringLiteral("数据包过短：%1 字节，需要至少 %2 字节")
                                .arg(packet.size()).arg(requiredBytes);
        return false;
    }
    return true;
}

// E 扫图像索引是双极的（127.5 ≈ 零信号），默认色表同样按双极灰阶：
// 零信号=浅灰底、负半周压黑、正半周提白，未接 UI 色表时也不会把极性画丢。
static const std::array<QRgb, 256> &bipolarPalette()
{
    static const std::array<QRgb, 256> palette = [] {
        std::array<QRgb, 256> result{};
        for (int i = 0; i < 256; ++i)
            result[size_t(i)] = ScanPalette::tofdGrayRgb(i);
        return result;
    }();
    return palette;
}
}

bool PacketDecoder::decodePacketTail(const QByteArray &packet, PacketTail &tail,
                                     QString *error)
{
    if (packet.size() < qsizetype(sizeof(RawPacketTail))) {
        if (error)
            *error = QStringLiteral("数据包不足以读取编码器尾部：%1 字节")
                         .arg(packet.size());
        return false;
    }
    decodeTail(packet, tail);
    return true;
}

bool PacketDecoder::decode(const QByteArray &packet, int groupOffsetWords, int beamCount,
                           int pointCount, DecodedFrame &out, QString *error)
{
    if (!validatePacket(packet, groupOffsetWords, beamCount, pointCount, error))
        return false;

    const qsizetype strideWords = pointCount + 16;
    const auto *words = reinterpret_cast<const qint16 *>(packet.constData());
    out = {};
    out.sourceBeamCount = beamCount;
    out.beams.resize(beamCount);
    out.measurements.resize(beamCount);
    for (int beam = 0; beam < beamCount; ++beam) {
        const qsizetype base = groupOffsetWords + qsizetype(beam) * strideWords;
        out.beams[beam].resize(pointCount);
        std::memcpy(out.beams[beam].data(), words + base, qsizetype(pointCount) * sizeof(qint16));

        out.measurements[beam] = decodeMeasurement(words + base + pointCount);
    }
    decodeTail(packet, out.tail);
    return true;
}

bool PacketDecoder::decodeSelectedBeam(const QByteArray &packet, int groupOffsetWords,
                                       int beamCount, int pointCount, int selectedBeam,
                                       DecodedFrame &out, QString *error)
{
    if (!validatePacket(packet, groupOffsetWords, beamCount, pointCount, error))
        return false;
    selectedBeam = qBound(0, selectedBeam, beamCount - 1);
    const qsizetype strideWords = pointCount + 16;
    const qsizetype base = groupOffsetWords + qsizetype(selectedBeam) * strideWords;
    const auto *words = reinterpret_cast<const qint16 *>(packet.constData());

    out = {};
    out.sourceBeamCount = beamCount;
    out.selectedSourceBeam = selectedBeam;
    out.beams.resize(1);
    out.beams[0].resize(pointCount);
    std::memcpy(out.beams[0].data(), words + base,
                qsizetype(pointCount) * sizeof(qint16));
    out.measurements.append(decodeMeasurement(words + base + pointCount));
    decodeTail(packet, out.tail);
    return true;
}

bool PacketDecoder::decodeSelectedMeasurement(const QByteArray &packet,
                                              int groupOffsetWords,
                                              int beamCount, int pointCount,
                                              int selectedBeam,
                                              BeamMeasurement &out,
                                              QString *error)
{
    if (!validatePacket(packet, groupOffsetWords, beamCount, pointCount, error))
        return false;
    selectedBeam = qBound(0, selectedBeam, beamCount - 1);
    const qsizetype strideWords = pointCount + 16;
    const qsizetype base = groupOffsetWords + qsizetype(selectedBeam) * strideWords;
    const auto *words = reinterpret_cast<const qint16 *>(packet.constData());
    out = decodeMeasurement(words + base + pointCount);
    return true;
}

bool PacketDecoder::decodeMeasurements(const QByteArray &packet, int groupOffsetWords,
                                       int beamCount, int pointCount, DecodedFrame &out,
                                       QString *error)
{
    if (!validatePacket(packet, groupOffsetWords, beamCount, pointCount, error))
        return false;
    const qsizetype strideWords = pointCount + 16;
    const auto *words = reinterpret_cast<const qint16 *>(packet.constData());
    out = {};
    out.sourceBeamCount = beamCount;
    out.measurements.resize(beamCount);
    for (int beam = 0; beam < beamCount; ++beam) {
        const qsizetype base = groupOffsetWords + qsizetype(beam) * strideWords;
        out.measurements[beam] = decodeMeasurement(words + base + pointCount);
    }
    decodeTail(packet, out.tail);
    return true;
}

bool PacketDecoder::decodeEScan(const QByteArray &packet, int groupOffsetWords,
                                int beamCount, int pointCount, int selectedBeam,
                                double scale, DecodedFrame &out, QString *error)
{
    if (!validatePacket(packet, groupOffsetWords, beamCount, pointCount, error))
        return false;
    selectedBeam = qBound(0, selectedBeam, beamCount - 1);
    scale = qMax(1.0, scale);
    const qsizetype strideWords = pointCount + 16;
    const auto *words = reinterpret_cast<const qint16 *>(packet.constData());
    const auto &palette = bipolarPalette();

    out = {};
    out.sourceBeamCount = beamCount;
    out.selectedSourceBeam = selectedBeam;
    out.eScanImage = QImage(beamCount, pointCount, QImage::Format_Indexed8);
    if (out.eScanImage.isNull()) {
        if (error) *error = QStringLiteral("无法分配 E 扫图像缓冲区");
        return false;
    }
    QVector<QRgb> colorTable(256);
    for (int i = 0; i < colorTable.size(); ++i)
        colorTable[i] = palette[size_t(i)];
    out.eScanImage.setColorTable(colorTable);

    // The packet is beam-major while the image is row-major.  Generate the
    // raster directly from the SDK buffer with an integer palette lookup;
    // avoid constructing beamCount temporary QVector waveforms and avoid a
    // QColor/HSV conversion for every pixel on the GUI thread.
    //
    // 索引保留采样正负极性：127.5 ≈ 零信号，0 = 最负、255 = 最正。
    // 这样 E 扫才能像 B 扫的 TOFD/RF 那样「负半周压黑 / 正半周提白」，
    // 幅值类色表在扫查端只取 |索引 - 127.5|，语义不受影响。
    const double gain = 127.5 / scale;
    for (int point = 0; point < pointCount; ++point) {
        uchar *line = out.eScanImage.scanLine(point);
        for (int beam = 0; beam < beamCount; ++beam) {
            const qsizetype sampleIndex = groupOffsetWords
                + qsizetype(beam) * strideWords + point;
            const int colorIndex = qBound(0, qRound(127.5
                + double(words[sampleIndex]) * gain), 255);
            line[beam] = uchar(colorIndex);
        }
    }

    const qsizetype selectedBase = groupOffsetWords
        + qsizetype(selectedBeam) * strideWords;
    // E scan may run at the same time as A/B/C.  Keep the selected waveform
    // in the same decoded frame so enabling E does not starve the A-scan view.
    out.beams.resize(1);
    out.beams[0].resize(pointCount);
    std::memcpy(out.beams[0].data(), words + selectedBase,
                qsizetype(pointCount) * sizeof(qint16));
    out.measurements.append(
        decodeMeasurement(words + selectedBase + pointCount));
    decodeTail(packet, out.tail);
    return true;
}

double PacketDecoder::amplitudeScale(int maxAmplitudeEnumValue)
{
    return maxAmplitudeEnumValue > 0 ? double(maxAmplitudeEnumValue) : 4096.0;
}
