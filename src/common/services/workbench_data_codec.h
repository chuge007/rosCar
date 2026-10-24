#pragma once

#include "framedecoderworker.h"
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

}  // namespace pa1664::remote
