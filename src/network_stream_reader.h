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
  // Step-by-step connection diagnostics (DNS, TCP, RTSP status line) emitted
  // before the decoder opens the stream, so a timeout can be attributed.
  void diagnostic(int slot, const QString& text);
  void opened(int slot, int width, int height, double fps);
  void frameReady(int slot, const QImage& image);
  // Emitted at most once per second while grab() keeps succeeding, even if no
  // frame is delivered because the consumer is still busy. The frame watchdog
  // must measure stream liveness, not consumer throughput.
  void progress(int slot);
  void failed(int slot, const QString& reason);

 protected:
  void run() override;

 private:
  // Pre-flight probe: resolve the host, open a plain TCP connection and ask the
  // camera for its RTSP description. Every stage is reported, so a 15 s FFmpeg
  // open timeout turns into "DNS failed" / "TCP refused" / "RTSP 401" / "no
  // reply" instead of an opaque error.
  void probeEndpoint(const QString& safeUrl);
  // 2 s window counters proving the strict one-frame backpressure is flowing:
  // frameReady emitted, releaseFrame called, and frames skipped while busy.
  void maybeLogReaderStats(qint64 nowMs);

  const QString url_;
  const int slot_;
  QAtomicInt stop_{0};
  QAtomicInt busy_{0};
  // releaseFrame() is called from the camera controller thread, so its counter
  // is atomic; the others are only touched by run() on this reader thread.
  QAtomicInt diagReleased_{0};
  quint64 diagEmitted_ = 0;
  quint64 diagBusySkips_ = 0;
  qint64 lastReaderLogMs_ = -100000;
  QAtomicInt targetWidth_{640};
  QAtomicInt targetHeight_{480};
  QAtomicInt flipH_{0};
  QAtomicInt flipV_{0};
};

}  // namespace crawling
