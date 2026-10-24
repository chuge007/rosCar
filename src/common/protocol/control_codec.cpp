#include "control_codec.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <array>

namespace pa1664::remote {
namespace {

constexpr quint32 kCrcPolynomial = 0xEDB88320u;

void setError(DecodeError* error, DecodeResult result, const QString& detail) {
  if (!error) return;
  error->result = result;
  error->detail = detail;
}

}  // namespace

quint32 ControlCodec::crc32(const char* data, qsizetype size) {
  static const std::array<quint32, 256> table = [] {
    std::array<quint32, 256> values{};
    for (quint32 i = 0; i < values.size(); ++i) {
      quint32 value = i;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) != 0u ? (value >> 1u) ^ kCrcPolynomial
                                   : value >> 1u;
      }
      values[i] = value;
    }
    return values;
  }();

  if (!data || size <= 0) return 0u;
  quint32 value = 0xFFFFFFFFu;
  for (qsizetype i = 0; i < size; ++i) {
    const quint8 byte = static_cast<quint8>(data[i]);
    value = table[(value ^ byte) & 0xFFu] ^ (value >> 8u);
  }
  return value ^ 0xFFFFFFFFu;
}

QByteArray ControlCodec::encode(const ControlFrame& frame, DecodeError* error) {
  if (error) *error = {};
  if (frame.payload.size() > kMaxControlPayloadBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("控制消息超过最大允许大小"));
    return {};
  }

  QByteArray result;
  result.reserve(kControlHeaderBytes + frame.payload.size());
  QDataStream stream(&result, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << kWireMagic;
  stream << kProtocolVersion;
  stream << static_cast<quint16>(frame.type);
  stream << static_cast<quint16>(frame.flags);
  stream << frame.sessionId;
  stream << frame.sequence;
  stream << frame.requestId;
  stream << frame.timestampUs;
  stream << static_cast<quint32>(frame.payload.size());
  stream << crc32(frame.payload.constData(), frame.payload.size());
  if (!frame.payload.isEmpty())
    stream.writeRawData(frame.payload.constData(), frame.payload.size());
  if (stream.status() != QDataStream::Ok ||
      result.size() != kControlHeaderBytes + frame.payload.size()) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("控制消息编码失败"));
    return {};
  }
  return result;
}

DecodeResult ControlCodec::decodeOne(const QByteArray& bytes, ControlFrame* frame,
                                     int* consumedBytes, DecodeError* error) {
  if (error) *error = {};
  if (consumedBytes) *consumedBytes = 0;
  if (!frame) {
    setError(error, DecodeResult::InvalidPayload, QStringLiteral("输出帧为空"));
    return DecodeResult::InvalidPayload;
  }
  if (bytes.size() < kControlHeaderBytes) return DecodeResult::Incomplete;

  QDataStream stream(bytes);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);

  quint32 magic = 0;
  quint16 version = 0;
  quint16 rawType = 0;
  quint16 rawFlags = 0;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  quint64 requestId = 0;
  quint64 timestampUs = 0;
  quint32 payloadBytes = 0;
  quint32 expectedCrc = 0;
  stream >> magic >> version >> rawType >> rawFlags >> sessionId >> sequence >>
      requestId >> timestampUs >> payloadBytes >> expectedCrc;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::InvalidLength, QStringLiteral("控制消息头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("控制消息魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的控制协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (payloadBytes > static_cast<quint32>(kMaxControlPayloadBytes)) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("控制消息载荷过长：%1").arg(payloadBytes));
    return DecodeResult::InvalidLength;
  }
  const qint64 totalBytes = static_cast<qint64>(kControlHeaderBytes) + payloadBytes;
  if (bytes.size() < totalBytes) return DecodeResult::Incomplete;

  if (rawType < static_cast<quint16>(ControlMessageType::Hello) ||
      rawType > static_cast<quint16>(ControlMessageType::Error)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("未知控制消息类型：%1").arg(rawType));
    return DecodeResult::InvalidPayload;
  }

  const char* payload = bytes.constData() + kControlHeaderBytes;
  const quint32 actualCrc = crc32(payload, payloadBytes);
  if (actualCrc != expectedCrc) {
    setError(error, DecodeResult::ChecksumMismatch,
             QStringLiteral("控制消息 CRC 校验失败"));
    return DecodeResult::ChecksumMismatch;
  }

  frame->type = static_cast<ControlMessageType>(rawType);
  frame->flags = static_cast<ControlFlags>(rawFlags);
  frame->sessionId = sessionId;
  frame->sequence = sequence;
  frame->requestId = requestId;
  frame->timestampUs = timestampUs;
  frame->payload = bytes.mid(kControlHeaderBytes, payloadBytes);
  if (consumedBytes) *consumedBytes = static_cast<int>(totalBytes);
  return DecodeResult::Ok;
}

