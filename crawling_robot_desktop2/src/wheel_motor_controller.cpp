#include "wheel_motor_controller.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEncoderCountsPerRevolution = 65536.0;
constexpr int kFeedbackReplyWaitMs = 20;
constexpr int kInterByteWaitMs = 2;
constexpr int kPositionReplyWaitMs = 20;
constexpr double kHoldMaximumMotorSpeedDps = 360.0;

}  // namespace

WheelMotorController::~WheelMotorController() {
  shutdown();
}

bool WheelMotorController::initialize(const WheelMotorConfig& config,
                                      QString* errorMessage) {
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  shutdown();
  config_ = config;
  const bool samePort = config_.leftSerialPort.trimmed().compare(
                            config_.rightSerialPort.trimmed(), Qt::CaseInsensitive) == 0;
  if (config_.leftSerialPort.trimmed().isEmpty() || config_.rightSerialPort.trimmed().isEmpty() ||
      config_.leftBaudRate <= 0 || config_.rightBaudRate <= 0 ||
      config_.leftMotorId < 1 || config_.leftMotorId > 32 ||
      config_.rightMotorId < 1 || config_.rightMotorId > 32 ||
      (samePort && config_.leftBaudRate != config_.rightBaudRate) ||
      (samePort && config_.leftMotorId == config_.rightMotorId) ||
      (config_.leftDirectionSign != -1 && config_.leftDirectionSign != 1) ||
      (config_.rightDirectionSign != -1 && config_.rightDirectionSign != 1) ||
      config_.wheelRadiusM <= 0.0 || !std::isfinite(config_.wheelRadiusM) ||
      config_.motorOutputToWheelRatio <= 0.0 ||
      !std::isfinite(config_.motorOutputToWheelRatio) ||
      config_.maximumWheelSpeedMps <= 0.0 ||
      !std::isfinite(config_.maximumWheelSpeedMps) ||
      config_.maximumMotorSpeedDps <= 0.0 ||
      !std::isfinite(config_.maximumMotorSpeedDps)) {
    setError(errorMessage, QStringLiteral("Invalid MWD RS485 wheel motor configuration"));
    return false;
  }

  // Construct in the caller's thread. SynchronizedDriveController is moved
  // to its control thread after construction, so a value-member QSerialPort
  // would otherwise retain the GUI thread affinity.
  const auto openPort = [](const QString& name, int baud, std::unique_ptr<QSerialPort>* output,
                           QString* error) {
    *output = std::make_unique<QSerialPort>();
    (*output)->setPortName(name.trimmed());
    (*output)->setBaudRate(baud);
    (*output)->setDataBits(QSerialPort::Data8);
    (*output)->setParity(QSerialPort::NoParity);
    (*output)->setStopBits(QSerialPort::OneStop);
    (*output)->setFlowControl(QSerialPort::NoFlowControl);
    if (!(*output)->open(QIODevice::ReadWrite)) {
      if (error != nullptr) {
        *error = (*output)->errorString();
      }
      return false;
    }
    (*output)->clear(QSerialPort::AllDirections);
    return true;
  };
  QString portOpenError;
  sharedPort_ = samePort;
  if (!openPort(config_.leftSerialPort, config_.leftBaudRate, &leftSerialPort_, &portOpenError) ||
      (!sharedPort_ &&
       !openPort(config_.rightSerialPort, config_.rightBaudRate, &rightSerialPort_, &portOpenError))) {
    if (leftSerialPort_ != nullptr) leftSerialPort_->close();
    if (rightSerialPort_ != nullptr) rightSerialPort_->close();
    setError(errorMessage,
             QStringLiteral("Cannot open MWD RS485 wheel port: %1").arg(portOpenError));
    return false;
  }
  leftReceiveBuffer_.clear();
  rightReceiveBuffer_.clear();
  leftFeedback_ = {};
  rightFeedback_ = {};
  leftFeedbackUpdated_ = false;
  rightFeedbackUpdated_ = false;
  haveLeftEncoder_ = false;
  haveRightEncoder_ = false;
  leftEncoderPosition_ = 0;
  rightEncoderPosition_ = 0;
  if (!sendCommand(true, MwdRs485Protocol::kClearError, config_.leftMotorId) ||
      !sendCommand(false, MwdRs485Protocol::kClearError, config_.rightMotorId) ||
      !sendCommand(true, MwdRs485Protocol::kMotorRun, config_.leftMotorId) ||
      !sendCommand(false, MwdRs485Protocol::kMotorRun, config_.rightMotorId)) {
    if (leftSerialPort_ != nullptr) leftSerialPort_->close();
    if (rightSerialPort_ != nullptr) rightSerialPort_->close();
    setError(errorMessage, QStringLiteral("Failed to initialize MWD RS485 motors on %1 and %2")
                               .arg(config_.leftSerialPort, config_.rightSerialPort));
    return false;
  }
  initialized_ = true;
  leftRunning_ = true;
  rightRunning_ = true;
  lastLeftCommandDps_ = 0;
  lastRightCommandDps_ = 0;
  if (!stop()) {
    shutdown();
    setError(errorMessage,
             QStringLiteral("Failed to stop and hold MWD RS485 motors on %1 and %2")
                 .arg(config_.leftSerialPort, config_.rightSerialPort));
    return false;
  }
  return true;
}

