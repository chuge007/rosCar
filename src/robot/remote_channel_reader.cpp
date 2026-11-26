#include "remote_channel_reader.h"

#include <QSerialPort>
#include <QTimer>

namespace crawling {

RemoteChannelReader::RemoteChannelReader(QObject *parent) : QObject(parent) {
  pollTimer_ = new QTimer(this);
  connect(pollTimer_, &QTimer::timeout, this, &RemoteChannelReader::poll);

  staleTimer_ = new QTimer(this);
  staleTimer_->setInterval(50);
  connect(staleTimer_, &QTimer::timeout, this, &RemoteChannelReader::checkStale);
}

RemoteChannelReader::~RemoteChannelReader() { close(); }

const RemotePlatformProfile &RemoteChannelReader::profile() const {
  static const RemotePlatformProfile empty;
  return codec_ ? codec_->profile() : empty;
}

bool RemoteChannelReader::open(const RemotePlatformProfile &profile,
                               const QString &portName, QString *error) {
  close();
  if (portName.trimmed().isEmpty()) {
    lastError_ = QStringLiteral("未选择串口");
    if (error) *error = lastError_;
    return false;
  }

  codec_ = std::make_unique<RemoteChannelCodec>(profile);
  serial_ = new QSerialPort(this);
  serial_->setPortName(portName);
  serial_->setBaudRate(profile.baudRate);
  serial_->setDataBits(QSerialPort::Data8);
  serial_->setParity(QSerialPort::NoParity);
  serial_->setStopBits(QSerialPort::OneStop);
  serial_->setFlowControl(QSerialPort::NoFlowControl);
  connect(serial_, &QSerialPort::readyRead, this, &RemoteChannelReader::onReadyRead);
  connect(serial_, &QSerialPort::errorOccurred, this,
          [this](QSerialPort::SerialPortError error) {
            onSerialError(static_cast<int>(error));
          });

  if (!serial_->open(QIODevice::ReadWrite)) {
    lastError_ = QStringLiteral("打开 %1 失败：%2")
                     .arg(portName, serial_->errorString());
    if (error) *error = lastError_;
    emit linkStateChanged(false, lastError_);
    serial_->deleteLater();
    serial_ = nullptr;
    return false;
  }

  portName_ = portName;
  buffer_.clear();
  frameCount_ = 0;
  connected_ = false;
  staleClock_.restart();
  pollTimer_->start(qBound(5, profile.frame.requestIntervalMs, 1000));
  staleTimer_->start();
  emit logMessage(QStringLiteral("遥控器串口 %1 已打开（%2 bps，%3 ms 轮询）")
                      .arg(portName)
                      .arg(profile.baudRate)
                      .arg(profile.frame.requestIntervalMs));
  return true;
}

void RemoteChannelReader::close() {
  if (pollTimer_) pollTimer_->stop();
  if (staleTimer_) staleTimer_->stop();
  if (serial_) {
    if (serial_->isOpen()) serial_->close();
    serial_->deleteLater();
    serial_ = nullptr;
  }
  buffer_.clear();
  codec_.reset();
  if (connected_) {
    connected_ = false;
    emit linkStateChanged(false, QStringLiteral("已断开"));
  }
  portName_.clear();
}

bool RemoteChannelReader::isOpen() const {
  return serial_ != nullptr && serial_->isOpen();
}

void RemoteChannelReader::poll() {
  if (!isOpen() || !codec_) return;
  const QByteArray request = codec_->requestFrame();
  if (!request.isEmpty()) serial_->write(request);
}

void RemoteChannelReader::onReadyRead() {
  if (!serial_ || !codec_) return;
  buffer_.append(serial_->readAll());

  RemoteChannelSample sample;
  bool parsed = false;
  while (codec_->takeFrame(&buffer_, &sample)) {
    parsed = true;
    ++frameCount_;
  }
  if (parsed) {
    markAlive();
    emit sampleReceived(sample);
  }
}

void RemoteChannelReader::checkStale() {
  if (!isOpen() || !connected_) return;
  if (staleClock_.elapsed() <= staleTimeoutMs_) return;
  markStale(QStringLiteral("超过 %1 ms 未收到有效帧").arg(staleTimeoutMs_));
}

void RemoteChannelReader::markAlive() {
  staleClock_.restart();
  if (connected_) return;
  connected_ = true;
  emit linkStateChanged(true, QStringLiteral("已连接"));
}

void RemoteChannelReader::markStale(const QString &reason) {
  if (!connected_) return;
  connected_ = false;
  lastError_ = reason;
  emit linkStateChanged(false, reason);
}

void RemoteChannelReader::onSerialError(int error) {
  const auto code = static_cast<QSerialPort::SerialPortError>(error);
  if (code == QSerialPort::NoError || code == QSerialPort::TimeoutError) return;
  lastError_ = QStringLiteral("串口错误（%1）：%2")
                   .arg(error)
                   .arg(serial_ ? serial_->errorString() : QString());
  emit logMessage(lastError_);
  if (code == QSerialPort::ResourceError || code == QSerialPort::DeviceNotFoundError ||
      code == QSerialPort::PermissionError) {
    if (pollTimer_) pollTimer_->stop();
    if (staleTimer_) staleTimer_->stop();
    if (serial_ && serial_->isOpen()) serial_->close();
    buffer_.clear();
    markStale(lastError_);
  }
}

} // namespace crawling