ControlStreamDecoder::ControlStreamDecoder(int maximumPayloadBytes)
    : maximumPayloadBytes_(std::clamp(maximumPayloadBytes, 1,
                                      kMaxControlPayloadBytes)) {}

QVector<ControlFrame> ControlStreamDecoder::append(const QByteArray& bytes,
                                                   DecodeError* error) {
  if (error) *error = {};
  if (!bytes.isEmpty()) buffer_.append(bytes);

  QVector<ControlFrame> frames;
  while (!buffer_.isEmpty()) {
    if (buffer_.size() < kControlHeaderBytes) break;

    QDataStream header(buffer_);
    header.setByteOrder(QDataStream::LittleEndian);
    header.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    header >> magic >> version;
    if (magic != kWireMagic) {
      setError(error, DecodeResult::BadMagic, QStringLiteral("控制流魔数错误"));
      buffer_.clear();
      break;
    }
    if (version != kProtocolVersion) {
      setError(error, DecodeResult::UnsupportedVersion,
               QStringLiteral("不支持的控制协议版本：%1").arg(version));
      buffer_.clear();
      break;
    }

    QDataStream payloadHeader(buffer_);
    payloadHeader.setByteOrder(QDataStream::LittleEndian);
    payloadHeader.setVersion(QDataStream::Qt_6_0);
    payloadHeader.skipRawData(4 + 2 + 2 + 2 + 8 + 8 + 8 + 8);
    quint32 payloadBytes = 0;
    payloadHeader >> payloadBytes;
    if (payloadHeader.status() != QDataStream::Ok) {
      buffer_.clear();
      setError(error, DecodeResult::InvalidLength,
               QStringLiteral("控制流长度字段无效"));
      break;
    }
    if (payloadBytes > static_cast<quint32>(maximumPayloadBytes_)) {
      setError(error, DecodeResult::InvalidLength,
               QStringLiteral("控制流载荷过长：%1").arg(payloadBytes));
      buffer_.clear();
      break;
    }
    const qint64 totalBytes = static_cast<qint64>(kControlHeaderBytes) + payloadBytes;
    if (buffer_.size() < totalBytes) break;

    const QByteArray oneFrame = buffer_.left(static_cast<int>(totalBytes));
    ControlFrame frame;
    int consumed = 0;
    const DecodeResult result = decodeOne(oneFrame, &frame, &consumed, error);
    if (result != DecodeResult::Ok) {
      buffer_.clear();
      break;
    }
    buffer_.remove(0, consumed);
    frames.push_back(std::move(frame));
  }
  return frames;
}

void ControlStreamDecoder::reset() { buffer_.clear(); }

QString decodeResultText(DecodeResult result) {
  switch (result) {
    case DecodeResult::Ok:
      return QStringLiteral("OK");
    case DecodeResult::Incomplete:
      return QStringLiteral("INCOMPLETE");
    case DecodeResult::BadMagic:
      return QStringLiteral("BAD_MAGIC");
    case DecodeResult::UnsupportedVersion:
      return QStringLiteral("UNSUPPORTED_VERSION");
    case DecodeResult::InvalidLength:
      return QStringLiteral("INVALID_LENGTH");
    case DecodeResult::ChecksumMismatch:
      return QStringLiteral("CHECKSUM_MISMATCH");
    case DecodeResult::AuthenticationFailed:
      return QStringLiteral("AUTHENTICATION_FAILED");
    case DecodeResult::InvalidPayload:
      return QStringLiteral("INVALID_PAYLOAD");
  }
  return QStringLiteral("UNKNOWN");
}

QString controlMessageTypeText(ControlMessageType type) {
  switch (type) {
    case ControlMessageType::Hello:
      return QStringLiteral("hello");
    case ControlMessageType::HelloAck:
      return QStringLiteral("helloAck");
    case ControlMessageType::Request:
      return QStringLiteral("request");
    case ControlMessageType::Response:
      return QStringLiteral("response");
    case ControlMessageType::Event:
      return QStringLiteral("event");
    case ControlMessageType::Ping:
      return QStringLiteral("ping");
    case ControlMessageType::Pong:
      return QStringLiteral("pong");
    case ControlMessageType::Error:
      return QStringLiteral("error");
  }
  return QStringLiteral("unknown");
}

}  // namespace pa1664::remote
