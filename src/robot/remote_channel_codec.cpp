#include "remote_channel_codec.h"

#include <QDateTime>

namespace crawling {
namespace {

int checksumBytes(RemoteChecksumKind kind) {
  return kind == RemoteChecksumKind::Crc16Modbus ? 2 : 1;
}

quint16 computeChecksum(RemoteChecksumKind kind, const char *data, int size) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(data);
  switch (kind) {
    case RemoteChecksumKind::Sum8: {
      quint8 sum = 0;
      for (int i = 0; i < size; ++i) sum = static_cast<quint8>(sum + bytes[i]);
      return sum;
    }
    case RemoteChecksumKind::Xor8: {
      quint8 value = 0;
      for (int i = 0; i < size; ++i) value ^= bytes[i];
      return value;
    }
    case RemoteChecksumKind::Crc8: {
      quint8 crc = 0x00;
      for (int i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
          crc = (crc & 0x80) ? static_cast<quint8>((crc << 1) ^ 0x07)
                             : static_cast<quint8>(crc << 1);
      }
      return crc;
    }
    case RemoteChecksumKind::Crc16Modbus: {
      quint16 crc = 0xFFFF;
      for (int i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
          crc = (crc & 0x0001) ? static_cast<quint16>((crc >> 1) ^ 0xA001)
                               : static_cast<quint16>(crc >> 1);
      }
      return crc;
    }
    case RemoteChecksumKind::None:
    default:
      return 0;
  }
}

} // namespace

RemoteChannelCodec::RemoteChannelCodec(const RemotePlatformProfile &profile)
    : profile_(profile) {}

void RemoteChannelCodec::setProfile(const RemotePlatformProfile &profile) {
  profile_ = profile;
  sequence_ = 0;
}

QByteArray RemoteChannelCodec::requestFrame() const {
  if (profile_.frame.streaming) return {};
  return profile_.frame.request;
}

