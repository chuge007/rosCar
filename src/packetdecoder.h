#pragma once

#include <QByteArray>
#include <QImage>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <cstdint>

struct PacketTail
{
    bool frameError = false;
    int scannerIo = 0;
    int multiFrame = 0;
    int frameNumber = 0;
    bool encoderTriggerError = false;
    bool encoderSyncError = false;
    qint32 encoder[5] = {};
    quint64 timestamp = 0;
};

struct BeamMeasurement
{
    quint32 amplitudeA = 0;
    quint32 amplitudeB = 0;
    quint32 amplitudeC = 0;
    quint32 amplitudeI = 0;
    quint32 positionA = 0;
    quint32 positionB = 0;
    quint32 positionC = 0;
    quint32 positionI = 0;
};

struct DecodedFrame
{
    QVector<QVector<qint16>> beams;
    QVector<BeamMeasurement> measurements;
    QImage eScanImage;
    PacketTail tail;
    // Live A-scan frames may contain only the selected source beam.  Keep the
    // source geometry so the UI can still expose the complete beam range.
    int sourceBeamCount = 0;
    int selectedSourceBeam = -1;
};

Q_DECLARE_METATYPE(DecodedFrame)
using DecodedFrameBatch = QVector<DecodedFrame>;
Q_DECLARE_METATYPE(DecodedFrameBatch)

class PacketDecoder
{
public:
    static bool decodePacketTail(const QByteArray &packet, PacketTail &tail,
                                 QString *error = nullptr);
    static bool decode(const QByteArray &packet, int groupOffsetWords, int beamCount,
                       int pointCount, DecodedFrame &out, QString *error = nullptr);
    static bool decodeSelectedBeam(const QByteArray &packet, int groupOffsetWords,
                                   int beamCount, int pointCount, int selectedBeam,
                                   DecodedFrame &out, QString *error = nullptr);
    static bool decodeMeasurements(const QByteArray &packet, int groupOffsetWords,
                                   int beamCount, int pointCount, DecodedFrame &out,
                                   QString *error = nullptr);
    static bool decodeEScan(const QByteArray &packet, int groupOffsetWords,
                            int beamCount, int pointCount, int selectedBeam,
                            double amplitudeScale, DecodedFrame &out,
                            QString *error = nullptr);
    static double amplitudeScale(int maxAmplitudeEnumValue);
};
