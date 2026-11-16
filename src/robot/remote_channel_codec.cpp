#include "remote_channel_codec.h"

#include <QDateTime>

namespace crawling {

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
  const QByteArray header = profile_.frame.header;
  if (header.isEmpty()) return false;

  // 丢掉帧头之前的杂字节。串口刚打开时可能读到半帧或设备回显，不认为出错。
  const int headerIndex = buffer->indexOf(header);
  if (headerIndex < 0) {
    if (buffer->size() > 4096) buffer->clear();
    return false;
  }
  if (headerIndex > 0) buffer->remove(0, headerIndex);

  const int lengthOffset = profile_.frame.lengthOffset;
  const int prefix = lengthOffset + 2;
  if (buffer->size() < prefix) return false;

  const int payloadLength =
      static_cast<unsigned char>(buffer->at(lengthOffset)) |
      (static_cast<unsigned char>(buffer->at(lengthOffset + 1)) << 8);
  const int totalLength = payloadLength + profile_.frame.lengthExtra;
  if (totalLength <= prefix || totalLength > 4096) {
    lastError_ = QStringLiteral("帧长字段非法：%1").arg(payloadLength);
    ++parseErrorCount_;
    buffer->remove(0, 1);
    return false;
  }
  if (buffer->size() < totalLength) return false;

  const QByteArray frame = buffer->left(totalLength);
  if (static_cast<unsigned char>(frame.at(totalLength - 1)) !=
      profile_.frame.trailer) {
    lastError_ = QStringLiteral("帧尾校验失败：0x%1")
                     .arg(static_cast<unsigned char>(frame.at(totalLength - 1)), 2, 16,
                          QLatin1Char('0'));
    ++parseErrorCount_;
    buffer->remove(0, 1);
    return false;
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
  const int lastDataIndex = frame.size() - 1; // 尾字节位置
  for (const auto &channel : profile_.channels) {
    if (channel.offset < 0 || channel.offset + 1 >= lastDataIndex) continue;
    const int value =
        static_cast<unsigned char>(frame.at(channel.offset)) |
        (static_cast<unsigned char>(frame.at(channel.offset + 1)) << 8);
    sample->values.insert(channel.key, value);
  }
  for (const auto &button : profile_.buttons) {
    if (button.offset < 0 || button.offset >= lastDataIndex) continue;
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