void WheelMotorController::shutdown() {
  if (initialized_ && !isStopped()) {
    stop();
  }
  if (leftSerialPort_ != nullptr && leftSerialPort_->isOpen()) leftSerialPort_->close();
  if (rightSerialPort_ != nullptr && rightSerialPort_->isOpen()) rightSerialPort_->close();
  leftSerialPort_.reset();
  rightSerialPort_.reset();
  initialized_ = false;
  sharedPort_ = false;
  leftRunning_ = false;
  rightRunning_ = false;
  lastLeftCommandDps_ = 0;
  lastRightCommandDps_ = 0;
  lastLeftHoldAngleHundredthDegree_ = 0;
  lastRightHoldAngleHundredthDegree_ = 0;
  leftReceiveBuffer_.clear();
  rightReceiveBuffer_.clear();
  leftFeedbackUpdated_ = false;
  rightFeedbackUpdated_ = false;
  haveLeftEncoder_ = false;
  haveRightEncoder_ = false;
  leftEncoderPosition_ = 0;
  rightEncoderPosition_ = 0;
}

bool WheelMotorController::setWheelSpeeds(double leftMps, double rightMps) {
  if (!initialized_) {
    return false;
  }
  const bool motionRequested = std::abs(leftMps) > 1e-9 ||
                               std::abs(rightMps) > 1e-9;
  if (!motionRequested) {
    return stop();
  }
  const bool leftSent = sendSpeed(true, config_.leftMotorId, leftMps,
                                  config_.leftDirectionSign);
  const bool rightSent = sendSpeed(false, config_.rightMotorId, rightMps,
                                   config_.rightDirectionSign);
  return leftSent && rightSent;
}

bool WheelMotorController::sendSpeed(bool leftMotor, std::uint8_t motorId,
                                     double wheelMps, int directionSign) {
  const int speedDps = toMotorSpeedDps(wheelMps, directionSign);
  bool* running = leftMotor ? &leftRunning_ : &rightRunning_;
  int* lastCommandDps = leftMotor ? &lastLeftCommandDps_
                                  : &lastRightCommandDps_;
  *lastCommandDps = speedDps;
  const QByteArray speedFrame = MwdRs485Protocol::speedCommand(motorId, speedDps);
  const bool sent = !speedFrame.isEmpty() &&
                    sendCommand(leftMotor, MwdRs485Protocol::kSpeedClosedLoop, motorId,
                                speedFrame.mid(5, 4));
  if (sent) {
    *running = true;
  }
  return sent;
}

bool WheelMotorController::stop() {
  if (!initialized_) {
    return false;
  }
  const bool leftSent = sendCommand(true, MwdRs485Protocol::kMotorStop, config_.leftMotorId);
  const bool rightSent = sendCommand(false, MwdRs485Protocol::kMotorStop, config_.rightMotorId);
  std::int64_t leftHoldAngle = 0;
  std::int64_t rightHoldAngle = 0;
  const bool leftHeld = leftSent &&
      holdCurrentPosition(true, config_.leftMotorId, &leftHoldAngle);
  const bool rightHeld = rightSent &&
      holdCurrentPosition(false, config_.rightMotorId, &rightHoldAngle);
  if (leftHeld) {
    leftRunning_ = false;
    lastLeftCommandDps_ = 0;
    lastLeftHoldAngleHundredthDegree_ = leftHoldAngle;
  }
  if (rightHeld) {
    rightRunning_ = false;
    lastRightCommandDps_ = 0;
    lastRightHoldAngleHundredthDegree_ = rightHoldAngle;
  }
  return leftHeld && rightHeld;
}

