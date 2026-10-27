#include "workbench_data_codec.h"

#include "protocol/protocol_types.h"
#include <QBuffer>
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>

namespace pa1664::remote {
namespace {

constexpr quint32 kDataMagic = 0x50414431u;  // PAD1
constexpr quint16 kDataVersion = 1;
constexpr int kMaximumBeams = 8192;
constexpr int kMaximumPointsPerBeam = 65536;
constexpr int kMaximumMeasurements = 65536;
constexpr int kMaximumBatchFrames = 100000;
constexpr int kMaximumImageBytes = 64 * 1024 * 1024;
constexpr int kMaximumPointCloudPoints = 8 * 1024 * 1024;

void setError(QString* error, const QString& detail) {
  if (error) *error = detail;
}

// QDataStream is not copyable in Qt 6, so streams are constructed at the call
// site and configured in place.
void configureStream(QDataStream& stream) {
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
}

void writeImage(QDataStream& stream, const QImage& image, int jpegQuality) {
  if (image.isNull()) {
    stream << static_cast<quint32>(0);
    return;
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  const QByteArray format = jpegQuality >= 0 ? QByteArray("JPG") : QByteArray("PNG");
  if (!image.save(&buffer, format.constData(), jpegQuality)) bytes.clear();
  stream << static_cast<quint32>(bytes.size());
  if (!bytes.isEmpty()) stream.writeRawData(bytes.constData(), bytes.size());
}

bool readImage(QDataStream& stream, QImage* image, QString* error) {
  quint32 size = 0;
  stream >> size;
  if (stream.status() != QDataStream::Ok || size > kMaximumImageBytes) {
    setError(error, QStringLiteral("图像载荷长度无效"));
    return false;
  }
  QByteArray bytes(static_cast<int>(size), Qt::Uninitialized);
  if (size > 0 && stream.readRawData(bytes.data(), size) != static_cast<int>(size)) {
    setError(error, QStringLiteral("图像载荷不完整"));
    return false;
  }
  if (bytes.isEmpty()) {
    *image = QImage();
    return true;
  }
  if (!image->loadFromData(bytes)) {
    setError(error, QStringLiteral("图像解码失败"));
    return false;
  }
  return true;
}

void writeFrame(QDataStream& stream, const DecodedFrame& frame) {
  stream << static_cast<quint32>(frame.beams.size());
  for (const QVector<qint16>& beam : frame.beams) {
    stream << static_cast<quint32>(beam.size());
    for (qint16 value : beam) stream << value;
  }
  stream << static_cast<quint32>(frame.measurements.size());
  for (const BeamMeasurement& measurement : frame.measurements) {
    stream << measurement.amplitudeA << measurement.amplitudeB
           << measurement.amplitudeC << measurement.amplitudeI
           << measurement.positionA << measurement.positionB
           << measurement.positionC << measurement.positionI;
  }
  QByteArray imageBytes;
  QBuffer imageBuffer(&imageBytes);
  imageBuffer.open(QIODevice::WriteOnly);
  if (!frame.eScanImage.isNull())
    frame.eScanImage.save(&imageBuffer, "PNG");
  stream << static_cast<quint32>(imageBytes.size());
  if (!imageBytes.isEmpty())
    stream.writeRawData(imageBytes.constData(), imageBytes.size());
  stream << frame.tail.frameError << frame.tail.scannerIo
         << frame.tail.multiFrame << frame.tail.frameNumber
         << frame.tail.encoderTriggerError << frame.tail.encoderSyncError;
  for (int value : frame.tail.encoder) stream << value;
  stream << frame.tail.timestamp;
  stream << frame.sourceBeamCount << frame.selectedSourceBeam;
}

bool readFrame(QDataStream& stream, DecodedFrame* frame, QString* error) {
  quint32 beamCount = 0;
  stream >> beamCount;
  if (stream.status() != QDataStream::Ok || beamCount > kMaximumBeams) {
    setError(error, QStringLiteral("声束数量无效"));
    return false;
  }
  frame->beams.clear();
  frame->beams.reserve(static_cast<int>(beamCount));
  for (quint32 beam = 0; beam < beamCount; ++beam) {
    quint32 points = 0;
    stream >> points;
    if (stream.status() != QDataStream::Ok ||
        points > kMaximumPointsPerBeam) {
      setError(error, QStringLiteral("声束点数无效"));
      return false;
    }
    QVector<qint16> samples;
    samples.resize(static_cast<int>(points));
    for (quint32 index = 0; index < points; ++index)
      stream >> samples[static_cast<int>(index)];
    frame->beams.push_back(std::move(samples));
  }
  quint32 measurementCount = 0;
  stream >> measurementCount;
  if (stream.status() != QDataStream::Ok ||
      measurementCount > kMaximumMeasurements) {
    setError(error, QStringLiteral("测量结果数量无效"));
    return false;
  }
  frame->measurements.resize(static_cast<int>(measurementCount));
  for (BeamMeasurement& measurement : frame->measurements) {
    stream >> measurement.amplitudeA >> measurement.amplitudeB
        >> measurement.amplitudeC >> measurement.amplitudeI
        >> measurement.positionA >> measurement.positionB
        >> measurement.positionC >> measurement.positionI;
  }
  quint32 imageBytes = 0;
  stream >> imageBytes;
  if (stream.status() != QDataStream::Ok || imageBytes > kMaximumImageBytes) {
    setError(error, QStringLiteral("E 扫图像长度无效"));
    return false;
  }
  QByteArray imageData(static_cast<int>(imageBytes), Qt::Uninitialized);
  if (imageBytes > 0 &&
      stream.readRawData(imageData.data(), imageBytes) !=
          static_cast<int>(imageBytes)) {
    setError(error, QStringLiteral("E 扫图像数据不完整"));
    return false;
  }
  frame->eScanImage = QImage();
  if (!imageData.isEmpty() &&
      !frame->eScanImage.loadFromData(imageData, "PNG")) {
    setError(error, QStringLiteral("E 扫图像解码失败"));
    return false;
  }
  stream >> frame->tail.frameError >> frame->tail.scannerIo
      >> frame->tail.multiFrame >> frame->tail.frameNumber
      >> frame->tail.encoderTriggerError >> frame->tail.encoderSyncError;
  for (int& value : frame->tail.encoder) stream >> value;
  stream >> frame->tail.timestamp;
  stream >> frame->sourceBeamCount >> frame->selectedSourceBeam;
  if (stream.status() != QDataStream::Ok) {
    setError(error, QStringLiteral("超声帧数据不完整"));
    return false;
  }
  return true;
}

}  // namespace

QByteArray encodeDecodedFrame(const DecodedFrame& frame) {
  QByteArray body;
  QDataStream stream(&body, QIODevice::WriteOnly);
  configureStream(stream);
  stream << kDataMagic << kDataVersion << static_cast<quint8>(1);
  writeFrame(stream, frame);
  return stream.status() == QDataStream::Ok ? qCompress(body, 4) : QByteArray();
}

bool decodeDecodedFrame(const QByteArray& payload, DecodedFrame* frame,
                        QString* error) {
  if (error) error->clear();
  if (!frame) return false;
  const QByteArray body = qUncompress(payload);
  if (body.isEmpty() || body.size() > kMaxMediaMessageBytes) {
    setError(error, QStringLiteral("超声帧压缩载荷无效"));
    return false;
  }
  QDataStream stream(body);
  configureStream(stream);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 type = 0;
  stream >> magic >> version >> type;
  if (magic != kDataMagic || version != kDataVersion || type != 1 ||
      stream.status() != QDataStream::Ok) {
    setError(error, QStringLiteral("超声帧头无效"));
    return false;
  }
  return readFrame(stream, frame, error);
}

QByteArray encodeGroupScanBatch(const GroupScanFrameBatch& batch) {
  QByteArray body;
  QDataStream stream(&body, QIODevice::WriteOnly);
  configureStream(stream);
  stream << kDataMagic << kDataVersion << static_cast<quint8>(2);
  stream << static_cast<quint32>(batch.size());
  for (const GroupScanFrame& item : batch) {
    stream << item.groupId << item.triggeredModes << item.bColumn
           << item.cColumn << item.cRow << item.cGateMask
           << item.amplitudeScale << item.rangeStart << item.rangeEnd;
    writeFrame(stream, item.frame);
  }
  return stream.status() == QDataStream::Ok ? qCompress(body, 5) : QByteArray();
}

bool decodeGroupScanBatch(const QByteArray& payload,
                          GroupScanFrameBatch* batch, QString* error) {
  if (error) error->clear();
  if (!batch) return false;
  const QByteArray body = qUncompress(payload);
  if (body.isEmpty() || body.size() > kMaxBulkPayloadBytes) {
    setError(error, QStringLiteral("扫描批量压缩载荷无效"));
    return false;
  }
  QDataStream stream(body);
  configureStream(stream);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 type = 0;
  quint32 count = 0;
  stream >> magic >> version >> type >> count;
  if (magic != kDataMagic || version != kDataVersion || type != 2 ||
      count > kMaximumBatchFrames || stream.status() != QDataStream::Ok) {
    setError(error, QStringLiteral("扫描批量头无效"));
    return false;
  }
  GroupScanFrameBatch decoded;
  decoded.reserve(static_cast<int>(count));
  for (quint32 index = 0; index < count; ++index) {
    GroupScanFrame item;
    stream >> item.groupId >> item.triggeredModes >> item.bColumn
        >> item.cColumn >> item.cRow >> item.cGateMask
        >> item.amplitudeScale >> item.rangeStart >> item.rangeEnd;
    if (!readFrame(stream, &item.frame, error)) return false;
    decoded.push_back(std::move(item));
  }
  *batch = std::move(decoded);
  return true;
}

QByteArray encodeImuSample(const crawling::ImuSample& sample) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  configureStream(stream);
  stream << sample.rollRad << sample.pitchRad << sample.yawRad
         << sample.gyroXRadps << sample.gyroYRadps << sample.gyroZRadps
         << sample.accelerationXMps2 << sample.accelerationYMps2
         << sample.accelerationZMps2;
  return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

bool decodeImuSample(const QByteArray& payload, crawling::ImuSample* sample,
                     QString* error) {
  if (error) error->clear();
  if (!sample || payload.isEmpty() || payload.size() > 128) {
    if (error) *error = QStringLiteral("IMU 载荷长度无效");
    return false;
  }
  QDataStream stream(payload);
  configureStream(stream);
  crawling::ImuSample decoded;
  stream >> decoded.rollRad >> decoded.pitchRad >> decoded.yawRad
      >> decoded.gyroXRadps >> decoded.gyroYRadps >> decoded.gyroZRadps
      >> decoded.accelerationXMps2 >> decoded.accelerationYMps2
      >> decoded.accelerationZMps2;
  if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
    if (error) *error = QStringLiteral("IMU 载荷不完整");
    return false;
  }
  *sample = decoded;
  return true;
}

QByteArray encodeImagePayload(const QImage& image, int jpegQuality) {
  QByteArray body;
  QDataStream stream(&body, QIODevice::WriteOnly);
  configureStream(stream);
  stream << kDataMagic << kDataVersion << static_cast<quint8>(3);
  writeImage(stream, image, jpegQuality);
  return stream.status() == QDataStream::Ok ? body : QByteArray();
}

bool decodeImagePayload(const QByteArray& payload, QImage* image,
                        QString* error) {
  if (error) error->clear();
  if (!image || payload.size() > kMaxMediaMessageBytes) return false;
  QDataStream stream(payload);
  configureStream(stream);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 type = 0;
  stream >> magic >> version >> type;
  if (magic != kDataMagic || version != kDataVersion || type != 3) {
    setError(error, QStringLiteral("图像帧头无效"));
    return false;
  }
  return readImage(stream, image, error);
}

QByteArray encodePointCloud(const QVector<QVector3D>& points) {
  QByteArray body;
  QDataStream stream(&body, QIODevice::WriteOnly);
  configureStream(stream);
  stream << kDataMagic << kDataVersion << static_cast<quint8>(4)
         << static_cast<quint32>(points.size());
  for (const QVector3D& point : points)
    stream << point.x() << point.y() << point.z();
  return stream.status() == QDataStream::Ok ? qCompress(body, 4) : QByteArray();
}

bool decodePointCloud(const QByteArray& payload, QVector<QVector3D>* points,
                      QString* error) {
  if (error) error->clear();
  if (!points) return false;
  const QByteArray body = qUncompress(payload);
  if (body.isEmpty() || body.size() > kMaxMediaMessageBytes) {
    setError(error, QStringLiteral("点云压缩载荷无效"));
    return false;
  }
  QDataStream stream(body);
  configureStream(stream);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 type = 0;
  quint32 count = 0;
  stream >> magic >> version >> type >> count;
  if (magic != kDataMagic || version != kDataVersion || type != 4 ||
      count > kMaximumPointCloudPoints) {
    setError(error, QStringLiteral("点云帧头无效"));
    return false;
  }
  QVector<QVector3D> decoded;
  decoded.resize(static_cast<int>(count));
  for (QVector3D& point : decoded) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    stream >> x >> y >> z;
    point = QVector3D(x, y, z);
  }
  if (stream.status() != QDataStream::Ok) {
    setError(error, QStringLiteral("点云数据不完整"));
    return false;
  }
  *points = std::move(decoded);
  return true;
}

}  // namespace pa1664::remote
