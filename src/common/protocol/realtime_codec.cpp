#include "realtime_codec.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace pa1664::remote {
namespace {

void setError(DecodeError* error, DecodeResult result, const QString& detail) {
  if (!error) return;
  error->result = result;
  error->detail = detail;
}

QByteArray authenticationTag(const QByteArray& body, const QByteArray& key) {
  if (key.isEmpty()) return {};
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

}  // namespace

QByteArray RealtimeCodec::makeSessionKey() {
  QByteArray key(32, '\0');
  auto* generator = QRandomGenerator::system();
  for (int offset = 0; offset < key.size(); offset += 4) {
    const quint32 value = generator->generate();
    const int remaining = key.size() - offset;
    const int bytes = remaining < 4 ? remaining : 4;
    for (int i = 0; i < bytes; ++i) {
      key[offset + i] = static_cast<char>((value >> (i * 8)) & 0xFFu);
    }
  }
  return key;
}

QByteArray RealtimeCodec::encode(const RealtimePacket& packet,
                                 const QByteArray& sessionKey,
                                 DecodeError* error) {
  if (error) *error = {};
  if (packet.payload.size() >
      kMaxRealtimeDatagramBytes - kRealtimeHeaderBytes - kRealtimeAuthTagBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("实时数据报载荷过长"));
    return {};
  }

  RealtimeFlags flags = packet.flags;
  if (!sessionKey.isEmpty()) flags = flags | RealtimeFlags::AuthPresent;

  QByteArray body;
  body.reserve(kRealtimeHeaderBytes + packet.payload.size());
  QDataStream stream(&body, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << kWireMagic;
  stream << kProtocolVersion;
  stream << static_cast<quint8>(packet.type);
  stream << static_cast<quint8>(flags);
  stream << packet.sessionId;
  stream << packet.sequence;
  stream << packet.timestampUs;
  stream << packet.ttlMs;
  stream << packet.linearMilliMps;
  stream << packet.angularMilliRadps;
  stream << static_cast<quint16>(packet.payload.size());
  if (!packet.payload.isEmpty())
    stream.writeRawData(packet.payload.constData(), packet.payload.size());
  if (stream.status() != QDataStream::Ok ||
      body.size() != kRealtimeHeaderBytes + packet.payload.size()) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("实时数据报编码失败"));
    return {};
  }

  const QByteArray tag = authenticationTag(body, sessionKey);
  body.append(tag);
  if (body.size() > kMaxRealtimeDatagramBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("实时数据报超过 MTU 上限"));
    return {};
  }
  return body;
}

DecodeResult RealtimeCodec::peekSessionId(const QByteArray& datagram,
                                              quint64* sessionId,
                                              DecodeError* error) {
  if (error) *error = {};
  if (sessionId) *sessionId = 0;
  if (datagram.size() < kRealtimeHeaderBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("实时数据报不完整"));
    return DecodeResult::Incomplete;
  }
  if (datagram.size() > kMaxRealtimeDatagramBytes) {
    setError(error, DecodeResult::InvalidLength, QStringLiteral("实时数据报过大"));
    return DecodeResult::InvalidLength;
  }
  QDataStream stream(datagram);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 rawType = 0;
  quint8 rawFlags = 0;
  quint64 value = 0;
  stream >> magic >> version >> rawType >> rawFlags >> value;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("实时数据报头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("实时数据报魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的实时协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (rawType < static_cast<quint8>(RealtimePacketType::Heartbeat) ||
      rawType > static_cast<quint8>(RealtimePacketType::Ack)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("未知实时包类型：%1").arg(rawType));
    return DecodeResult::InvalidPayload;
  }
  if (sessionId) *sessionId = value;
  return DecodeResult::Ok;
}