bool RemoteChannelCodec::takeFrame(QByteArray *buffer, RemoteChannelSample *sample) {
  if (!buffer || !sample) return false;
  const auto &format = profile_.frame;
  const QByteArray header = format.header;
  if (header.isEmpty()) return false;

  // 丢掉帧头之前的杂字节。串口刚打开时可能读到半帧或设备回显，不认为出错。
  const int headerIndex = buffer->indexOf(header);
  if (headerIndex < 0) {
    if (buffer->size() > 4096) buffer->clear();
    return false;
  }
  if (headerIndex > 0) buffer->remove(0, headerIndex);

  int totalLength = 0;
  if (format.fixedFrameLength > 0) {
    totalLength = format.fixedFrameLength;
    if (buffer->size() < totalLength) return false;
  } else if (format.lengthFieldOffset >= 0) {
    const int lengthSize = format.lengthFieldSize == 1 ? 1 : 2;
    const int prefix = format.lengthFieldOffset + lengthSize;
    if (buffer->size() < prefix) return false;
    int payloadLength = 0;
    if (lengthSize == 1) {
      payloadLength = static_cast<unsigned char>(buffer->at(format.lengthFieldOffset));
    } else {
      const int b0 = static_cast<unsigned char>(buffer->at(format.lengthFieldOffset));
      const int b1 = static_cast<unsigned char>(buffer->at(format.lengthFieldOffset + 1));
      payloadLength = format.lengthBigEndian ? (b0 << 8) | b1 : b0 | (b1 << 8);
    }
    totalLength = payloadLength + format.lengthExtra;
    if (totalLength <= prefix || totalLength > 4096) {
      lastError_ = QStringLiteral("帧长字段非法：%1").arg(payloadLength);
      ++parseErrorCount_;
      buffer->remove(0, 1);
      return false;
    }
    if (buffer->size() < totalLength) return false;
  } else {
    lastError_ = QStringLiteral("平台未定义帧长规则");
    ++parseErrorCount_;
    buffer->clear();
    return false;
  }

  const QByteArray frame = buffer->left(totalLength);

  if (format.answerTypeOffset >= 0 && format.answerTypeValue >= 0 &&
      format.answerTypeOffset < totalLength) {
    const int type = static_cast<unsigned char>(frame.at(format.answerTypeOffset));
    if (type != format.answerTypeValue) {
      lastError_ = QStringLiteral("应答类型不匹配：0x%1")
                       .arg(type, 2, 16, QLatin1Char('0'));
      ++parseErrorCount_;
      buffer->remove(0, 1);
      return false;
    }
  }

  if (format.trailer >= 0 &&
      static_cast<unsigned char>(frame.at(totalLength - 1)) != format.trailer) {
    lastError_ = QStringLiteral("帧尾校验失败：0x%1")
                     .arg(static_cast<unsigned char>(frame.at(totalLength - 1)), 2, 16,
                          QLatin1Char('0'));
    ++parseErrorCount_;
    buffer->remove(0, 1);
    return false;
  }

  if (format.checksum != RemoteChecksumKind::None) {
    const int bytes = checksumBytes(format.checksum);
    const int checksumIndex = totalLength - format.checksumOffsetFromEnd;
    if (checksumIndex < 1 || checksumIndex + bytes > totalLength) {
      lastError_ = QStringLiteral("校验字段位置越界");
      ++parseErrorCount_;
      buffer->remove(0, 1);
      return false;
    }
    const int start = format.checksumCoversHeader ? 0 : header.size();
    if (checksumIndex > start) {
      const quint16 expected =
          computeChecksum(format.checksum, frame.constData() + start,
                          checksumIndex - start);
      quint16 actual = 0;
      if (bytes == 1) {
        actual = static_cast<unsigned char>(frame.at(checksumIndex));
      } else {
        const int b0 = static_cast<unsigned char>(frame.at(checksumIndex));
        const int b1 = static_cast<unsigned char>(frame.at(checksumIndex + 1));
        actual = format.checksumBigEndian ? (b0 << 8) | b1 : b0 | (b1 << 8);
      }
      if (actual != expected) {
        lastError_ = QStringLiteral("校验失败：期望 0x%1，实际 0x%2")
                         .arg(expected, 4, 16, QLatin1Char('0'))
                         .arg(actual, 4, 16, QLatin1Char('0'));
        ++parseErrorCount_;
        buffer->remove(0, 1);
        return false;
      }
    }
  }

  if (!parseFrame(frame, sample)) {
    buffer->remove(0, 1);
    return false;
  }
  buffer->remove(0, totalLength);
  return true;
}

bool RemoteChannelCodec::parseFrame(const QByteArray &frame,
                                    RemoteChannelSample *sample) {
  const auto &format = profile_.frame;
  // 可用数据区：[0, dataEnd)。扣掉尾字节与校验字段，避免把它们误读成通道值。
  int dataEnd = frame.size();
  if (format.trailer >= 0) --dataEnd;
  if (format.checksum != RemoteChecksumKind::None)
    dataEnd -= checksumBytes(format.checksum);

  for (const auto &channel : profile_.channels) {
    const int need = channel.valueSize == 1 ? 1 : 2;
    if (channel.offset < 0 || channel.offset + need > dataEnd) continue;
    int value = 0;
    if (need == 1) {
      value = static_cast<unsigned char>(frame.at(channel.offset));
    } else {
      const int b0 = static_cast<unsigned char>(frame.at(channel.offset));
      const int b1 = static_cast<unsigned char>(frame.at(channel.offset + 1));
      value = channel.bigEndian ? (b0 << 8) | b1 : b0 | (b1 << 8);
    }
    sample->values.insert(channel.key, value);
  }
  for (const auto &button : profile_.buttons) {
    if (button.offset < 0 || button.offset >= dataEnd) continue;
    const int raw =
        (static_cast<unsigned char>(frame.at(button.offset)) >> button.bit) & 0x1;
    const bool pressed = button.activeLow ? (raw == 0) : (raw != 0);
    sample->buttons.insert(button.key, pressed);
  }
  sample->sequence = ++sequence_;
  sample->receivedMs = QDateTime::currentMSecsSinceEpoch();
  return true;
}

} // namespace crawling