bool WheelMotorController::reset() {
  if (!initialized_) {
    return false;
  }
  const bool leftSent = sendCommand(true, MwdRs485Protocol::kClearError, config_.leftMotorId);
  const bool rightSent = sendCommand(false, MwdRs485Protocol::kClearError, config_.rightMotorId);
  const bool stopped = stop();
  return leftSent && rightSent && stopped;
}

bool WheelMotorController::pollFeedback(WheelMotorFeedback* feedback,
                                        bool requestStatus) {
  if (!initialized_ || serialPortFor(true) == nullptr || serialPortFor(false) == nullptr ||
      !serialPortFor(true)->isOpen() || !serialPortFor(false)->isOpen() ||
      feedback == nullptr) {
    return false;
  }
  leftFeedbackUpdated_ = false;
  rightFeedbackUpdated_ = false;
  const auto updateMotorFeedback = [this](bool leftMotor,
                                          const MwdMotorFeedback& parsed) {
    MwdMotorFeedback& stored = leftMotor ? leftFeedback_ : rightFeedback_;
    bool& updated = leftMotor ? leftFeedbackUpdated_ : rightFeedbackUpdated_;
    bool& haveEncoder = leftMotor ? haveLeftEncoder_ : haveRightEncoder_;
    std::uint16_t& lastEncoder = leftMotor ? lastLeftEncoder_ : lastRightEncoder_;
    qint64& encoderPosition = leftMotor ? leftEncoderPosition_ : rightEncoderPosition_;
    stored = parsed;
    updated = true;
    if (haveEncoder) {
      encoderPosition += static_cast<std::int16_t>(
          static_cast<std::uint16_t>(parsed.encoder - lastEncoder));
    }
    lastEncoder = parsed.encoder;
    haveEncoder = true;
  };
  const auto consumeFrames = [this, &updateMotorFeedback](bool fromLeftPort) {
    MwdRs485Frame frame;
    QByteArray& receiveBuffer = receiveBufferFor(fromLeftPort);
    while (MwdRs485Protocol::takeFrame(&receiveBuffer, &frame)) {
      const auto parsed = MwdRs485Protocol::parseMotorFeedback(frame);
      if (!parsed.has_value()) {
        continue;
      }
      if (sharedPort_) {
        if (parsed->motorId == config_.leftMotorId) {
          updateMotorFeedback(true, *parsed);
        } else if (parsed->motorId == config_.rightMotorId) {
          updateMotorFeedback(false, *parsed);
        }
      } else if (fromLeftPort && parsed->motorId == config_.leftMotorId) {
        updateMotorFeedback(true, *parsed);
      } else if (!fromLeftPort && parsed->motorId == config_.rightMotorId) {
        updateMotorFeedback(false, *parsed);
      }
    }
  };
  const auto collectResponse = [this, &consumeFrames](bool leftMotor,
                                                       bool waitForReply) {
    QSerialPort* port = serialPortFor(leftMotor);
    QByteArray& receiveBuffer = receiveBufferFor(leftMotor);
    receiveBuffer.append(port->readAll());
    consumeFrames(leftMotor);
    const bool requestedMotorUpdated = leftMotor ? leftFeedbackUpdated_
                                                 : rightFeedbackUpdated_;
    if (!waitForReply || requestedMotorUpdated ||
        !port->waitForReadyRead(kFeedbackReplyWaitMs)) {
      return;
    }
    do {
      receiveBuffer.append(port->readAll());
      consumeFrames(leftMotor);
    } while (port->waitForReadyRead(kInterByteWaitMs));
  };

  // Always harvest replies already queued by the previous 0xA2 command.
  // A separate 0x9C request is only needed while no cyclic speed command is
  // being sent.
  collectResponse(true, false);
  if (!sharedPort_) {
    collectResponse(false, false);
  }

  if (requestStatus) {
    if (!leftFeedbackUpdated_ &&
        sendCommand(true, MwdRs485Protocol::kReadStatus2, config_.leftMotorId)) {
      collectResponse(true, true);
    }
    if (!rightFeedbackUpdated_ &&
        sendCommand(false, MwdRs485Protocol::kReadStatus2, config_.rightMotorId)) {
      collectResponse(false, true);
    }
  }

  const double positionScale = 2.0 * kPi * config_.wheelRadiusM /
                               kEncoderCountsPerRevolution /
                               config_.motorOutputToWheelRatio;
  const double speedScale = config_.wheelRadiusM * kPi / 180.0 /
                             config_.motorOutputToWheelRatio;
  feedback->leftUpdated = leftFeedbackUpdated_;
  feedback->rightUpdated = rightFeedbackUpdated_;
  feedback->valid = leftFeedbackUpdated_ && rightFeedbackUpdated_;
  feedback->leftEncoder = leftFeedback_.encoder;
  feedback->rightEncoder = rightFeedback_.encoder;
  feedback->leftPositionM = leftEncoderPosition_ * positionScale * config_.leftDirectionSign;
  feedback->rightPositionM = rightEncoderPosition_ * positionScale * config_.rightDirectionSign;
  feedback->leftMotorSpeedDps = leftFeedback_.speedDps * config_.leftDirectionSign;
  feedback->rightMotorSpeedDps = rightFeedback_.speedDps * config_.rightDirectionSign;
  feedback->leftMotorControlValue = leftFeedback_.controlValue;
  feedback->rightMotorControlValue = rightFeedback_.controlValue;
  feedback->leftSpeedMps = feedback->leftMotorSpeedDps * speedScale;
  feedback->rightSpeedMps = feedback->rightMotorSpeedDps * speedScale;
  feedback->leftTemperatureC = leftFeedback_.temperatureC;
  feedback->rightTemperatureC = rightFeedback_.temperatureC;
  return leftFeedbackUpdated_ || rightFeedbackUpdated_;
}

