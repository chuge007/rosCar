#pragma once

#include "framedecoderworker.h"
#include "laser_gap_detector.h"
#include "packetdecoder.h"
#include "rim302_protocol.h"

#include <QByteArray>
#include <QImage>
#include <QVector>
#include <QVector3D>

namespace pa1664::remote {

// Shared compact codecs for data that crosses the media or bulk channels.
// Frames are compressed once at the host and decoded once on the operator PC.
QByteArray encodeDecodedFrame(const DecodedFrame& frame);
bool decodeDecodedFrame(const QByteArray& payload, DecodedFrame* frame,
                        QString* error = nullptr);

QByteArray encodeGroupScanBatch(const GroupScanFrameBatch& batch);
bool decodeGroupScanBatch(const QByteArray& payload, GroupScanFrameBatch* batch,
                          QString* error = nullptr);

QByteArray encodeImuSample(const crawling::ImuSample& sample);
bool decodeImuSample(const QByteArray& payload, crawling::ImuSample* sample,
                     QString* error = nullptr);

QByteArray encodeImagePayload(const QImage& image, int jpegQuality = 82);
bool decodeImagePayload(const QByteArray& payload, QImage* image,
                        QString* error = nullptr);

QByteArray encodePointCloud(const QVector<QVector3D>& points);
bool decodePointCloud(const QByteArray& payload, QVector<QVector3D>* points,
                      QString* error = nullptr);

// Quantized point cloud (payload type 5). Same point count and ordering as
// encodePointCloud(), but each coordinate is stored as qint16 relative to a
// per-frame origin with a per-frame scale, halving the payload while keeping
// the reconstruction error below max-range/32767 (sub-micrometre for a laser
// profile). Frames containing non-finite coordinates transparently fall back
// to the type-4 float encoding so invalid slots keep their exact semantics.
QByteArray encodeQuantizedPointCloud(const QVector<QVector3D>& points);

// Full weld observation: points, gap detection, SDK frame number and the
// original receive timestamp. The integrated build delivered all of this to
// RobotProfileView via profileObservationReady; the separated build must carry
// it over the media channel or the view reports "本帧未确认焊道" and SDK #0.
QByteArray encodeProfileObservation(
    const QVector<QVector3D>& points,
    const crawling::LaserGapDetection& detection, quint32 frameNumber,
    qint64 receivedAtEpochMs);
bool decodeProfileObservation(const QByteArray& payload,
                              QVector<QVector3D>* points,
                              crawling::LaserGapDetection* detection,
                              quint32* frameNumber,
                              qint64* receivedAtEpochMs,
                              QString* error = nullptr);

}  // namespace pa1664::remote
