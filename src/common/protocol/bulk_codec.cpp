#include "bulk_codec.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QMessageAuthenticationCode>

namespace pa1664::remote {
namespace {

void setError(DecodeError* error, DecodeResult result, const QString& detail) {
  if (!error) return;
  error->result = result;
  error->detail = detail;
}

QByteArray authenticationTag(const QByteArray& body, const QByteArray& key) {
  QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
  mac.addData(body);
  return mac.result().left(kRealtimeAuthTagBytes);
}

bool constantTimeEquals(const QByteArray& left, const QByteArray& right) {
  if (left.size() != right.size()) return false;
  quint8 difference = 0;
  for (int i = 0; i < left.size(); ++i) {
    difference |= static_cast<quint8>(left.at(i) ^ right.at(i));
  }
  return difference == 0;
}

bool validKind(quint16 kind) {
  return kind >= static_cast<quint16>(BulkStreamKind::Heartbeat) &&
         kind <= static_cast<quint16>(BulkStreamKind::JsonEvent);
}

}  // namespace

QByteArray BulkCodec::encode(const BulkFrame& frame,
                             const QByteArray& sessionKey,
                             DecodeError* error) {
  if (error) *error = {};
  if (sessionKey.size() != 32 || frame.sessionId == 0 ||
      frame.payload.size() > kMaxBulkPayloadBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("批量通道帧字段或载荷长度无效"));
    return {};
  }
  QByteArray body;
  body.reserve(kBulkHeaderBytes + frame.payload.size());
  QDataStream stream(&body, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << kWireMagic;
  stream << kProtocolVersion;
  stream << static_cast<quint16>(frame.kind);
  stream << frame.sessionId;
  stream << frame.sequence;
  stream << static_cast<quint32>(frame.payload.size());
  if (!frame.payload.isEmpty())
    stream.writeRawData(frame.payload.constData(), frame.payload.size());
  if (stream.status() != QDataStream::Ok ||
      body.size() != kBulkHeaderBytes + frame.payload.size()) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("批量通道帧编码失败"));
    return {};
  }
  body.append(authenticationTag(body, sessionKey));
  return body;
}

DecodeResult BulkCodec::peekSessionId(const QByteArray& bytes,
                                      quint64* sessionId,
                                      DecodeError* error) {
  if (error) *error = {};
  if (sessionId) *sessionId = 0;
  if (bytes.size() < kBulkHeaderBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("批量通道帧不完整"));
    return DecodeResult::Incomplete;
  }
  QDataStream stream(bytes);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint16 kind = 0;
  quint64 value = 0;
  stream >> magic >> version >> kind >> value;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("批量通道头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("批量通道魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的批量协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (!validKind(kind)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("未知批量通道类型：%1").arg(kind));
    return DecodeResult::InvalidPayload;
  }
  if (sessionId) *sessionId = value;
  return DecodeResult::Ok;
}

DecodeResult BulkCodec::decodeOne(const QByteArray& bytes,
                                  const QByteArray& sessionKey,
                                  BulkFrame* frame, int* consumedBytes,
                                  DecodeError* error) {
  if (error) *error = {};
  if (consumedBytes) *consumedBytes = 0;
  if (!frame || bytes.size() < kBulkHeaderBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("批量通道帧不完整"));
    return DecodeResult::Incomplete;
  }
  if (sessionKey.size() != 32) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("批量通道没有有效会话密钥"));
    return DecodeResult::AuthenticationFailed;
  }

  QDataStream stream(bytes);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint16 kind = 0;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  quint32 payloadBytes = 0;
  stream >> magic >> version >> kind >> sessionId >> sequence >> payloadBytes;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("批量通道头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("批量通道魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的批量协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (!validKind(kind) ||
      payloadBytes > static_cast<quint32>(kMaxBulkPayloadBytes)) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("批量通道头字段无效"));
    return DecodeResult::InvalidLength;
  }
  const qint64 total = static_cast<qint64>(kBulkHeaderBytes) + payloadBytes +
                       kRealtimeAuthTagBytes;
  if (bytes.size() < total) return DecodeResult::Incomplete;

  const QByteArray body = bytes.left(kBulkHeaderBytes + payloadBytes);
  const QByteArray expectedTag = authenticationTag(body, sessionKey);
  if (!constantTimeEquals(bytes.mid(kBulkHeaderBytes + payloadBytes,
                                    kRealtimeAuthTagBytes),
                          expectedTag)) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("批量通道 HMAC 校验失败"));
    return DecodeResult::AuthenticationFailed;
  }

  frame->kind = static_cast<BulkStreamKind>(kind);
  frame->sessionId = sessionId;
  frame->sequence = sequence;
  frame->payload = body.mid(kBulkHeaderBytes);
  if (consumedBytes) *consumedBytes = static_cast<int>(total);
  return DecodeResult::Ok;
}

BulkStreamDecoder::BulkStreamDecoder(const QByteArray& sessionKey)
    : sessionKey_(sessionKey) {}

QVector<BulkFrame> BulkStreamDecoder::append(const QByteArray& bytes,
                                             DecodeError* error) {
  if (error) *error = {};
  if (!bytes.isEmpty()) buffer_.append(bytes);
  QVector<BulkFrame> frames;
  while (buffer_.size() >= kBulkHeaderBytes) {
    BulkFrame frame;
    int consumed = 0;
    const DecodeResult result =
        BulkCodec::decodeOne(buffer_, sessionKey_, &frame, &consumed, error);
    if (result == DecodeResult::Incomplete) break;
    if (result != DecodeResult::Ok) {
      buffer_.clear();
      break;
    }
    buffer_.remove(0, consumed);
    frames.push_back(std::move(frame));
  }
  return frames;
}

void BulkStreamDecoder::reset() { buffer_.clear(); }

}  // namespace pa1664::remote
