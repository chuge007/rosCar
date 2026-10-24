// Camera Server Client Implementation
#include "camera_server_client.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QStandardPaths>

namespace crawling {

namespace {
constexpr int kSharedMemorySize = 4 * 1024 * 1024;
constexpr int kPollIntervalMs = 33;  // ~30fps polling
}

CameraServerClient::CameraServerClient(QObject* parent)
    : QObject(parent),
      ipcKeyBase_("PA1664Camera") {
  pollTimer_ = new QTimer(this);
  connect(pollTimer_, &QTimer::timeout, this, &CameraServerClient::pollSharedMemory);
}

CameraServerClient::~CameraServerClient() {
  stop();
}

bool CameraServerClient::start() {
  if (serverProcess_) {
    qWarning() << "Camera server already running";
    return true;
  }

  // Setup shared memory for each slot
  for (int i = 0; i < kUsbCameraChannelCount; ++i) {
    const QString key = QString("%1_slot%2").arg(ipcKeyBase_).arg(i);
    sharedMemory_[i] = new QSharedMemory(key, this);

    // Try to attach (server should have created it)
    if (!sharedMemory_[i]->attach(QSharedMemory::ReadOnly)) {
      qWarning() << "Failed to attach to shared memory" << key;
      // Server will create it, we'll attach later
    }
  }

  // Launch camera server process
  serverProcess_ = new QProcess(this);
  serverProcess_->setProgram(QCoreApplication::applicationDirPath() + "/PA1664CameraServer");
  serverProcess_->setArguments(QStringList() << "--ipc-key" << ipcKeyBase_);

  connect(serverProcess_, &QProcess::started,
          this, &CameraServerClient::onServerStarted);
  connect(serverProcess_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &CameraServerClient::onServerFinished);
  connect(serverProcess_, &QProcess::errorOccurred,
          this, &CameraServerClient::onServerError);
  connect(serverProcess_, &QProcess::readyReadStandardOutput,
          this, &CameraServerClient::onServerOutput);

  serverProcess_->start();

  if (!serverProcess_->waitForStarted(5000)) {
    qCritical() << "Failed to start camera server:" << serverProcess_->errorString();
    delete serverProcess_;
    serverProcess_ = nullptr;
    return false;
  }

  qInfo() << "Camera server process started, PID:" << serverProcess_->processId();

  // Start polling for frames
  pollTimer_->start(kPollIntervalMs);

  emit serverStatusChanged(true);
  return true;
}

void CameraServerClient::stop() {
  pollTimer_->stop();

  if (serverProcess_) {
    serverProcess_->terminate();
    if (!serverProcess_->waitForFinished(3000)) {
      serverProcess_->kill();
      serverProcess_->waitForFinished(1000);
    }
    delete serverProcess_;
    serverProcess_ = nullptr;
  }

  for (int i = 0; i < kUsbCameraChannelCount; ++i) {
    if (sharedMemory_[i]) {
      if (sharedMemory_[i]->isAttached()) {
        sharedMemory_[i]->detach();
      }
      delete sharedMemory_[i];
      sharedMemory_[i] = nullptr;
    }
  }

  emit serverStatusChanged(false);
}

void CameraServerClient::onServerStarted() {
  qInfo() << "Camera server started successfully";

  // Attach to shared memory now that server has created it
  QTimer::singleShot(500, this, [this]() {
    for (int i = 0; i < kUsbCameraChannelCount; ++i) {
      if (sharedMemory_[i] && !sharedMemory_[i]->isAttached()) {
        if (sharedMemory_[i]->attach(QSharedMemory::ReadOnly)) {
          qInfo() << "Attached to shared memory slot" << i;
        }
      }
    }
  });
}

void CameraServerClient::onServerFinished(int exitCode, QProcess::ExitStatus status) {
  qWarning() << "Camera server exited with code" << exitCode
             << "status" << (status == QProcess::NormalExit ? "normal" : "crash");
  emit serverStatusChanged(false);
}

void CameraServerClient::onServerError(QProcess::ProcessError error) {
  qCritical() << "Camera server error:" << error << serverProcess_->errorString();
  emit serverStatusChanged(false);
}

void CameraServerClient::onServerOutput() {
  if (!serverProcess_)
    return;
  const QByteArray output = serverProcess_->readAllStandardOutput();
  qInfo().noquote() << "[CameraServer]" << QString::fromUtf8(output).trimmed();
}

void CameraServerClient::pollSharedMemory() {
  for (int slot = 0; slot < kUsbCameraChannelCount; ++slot) {
    QSharedMemory* shm = sharedMemory_[slot];
    if (!shm || !shm->isAttached())
      continue;

    shm->lock();
    const quint32 dataSize = *reinterpret_cast<const quint32*>(shm->constData());

    // Check if a complete frame is available. The writer publishes the payload
    // before its length, so a stable nonzero size means the frame is complete.
    if (dataSize > 0 && dataSize <= kSharedMemorySize - sizeof(quint32) &&
        dataSize != lastFrameSize_[slot]) {
      lastFrameSize_[slot] = dataSize;

      // Read JPEG data
      const char* dataPtr = static_cast<const char*>(shm->constData()) + sizeof(quint32);
      QByteArray imageData(dataPtr, static_cast<int>(dataSize));
      shm->unlock();

      QImage image;
      if (image.loadFromData(imageData, "JPG")) {
        emit frameReady(slot, image);
      }
    } else {
      shm->unlock();
    }
  }
}

void CameraServerClient::sendCommand(const QString& command) {
  if (!serverProcess_ || serverProcess_->state() != QProcess::Running) {
    qWarning() << "Cannot send command, server not running";
    return;
  }
  // For now, use stdout/stdin pipe (could upgrade to QLocalSocket later)
  serverProcess_->write((command + "\n").toUtf8());
}

void CameraServerClient::connectUsbCamera(int slot, int deviceIndex, int fps,
                                         bool flipH, bool flipV) {
  sendCommand(QString("CONNECT_USB %1 %2 %3 %4 %5")
                  .arg(slot).arg(deviceIndex).arg(fps)
                  .arg(flipH ? 1 : 0).arg(flipV ? 1 : 0));
}

void CameraServerClient::connectNetworkCamera(int slot, const QString& url,
                                              bool flipH, bool flipV) {
  sendCommand(QString("CONNECT_NETWORK %1 %2 %3 %4")
                  .arg(slot).arg(url)
                  .arg(flipH ? 1 : 0).arg(flipV ? 1 : 0));
}

void CameraServerClient::disconnectCamera(int slot) {
  sendCommand(QString("DISCONNECT %1").arg(slot));
}

void CameraServerClient::setDisplaySize(int slot, int width, int height) {
  sendCommand(QString("SET_SIZE %1 %2 %3").arg(slot).arg(width).arg(height));
}

}  // namespace crawling
