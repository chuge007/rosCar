#pragma once

#include "protocol_types.h"

#include <QByteArray>

namespace pa1664::remote {

class RealtimeCodec final {
 public:
  static QByteArray encode(const RealtimePacket& packet,
                           const QByteArray& sessionKey,
                           DecodeError* error = nullptr);
  static DecodeResult decode(const QByteArray& datagram,
                             const QByteArray& sessionKey,
                             RealtimePacket* packet,
                             DecodeError* error = nullptr);
  static DecodeResult peekSessionId(const QByteArray& datagram,
                                    quint64* sessionId,
                                    DecodeError* error = nullptr);

  static QByteArray makeSessionKey();
};

}  // namespace pa1664::remote
