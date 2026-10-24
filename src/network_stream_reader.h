#pragma once

#include <QAtomicInt>
#include <QImage>
#include <QString>
#include <QThread>

namespace crawling {

// Low-latency network camera reader (RTSP/HTTP) based on OpenCV + FFmpeg.
//
// QMediaPlayer buffers live streams and syncs them to a playback clock, which
// adds seconds of delay. This reader keeps the decoder drained on its own
// thread and only converts/emits the newest frame when the consumer has
// acknowledged the previous one, so latency never accumulates.
class NetworkStreamReader final : public QThread {
  Q_OBJECT

 public:
  explicit NetworkStreamReader(const QString& url, int slot, QObject* parent = nullptr);
  ~NetworkStreamReader() override;

  // Diagnostics helpers shared with the camera controller. They are static so
  // the environment snapshot can be logged before the first stream is opened.
  static QString redactStreamUrl(const QString& url);
  static QString backendDiagnostics();
  static void logEnvironmentSnapshot(const QString& trigger);

  void setTargetSize(int width, int height);
  void setFlip(bool horizontal, bool vertical);
  // Consumer acknowledges the last emitted frame; the next one may be sent.
  void releaseFrame();
  void requestStop();

 signals:
  void opened(int slot, int width, int height, double fps);
  void frameReady(int slot, const QImage& image);
  void failed(int slot, const QString& reason);

 protected:
  void run() override;

 private:
  const QString url_;
  const int slot_;
  QAtomicInt stop_{0};
  QAtomicInt busy_{0};
  QAtomicInt targetWidth_{640};
  QAtomicInt targetHeight_{480};
  QAtomicInt flipH_{0};
  QAtomicInt flipV_{0};
};

}  // namespace crawling