DecodeResult RealtimeCodec::decode(const QByteArray& datagram,
                                   const QByteArray& sessionKey,
                                   RealtimePacket* packet,
                                   DecodeError* error) {
  if (error) *error = {};
  if (!packet || datagram.size() < kRealtimeHeaderBytes) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("实时数据报不完整"));
    return DecodeResult::Incomplete;
  }
  if (datagram.size() > kMaxRealtimeDatagramBytes) {
    setError(error, DecodeResult::InvalidLength, QStringLiteral("实时数据报过大"));
    return DecodeResult::InvalidLength;
  }

  QDataStream stream(datagram);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint8 rawType = 0;
  quint8 rawFlags = 0;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  quint64 timestampUs = 0;
  quint16 ttlMs = 0;
  qint32 linear = 0;
  qint32 angular = 0;
  quint16 payloadBytes = 0;
  stream >> magic >> version >> rawType >> rawFlags >> sessionId >> sequence >>
      timestampUs >> ttlMs >> linear >> angular >> payloadBytes;
  if (stream.status() != QDataStream::Ok) {
    setError(error, DecodeResult::Incomplete, QStringLiteral("实时数据报头不完整"));
    return DecodeResult::Incomplete;
  }
  if (magic != kWireMagic) {
    setError(error, DecodeResult::BadMagic, QStringLiteral("实时数据报魔数错误"));
    return DecodeResult::BadMagic;
  }
  if (version != kProtocolVersion) {
    setError(error, DecodeResult::UnsupportedVersion,
             QStringLiteral("不支持的实时协议版本：%1").arg(version));
    return DecodeResult::UnsupportedVersion;
  }
  if (rawType < static_cast<quint8>(RealtimePacketType::Heartbeat) ||
      rawType > static_cast<quint8>(RealtimePacketType::Ack)) {
    setError(error, DecodeResult::InvalidPayload,
             QStringLiteral("未知实时包类型：%1").arg(rawType));
    return DecodeResult::InvalidPayload;
  }

  const RealtimeFlags flags = static_cast<RealtimeFlags>(rawFlags);
  const int tagBytes = hasFlag(flags, RealtimeFlags::AuthPresent)
                           ? kRealtimeAuthTagBytes
                           : 0;
  const qint64 totalBytes = static_cast<qint64>(kRealtimeHeaderBytes) +
                            payloadBytes + tagBytes;
  if (datagram.size() < totalBytes) return DecodeResult::Incomplete;
  if (datagram.size() != totalBytes) {
    setError(error, DecodeResult::InvalidLength,
             QStringLiteral("实时数据报长度与头字段不一致"));
    return DecodeResult::InvalidLength;
  }

  const QByteArray body = datagram.left(kRealtimeHeaderBytes + payloadBytes);
  if (sessionKey.isEmpty()) {
    if (tagBytes != 0) {
      setError(error, DecodeResult::AuthenticationFailed,
               QStringLiteral("收到带认证的实时包，但本地没有会话密钥"));
      return DecodeResult::AuthenticationFailed;
    }
  } else {
    if (tagBytes == 0) {
      setError(error, DecodeResult::AuthenticationFailed,
               QStringLiteral("实时包缺少认证标签"));
      return DecodeResult::AuthenticationFailed;
    }
    const QByteArray actualTag = datagram.right(kRealtimeAuthTagBytes);
    const QByteArray expectedTag = authenticationTag(body, sessionKey);
    if (!constantTimeEquals(actualTag, expectedTag)) {
      setError(error, DecodeResult::AuthenticationFailed,
               QStringLiteral("实时包 HMAC 校验失败"));
      return DecodeResult::AuthenticationFailed;
    }
  }

  packet->type = static_cast<RealtimePacketType>(rawType);
  packet->flags = flags;
  packet->sessionId = sessionId;
  packet->sequence = sequence;
  packet->timestampUs = timestampUs;
  packet->ttlMs = ttlMs;
  packet->linearMilliMps = linear;
  packet->angularMilliRadps = angular;
  packet->payload = datagram.mid(kRealtimeHeaderBytes, payloadBytes);
  return DecodeResult::Ok;
}

}  // namespace pa1664::remote
