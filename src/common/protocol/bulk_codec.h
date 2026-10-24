#pragma once

#include "protocol_types.h"

#include <QByteArray>
#include <QVector>

namespace pa1664::remote {

// Reliable byte stream for scan batches, recordings and configuration.  It
// uses its own TCP connection so large writes can never delay control or
// realtime UDP traffic.
class BulkCodec final {
 public:
  static QByteArray encode(const BulkFrame& frame, const QByteArray& sessionKey,
                           DecodeError* error = nullptr);
  static DecodeResult decodeOne(const QByteArray& bytes, const QByteArray& sessionKey,
                                BulkFrame* frame, int* consumedBytes,
                                DecodeError* error = nullptr);
  static DecodeResult peekSessionId(const QByteArray& bytes, quint64* sessionId,
                                    DecodeError* error = nullptr);
};

class BulkStreamDecoder final {
 public:
  explicit BulkStreamDecoder(const QByteArray& sessionKey = {});
  void setSessionKey(const QByteArray& sessionKey) { sessionKey_ = sessionKey; }

  QVector<BulkFrame> append(const QByteArray& bytes, DecodeError* error = nullptr);
  void reset();
  int bufferedBytes() const { return buffer_.size(); }

 private:
  QByteArray buffer_;
  QByteArray sessionKey_;
};

}  // namespace pa1664::remote

