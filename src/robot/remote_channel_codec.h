#pragma once

#include "remote_platform.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace crawling {

// 一帧解析结果：通道原始值 + 按键状态。
struct RemoteChannelSample {
  QHash<QString, int> values;
  QHash<QString, bool> buttons;
  quint64 sequence = 0;
  qint64 receivedMs = 0;

  int channelValue(const QString &key, int fallback) const {
    return values.value(key, fallback);
  }
  bool buttonPressed(const QString &key) const {
    return buttons.value(key, false);
  }
};

// 完全由平台 profile 参数化的帧编解码：帧头、长度规则、尾字节、通道偏移都来自
// profile，换品牌不需要改这里。
class RemoteChannelCodec {
 public:
  explicit RemoteChannelCodec(const RemotePlatformProfile &profile);

  void setProfile(const RemotePlatformProfile &profile);
  const RemotePlatformProfile &profile() const { return profile_; }

  QByteArray requestFrame() const;
  // 从缓冲区里取一帧拼装成 sample。返回 false 表示还需要更多字节（或缓冲区里
  // 暂时没有有效帧），buffer 会被就地裁剪。
  bool takeFrame(QByteArray *buffer, RemoteChannelSample *sample);

  QString lastError() const { return lastError_; }
  quint64 parseErrorCount() const { return parseErrorCount_; }

 private:
  bool parseFrame(const QByteArray &frame, RemoteChannelSample *sample);

  RemotePlatformProfile profile_;
  QString lastError_;
  quint64 parseErrorCount_ = 0;
  quint64 sequence_ = 0;
};

} // namespace crawling