bool WheelMotorController::resolvePort(const QString& specification, QString* serialPort,
                                       QString* errorMessage) {
  if (serialPort == nullptr) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Wheel motor serial-port output pointer is null");
    }
    return false;
  }
  const QString value = specification.trimmed();
  if (value.isEmpty()) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Select an RS485 motor serial port");
    }
    return false;
  }
  *serialPort = value;
  return true;
}

bool WheelMotorController::sendCommand(bool leftMotor, std::uint8_t command,
                                       std::uint8_t motorId,
                                       const QByteArray& data) {
  const QByteArray frame = MwdRs485Protocol::command(command, motorId, data);
  QSerialPort* port = serialPortFor(leftMotor);
  QByteArray& receiveBuffer = receiveBufferFor(leftMotor);
  if (frame.isEmpty() || port == nullptr || !port->isOpen()) {
    return false;
  }
  if (port->write(frame) != frame.size()) {
    return false;
  }
  if (!port->waitForBytesWritten(10)) {
    return false;
  }
  if (port->waitForReadyRead(2)) {
    receiveBuffer.append(port->readAll());
  }
  return true;
}

bool WheelMotorController::holdCurrentPosition(
    bool leftMotor, std::uint8_t motorId,
    std::int64_t* holdAngleHundredthDegree) {
  if (holdAngleHundredthDegree == nullptr) {
    return false;
  }

  QSerialPort* port = serialPortFor(leftMotor);
  QByteArray& receiveBuffer = receiveBufferFor(leftMotor);
  if (port == nullptr || !port->isOpen()) {
    return false;
  }
  // Discard stale replies before asking for the hold target. In particular,
  // an old 0x92 response must never be reused after the wheel has moved.
  receiveBuffer.clear();
  port->readAll();
  if (MwdRs485Protocol::multiTurnAngleQuery(motorId).isEmpty() ||
      !sendCommand(leftMotor, MwdRs485Protocol::kReadMultiTurnAngle, motorId)) {
    return false;
  }
  const auto readAngle = [&receiveBuffer, motorId]()
      -> std::optional<std::int64_t> {
    MwdRs485Frame response;
    while (MwdRs485Protocol::takeFrame(&receiveBuffer, &response)) {
      const auto angle = MwdRs485Protocol::parseMultiTurnAngle(response);
      if (response.motorId == motorId && angle.has_value()) {
        return angle;
      }
    }
    return std::nullopt;
  };
  const auto sendHold = [this, leftMotor, motorId,
                         holdAngleHundredthDegree, port,
                         &receiveBuffer](std::int64_t angle) {
    const QByteArray hold = MwdRs485Protocol::multiTurnPositionCommand(
        motorId, angle, kHoldMaximumMotorSpeedDps);
    if (hold.isEmpty() ||
        !sendCommand(leftMotor,
                     MwdRs485Protocol::kMultiTurnPositionClosedLoop,
                     motorId, hold.mid(5, 12))) {
      return false;
    }
    const auto holdConfirmed = [&receiveBuffer, motorId] {
      MwdRs485Frame response;
      while (MwdRs485Protocol::takeFrame(&receiveBuffer, &response)) {
        if (response.motorId == motorId &&
            response.command ==
                MwdRs485Protocol::kMultiTurnPositionClosedLoop &&
            MwdRs485Protocol::parseMotorFeedback(response).has_value()) {
          return true;
        }
      }
      return false;
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (holdConfirmed()) {
        *holdAngleHundredthDegree = angle;
        return true;
      }
      if (!port->waitForReadyRead(kPositionReplyWaitMs)) {
        continue;
      }
      receiveBuffer.append(port->readAll());
    }
    if (!holdConfirmed()) {
      return false;
    }
    *holdAngleHundredthDegree = angle;
    return true;
  };

  for (int attempt = 0; attempt < 3; ++attempt) {
    if (const auto angle = readAngle(); angle.has_value()) {
      return sendHold(angle.value());
    }
    if (!port->waitForReadyRead(kPositionReplyWaitMs)) {
      continue;
    }
    receiveBuffer.append(port->readAll());
  }
  if (const auto angle = readAngle(); angle.has_value()) {
    return sendHold(angle.value());
  }
  return false;
}

