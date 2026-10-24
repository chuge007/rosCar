#pragma once

#include "protocol_types.h"

#include <QByteArray>
#include <QVector>

namespace pa1664::remote {

class ControlCodec final {
 public:
  static QByteArray encode(const ControlFrame& frame, DecodeError* error = nullptr);
  static DecodeResult decodeOne(const QByteArray& bytes, ControlFrame* frame,
                                int* consumedBytes, DecodeError* error = nullptr);
  static quint32 crc32(const char* data, qsizetype size);
};

// Incremental TCP decoder. The caller may feed arbitrary chunks; complete
// frames are returned in order and partial bytes are retained internally.
class ControlStreamDecoder final {
 public:
  explicit ControlStreamDecoder(int maximumPayloadBytes = kMaxControlPayloadBytes);

  QVector<ControlFrame> append(const QByteArray& bytes, DecodeError* error = nullptr);
  void reset();
  int bufferedBytes() const { return buffer_.size(); }

 private:
  QByteArray buffer_;
  int maximumPayloadBytes_ = kMaxControlPayloadBytes;
};

}  // namespace pa1664::remote
