#include "camera_frame_processor.h"

#include <QMetaType>
#include <QSize>

namespace crawling {

CameraFrameProcessorWorker::CameraFrameProcessorWorker(QObject* parent)
    : QObject(parent) {}

CameraFrameProcessorWorker::~CameraFrameProcessorWorker() = default;

void CameraFrameProcessorWorker::processFrame(const FrameRequest& request) {
  if (!request.frame.isValid()) {
    // Release the controller in-flight slot even when the frame is invalid.
    emit frameProcessed(request.slot, QImage());
    return;
  }

  QImage image = request.frame.toImage();
  if (image.isNull()) {
    // Tell the controller to release its in-flight slot; otherwise a single
    // failed conversion would freeze that camera permanently.
    emit frameProcessed(request.slot, QImage());
    return;
  }

  const QSize target(request.targetWidth, request.targetHeight);
  if (target.width() > 0 && target.height() > 0 &&
      (image.width() > target.width() || image.height() > target.height())) {
    image = image.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
  }

  if (request.flipHorizontal || request.flipVertical) {
    image = image.mirrored(request.flipHorizontal, request.flipVertical);
  }

  emit frameProcessed(request.slot, image);
}

void CameraFrameProcessorWorker::stop() {
  QMutexLocker locker(&mutex_);
  stopping_ = true;
  condition_.wakeAll();
}

CameraFrameProcessor::CameraFrameProcessor(QObject* parent)
    : QObject(parent) {
  qRegisterMetaType<CameraFrameProcessorWorker::FrameRequest>(
      "crawling::CameraFrameProcessorWorker::FrameRequest");

  workerThread_ = new QThread(this);
  worker_ = new CameraFrameProcessorWorker();
  worker_->moveToThread(workerThread_);

  connect(this, &CameraFrameProcessor::processFrameSignal,
          worker_, &CameraFrameProcessorWorker::processFrame);
  connect(worker_, &CameraFrameProcessorWorker::frameProcessed,
          this, &CameraFrameProcessor::frameReady);
  connect(workerThread_, &QThread::finished,
          worker_, &QObject::deleteLater);

  workerThread_->start();
}

CameraFrameProcessor::~CameraFrameProcessor() {
  shutdown();
}

void CameraFrameProcessor::submitFrame(const QVideoFrame& frame, int slot,
                                       int targetWidth, int targetHeight,
                                       bool flipHorizontal, bool flipVertical) {
  CameraFrameProcessorWorker::FrameRequest request;
  request.frame = frame;
  request.slot = slot;
  request.targetWidth = targetWidth;
  request.targetHeight = targetHeight;
  request.flipHorizontal = flipHorizontal;
  request.flipVertical = flipVertical;

  emit processFrameSignal(request);
}

void CameraFrameProcessor::shutdown() {
  if (workerThread_ && workerThread_->isRunning()) {
    if (worker_)
      worker_->stop();
    workerThread_->quit();
    workerThread_->wait();
  }
}

}  // namespace crawling
