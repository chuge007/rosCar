#pragma once

#include "protocol_types.h"

#include <QByteArray>

namespace pa1664::remote {

// Authenticated UDP fragmentation for latest-value media.  The control path
// never waits for this codec: a lost or late fragment only invalidates that
// media message and is replaced by the next one.
class MediaCodec final {
 public:
  static QByteArray encode(const MediaFragmentPacket& packet,
                           const QByteArray& sessionKey,
                           DecodeError* error = nullptr);
  static DecodeResult decode(const QByteArray& datagram,
                             const QByteArray& sessionKey,
                             MediaFragmentPacket* packet,
                             DecodeError* error = nullptr);
  static DecodeResult peekSessionId(const QByteArray& datagram,
                                    quint64* sessionId,
                                    DecodeError* error = nullptr);
};

}  // namespace pa1664::remote
