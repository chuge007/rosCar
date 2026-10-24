#include "media_codec.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QMessageAuthenticationCode>

namespace pa1664::remote {
namespace {

constexpr quint8 kAuthPresent = 1u << 0;

void setError(DecodeError* error, DecodeResult result, const QString& detail) {
  if (!error) return;
  error->result = result;
  error->detail = detail;
}

QByteArray authenticationTag(const QByteArray& body, const QByteArray& key) {
  QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
  mac.addData(body);
  return mac.result().left(kMediaAuthTagBytes);
}

bool constantTimeEquals(const QByteArray& left, const QByteArray& right) {
  if (left.size() != right.size()) return false;
  quint8 difference = 0;
  for (int i = 0; i < left.size(); ++i) {
    difference |= static_cast<quint8>(left.at(i) ^ right.at(i));
  }
  return difference == 0;
}

bool validKind(quint8 rawKind) {
  return rawKind >= static_cast<quint8>(MediaStreamKind::Heartbeat) &&
         rawKind <= static_cast<quint8>(MediaStreamKind::ImuSample);
}

}  // namespace

QByteArray MediaCodec::encode(const MediaFragmentPacket& packet,
                              const QByteArray& sessionKey,
                              DecodeError* error) {
  if (error) *error = {};
  if (sessionKey.size() != 32) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("媒体通道需要 32 字节会话密钥"));
    return {};
  }
  const int maximumPayload =
      kMaxRealtimeDatagramBytes - kMediaHeaderBytes - kMediaAuthTagBytes;
  if (packet.payload.size() > maximumPayload ||
      packet.fragmentCount == 0 ||
      packet.fragmentIndex >= packet.fragmentCount ||
      packet.totalBytes > static_cast<quint32>(kMaxMediaMessageBytes)) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("媒体分片字段或载荷长度无效"));
    return {};
  }

  QByteArray body;
  body.reserve(kMediaHeaderBytes + packet.payload.size());
  QDataStream stream(&body, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << kWireMagic;
  stream << kProtocolVersion;
  stream << kAuthPresent;
  stream << static_cast<quint8>(packet.kind);
  stream << packet.sessionId;
  stream << packet.streamId;
  stream << packet.messageId;
  stream << packet.sequence;
  stream << packet.timestampUs;
  stream << packet.totalBytes;
  stream << packet.fragmentIndex;
  stream << packet.fragmentCount;
  stream << static_cast<quint32>(packet.payload.size());
  if (!packet.payload.isEmpty())
    stream.writeRawData(packet.payload.constData(), packet.payload.size());
  if (stream.status() != QDataStream::Ok ||
      body.size() != kMediaHeaderBytes + packet.payload.size()) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("媒体分片编码失败"));
    return {};
  }
  const QByteArray tag = authenticationTag(body, sessionKey);
  body.append(tag);
  if (body.size() > kMaxRealtimeDatagramBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("媒体数据报超过 MTU 上限"));
    return {};
  }
  return body;
}

DecodeResult MediaCodec::peekSessionId(const QByteArray& datagram,
                                       quint64* sessionId,
                                       DecodeError* error) {
  if (error) *error = {};
  if (sessionId) *sessionId = 0;
  if (datagram.size() < kMediaHeaderBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("媒体分片不完整"));
    return DecodeResult::Incomplete;
  }
  if (datagram.size() > kMaxRealtimeDatagramBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("媒体数据报过大"));
    return DecodeResult::InvalidLength;
  }
  QDataStream stream(datagram);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 flags = 0;
  quint8 kind = 0;
  quint64 value = 0;
  stream >> magic >> version >> flags >> kind >> value;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("媒体分片头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("媒体分片魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的媒体协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (!validKind(kind)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("未知媒体流类型：%1").arg(kind));
    return DecodeResult::InvalidPayload;
  }
  if (!(flags & kAuthPresent)) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("媒体分片缺少认证标志"));
    return DecodeResult::AuthenticationFailed;
  }
  if (sessionId) *sessionId = value;
  return DecodeResult::Ok;
}

DecodeResult MediaCodec::decode(const QByteArray& datagram,
                                const QByteArray& sessionKey,
                                MediaFragmentPacket* packet,
                                DecodeError* error) {
  if (error) *error = {};
  if (!packet || datagram.size() < kMediaHeaderBytes + kMediaAuthTagBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("媒体分片不完整"));
    return DecodeResult::Incomplete;
  }
  if (sessionKey.size() != 32) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("媒体通道没有有效会话密钥"));
    return DecodeResult::AuthenticationFailed;
  }

  QDataStream stream(datagram);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 flags = 0;
  quint8 rawKind = 0;
  quint64 sessionId = 0;
  quint16 streamId = 0;
  quint64 messageId = 0;
  quint64 sequence = 0;
  quint64 timestampUs = 0;
  quint32 totalBytes = 0;
  quint32 fragmentIndex = 0;
  quint32 fragmentCount = 0;
  quint32 payloadBytes = 0;
  stream >> magic >> version >> flags >> rawKind >> sessionId >> streamId >>
      messageId >> sequence >> timestampUs >> totalBytes >> fragmentIndex >>
      fragmentCount >> payloadBytes;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("媒体分片头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("媒体分片魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的媒体协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (!validKind(rawKind) || !(flags & kAuthPresent)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("媒体分片类型或标志无效"));
    return DecodeResult::InvalidPayload;
  }
  if (fragmentCount == 0 || fragmentIndex >= fragmentCount ||
      fragmentCount > static_cast<quint32>(kMaxMediaFragments) ||
      totalBytes > static_cast<quint32>(kMaxMediaMessageBytes) ||
      payloadBytes > static_cast<quint32>(
                         kMaxRealtimeDatagramBytes - kMediaHeaderBytes -
                         kMediaAuthTagBytes)) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("媒体分片长度字段无效"));
    return DecodeResult::InvalidLength;
  }
  const qint64 expected = static_cast<qint64>(kMediaHeaderBytes) +
                          payloadBytes + kMediaAuthTagBytes;
  if (datagram.size() != expected) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("媒体分片长度与头字段不一致"));
    return DecodeResult::InvalidLength;
  }
  const QByteArray body = datagram.left(kMediaHeaderBytes + payloadBytes);
  const QByteArray expectedTag = authenticationTag(body, sessionKey);
  if (!constantTimeEquals(datagram.right(kMediaAuthTagBytes), expectedTag)) {
    setError(error, DecodeResult::AuthenticationFailed,
             QStringLiteral("媒体分片 HMAC 校验失败"));
    return DecodeResult::AuthenticationFailed;
  }

  packet->kind = static_cast<MediaStreamKind>(rawKind);
  packet->flags = flags;
  packet->sessionId = sessionId;
  packet->streamId = streamId;
  packet->messageId = messageId;
  packet->sequence = sequence;
  packet->timestampUs = timestampUs;
  packet->totalBytes = totalBytes;
  packet->fragmentIndex = fragmentIndex;
  packet->fragmentCount = fragmentCount;
  packet->payload = datagram.mid(kMediaHeaderBytes, payloadBytes);
  return DecodeResult::Ok;
}

}  // namespace pa1664::remote