QSerialPort* WheelMotorController::serialPortFor(bool leftMotor) const {
  if (leftMotor || sharedPort_) {
    return leftSerialPort_.get();
  }
  return rightSerialPort_.get();
}

QByteArray& WheelMotorController::receiveBufferFor(bool leftMotor) {
  if (leftMotor || sharedPort_) {
    return leftReceiveBuffer_;
  }
  return rightReceiveBuffer_;
}

double WheelMotorController::lastLeftCommandMps() const {
  return toWheelSpeedMps(lastLeftCommandDps_, config_.leftDirectionSign);
}

double WheelMotorController::lastRightCommandMps() const {
  return toWheelSpeedMps(lastRightCommandDps_, config_.rightDirectionSign);
}

double WheelMotorController::maximumCommandableWheelSpeedMps() const {
  const double motorLimitMps = config_.maximumMotorSpeedDps *
      config_.wheelRadiusM * kPi / 180.0 / config_.motorOutputToWheelRatio;
  return std::min(config_.maximumWheelSpeedMps, motorLimitMps);
}

int WheelMotorController::toMotorSpeedDps(double wheelMps, int directionSign) const {
  const double bounded = std::clamp(std::isfinite(wheelMps) ? wheelMps : 0.0,
                                   -config_.maximumWheelSpeedMps,
                                   config_.maximumWheelSpeedMps);
  const double dps = bounded / config_.wheelRadiusM * 180.0 / kPi *
                     config_.motorOutputToWheelRatio * directionSign;
  return static_cast<int>(std::clamp(std::round(dps),
                                     -config_.maximumMotorSpeedDps,
                                     config_.maximumMotorSpeedDps));
}

double WheelMotorController::toWheelSpeedMps(int motorSpeedDps,
                                             int directionSign) const {
  return motorSpeedDps * config_.wheelRadiusM * kPi / 180.0 /
         config_.motorOutputToWheelRatio * directionSign;
}

void WheelMotorController::setError(QString* errorMessage, const QString& text) const {
  if (errorMessage != nullptr) {
    *errorMessage = text;
  }
}

}  // namespace crawling
