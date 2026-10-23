#pragma once

#include <QImage>
#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QThread>
#include <QVideoFrame>
#include <QWaitCondition>

namespace crawling {

// Worker thread for processing camera frames asynchronously
class CameraFrameProcessorWorker final : public QObject {
  Q_OBJECT

 public:
  struct FrameRequest {
    QVideoFrame frame;
    int slot;
    int targetWidth;
    int targetHeight;
    bool flipHorizontal;
    bool flipVertical;
  };

  explicit CameraFrameProcessorWorker(QObject* parent = nullptr);
  ~CameraFrameProcessorWorker() override;

 public slots:
  void processFrame(const FrameRequest& request);
  void stop();

 signals:
  void frameProcessed(int slot, const QImage& image);

 private:
  void run();

  QQueue<FrameRequest> queue_;
  QMutex mutex_;
  QWaitCondition condition_;
  bool stopping_ = false;
};

// Controller for managing frame processing thread
class CameraFrameProcessor final : public QObject {
  Q_OBJECT

 public:
  explicit CameraFrameProcessor(QObject* parent = nullptr);
  ~CameraFrameProcessor() override;

  void submitFrame(const QVideoFrame& frame, int slot, int targetWidth, int targetHeight,
                   bool flipHorizontal, bool flipVertical);
  void shutdown();

 signals:
  void frameReady(int slot, const QImage& image);
  void processFrameSignal(const CameraFrameProcessorWorker::FrameRequest& request);

 private:
  QThread* workerThread_ = nullptr;
  CameraFrameProcessorWorker* worker_ = nullptr;
};

}  // namespace crawling
