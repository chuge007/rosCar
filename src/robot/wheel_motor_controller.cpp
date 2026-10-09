#include "wheel_motor_controller.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kFeedbackReplyWaitMs = 20;
constexpr int kInterByteWaitMs = 2;
constexpr int kPositionReplyWaitMs = 20;
constexpr double kHoldMaximumMotorSpeedDps = 360.0;
constexpr int kCanAckWaitMs = 15;

QByteArray slcanBitrateCommand(int bitrate) {
  switch (bitrate) {
    case 125000: return "S4\r";
    case 250000: return "S5\r";
    case 500000: return "S6\r";
    case 800000: return "S7\r";
    case 1000000: return "S8\r";
    default: return {};
  }
}

QByteArray encodeSlcanFrame(const CanFrame& frame) {
  if (frame.id > 0x7FFU) {
    return {};
  }
  QByteArray encoded =
      QStringLiteral("t%1%2")
          .arg(frame.id, 3, 16, QLatin1Char('0'))
          .arg(8, 1, 16, QLatin1Char('0'))
          .toUpper()
          .toLatin1();
  encoded[0] = 't';
  for (const std::uint8_t byte : frame.data) {
    encoded.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'))
                       .toUpper()
                       .toLatin1());
  }
  encoded.append('\r');
  return encoded;
}

bool parseSlcanHex(const QByteArray& input, int* value) {
  bool okay = false;
  const int parsed = input.toInt(&okay, 16);
  if (okay) {
    *value = parsed;
  }
  return okay;
}

bool takeSlcanFrame(QByteArray* buffer, CanFrame* frame) {
  if (buffer == nullptr || frame == nullptr) {
    return false;
  }
  while (true) {
    const int terminator = buffer->indexOf('\r');
    if (terminator < 0) {
      return false;
    }
    const QByteArray line = buffer->left(terminator);
    buffer->remove(0, terminator + 1);
    if (line.size() != 21 || (line.at(0) != 't' && line.at(0) != 'T')) {
      continue;
    }
    int id = 0;
    int length = 0;
    if (!parseSlcanHex(line.mid(1, 3), &id) ||
        !parseSlcanHex(line.mid(4, 1), &length) || length != 8) {
      continue;
    }
    frame->id = static_cast<std::uint32_t>(id);
    bool valid = true;
    for (int index = 0; index < length; ++index) {
      int byte = 0;
      if (!parseSlcanHex(line.mid(5 + index * 2, 2), &byte)) {
        valid = false;
        break;
      }
      frame->data[static_cast<std::size_t>(index)] =
          static_cast<std::uint8_t>(byte);
    }
    if (valid) {
      return true;
    }
  }
}

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
  canMode_ = config_.communicationMode == WheelCommunicationMode::Can;
  const bool samePort = config_.leftSerialPort.trimmed().compare(
                            config_.rightSerialPort.trimmed(), Qt::CaseInsensitive) == 0;
  const bool transportInvalid =
      canMode_
          ? config_.canSerialPort.trimmed().isEmpty() ||
                config_.canSerialBaudRate <= 0 ||
                slcanBitrateCommand(config_.canBitrate).isEmpty()
          : config_.leftSerialPort.trimmed().isEmpty() ||
                config_.rightSerialPort.trimmed().isEmpty() ||
                config_.leftBaudRate <= 0 || config_.rightBaudRate <= 0 ||
                (samePort && config_.leftBaudRate != config_.rightBaudRate);
  if (transportInvalid ||
      config_.leftMotorId < 1 || config_.leftMotorId > 32 ||
      config_.rightMotorId < 1 || config_.rightMotorId > 32 ||
      ((canMode_ || samePort) && config_.leftMotorId == config_.rightMotorId) ||
      (config_.leftDirectionSign != -1 && config_.leftDirectionSign != 1) ||
      (config_.rightDirectionSign != -1 && config_.rightDirectionSign != 1) ||
      config_.wheelRadiusM <= 0.0 || !std::isfinite(config_.wheelRadiusM) ||
      config_.motorOutputToWheelRatio <= 0.0 ||
      !std::isfinite(config_.motorOutputToWheelRatio) ||
      config_.maximumWheelSpeedMps <= 0.0 ||
      !std::isfinite(config_.maximumWheelSpeedMps) ||
      config_.maximumMotorSpeedDps <= 0.0 ||
      !std::isfinite(config_.maximumMotorSpeedDps)) {
    setError(errorMessage, QStringLiteral("Invalid wheel motor configuration"));
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
  sharedPort_ = !canMode_ && samePort;
  if (canMode_) {
    if (!openPort(config_.canSerialPort, config_.canSerialBaudRate,
                  &canSerialPort_, &portOpenError)) {
      setError(errorMessage,
               QStringLiteral("Cannot open wheel CAN SLCAN port: %1")
                   .arg(portOpenError));
      return false;
    }
    const auto writeControl = [this](const QByteArray& command) {
      if (canSerialPort_->write(command) != command.size() ||
          !canSerialPort_->waitForBytesWritten(100)) {
        return false;
      }
      QByteArray reply;
      if (canSerialPort_->waitForReadyRead(100)) {
        reply = canSerialPort_->readAll();
      }
      return !reply.contains('\a');
    };
    if (!writeControl("C\r") ||
        !writeControl(slcanBitrateCommand(config_.canBitrate)) ||
        !writeControl("M0\r") || !writeControl("A0\r") ||
        !writeControl("O\r")) {
      canSerialPort_->close();
      canSerialPort_.reset();
      setError(errorMessage,
               QStringLiteral("Failed to initialize the wheel CAN SLCAN adapter"));
      return false;
    }
    canSerialPort_->clear(QSerialPort::AllDirections);
    canReceiveBuffer_.clear();
  } else if (!openPort(config_.leftSerialPort, config_.leftBaudRate,
                       &leftSerialPort_, &portOpenError) ||
             (!sharedPort_ &&
              !openPort(config_.rightSerialPort, config_.rightBaudRate,
                        &rightSerialPort_, &portOpenError))) {
    if (leftSerialPort_ != nullptr) leftSerialPort_->close();
    if (rightSerialPort_ != nullptr) rightSerialPort_->close();
    setError(errorMessage,
             QStringLiteral("Cannot open MWD RS485 wheel port: %1")
                 .arg(portOpenError));
    return false;
  }
  leftReceiveBuffer_.clear();
  rightReceiveBuffer_.clear();
  leftFeedback_ = {};
  rightFeedback_ = {};
  leftFeedbackUpdated_ = false;
  rightFeedbackUpdated_ = false;
  leftCanFeedbackUpdated_ = false;
  rightCanFeedbackUpdated_ = false;
  leftEncoderValue_ = 0;
  rightEncoderValue_ = 0;
  leftAngleHundredthDegree_ = 0;
  rightAngleHundredthDegree_ = 0;
  initialized_ = true;
  leftRunning_ = true;
  rightRunning_ = true;
  lastLeftCommandDps_ = 0;
  lastRightCommandDps_ = 0;
  if (canMode_) {
    leftRunning_ = false;
    rightRunning_ = false;
    if (!stop()) {
      shutdown();
      setError(errorMessage, QStringLiteral("Failed to stop CAN wheel motors"));
      return false;
    }
  } else if (!stop()) {
    shutdown();
    setError(errorMessage,
             QStringLiteral("Failed to stop and hold MWD RS485 motors on %1 and %2")
                 .arg(config_.leftSerialPort, config_.rightSerialPort));
    return false;
  }
  return true;
}

void WheelMotorController::shutdown() {
  if (initialized_ && !isStopped() && !canMode_) {
    stop();
  }
  if (canSerialPort_ != nullptr && canSerialPort_->isOpen()) {
    canSerialPort_->write("C\r");
    canSerialPort_->waitForBytesWritten(50);
    canSerialPort_->close();
  }
  canSerialPort_.reset();
  if (leftSerialPort_ != nullptr && leftSerialPort_->isOpen()) leftSerialPort_->close();
  if (rightSerialPort_ != nullptr && rightSerialPort_->isOpen()) rightSerialPort_->close();
  leftSerialPort_.reset();
  rightSerialPort_.reset();
  initialized_ = false;
  sharedPort_ = false;
  canMode_ = config_.communicationMode == WheelCommunicationMode::Can;
  leftRunning_ = false;
  rightRunning_ = false;
  lastLeftCommandDps_ = 0;
  lastRightCommandDps_ = 0;
  lastLeftHoldAngleHundredthDegree_ = 0;
  lastRightHoldAngleHundredthDegree_ = 0;
  leftReceiveBuffer_.clear();
  rightReceiveBuffer_.clear();
  canReceiveBuffer_.clear();
  leftFeedbackUpdated_ = false;
  rightFeedbackUpdated_ = false;
  leftCanFeedbackUpdated_ = false;
  rightCanFeedbackUpdated_ = false;
  leftEncoderValue_ = 0;
  rightEncoderValue_ = 0;
  leftAngleHundredthDegree_ = 0;
  rightAngleHundredthDegree_ = 0;
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
  const double speedDps = toMotorSpeedDps(wheelMps, directionSign);
  bool* running = leftMotor ? &leftRunning_ : &rightRunning_;
  double* lastCommandDps = leftMotor ? &lastLeftCommandDps_
                                    : &lastRightCommandDps_;
  const double encodedSpeedDps = canMode_ ? speedDps : std::round(speedDps);
  bool sent = false;
  if (canMode_) {
    sent = sendCanFrame(ServoProtocol::speedCommand(motorId, encodedSpeedDps));
  } else {
    const QByteArray speedFrame = MwdRs485Protocol::speedCommand(
        motorId, static_cast<int>(encodedSpeedDps));
    sent = !speedFrame.isEmpty() &&
           sendCommand(leftMotor, MwdRs485Protocol::kSpeedClosedLoop, motorId,
                       speedFrame.mid(4, 7));
  }
  if (sent) {
    *running = true;
    *lastCommandDps = encodedSpeedDps;
  }
  return sent;
}

bool WheelMotorController::stop() {
  if (!initialized_) {
    return false;
  }
  if (canMode_) {
    const bool leftSent = sendCanFrame(
        ServoProtocol::stopCommand(config_.leftMotorId));
    const bool rightSent = sendCanFrame(
        ServoProtocol::stopCommand(config_.rightMotorId));
    if (leftSent) {
      leftRunning_ = false;
      lastLeftCommandDps_ = 0;
    }
    if (rightSent) {
      rightRunning_ = false;
      lastRightCommandDps_ = 0;
    }
    return leftSent && rightSent;
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
  if (canMode_) {
    const bool leftSent = sendCanFrame(
        ServoProtocol::systemResetCommand(config_.leftMotorId));
    const bool rightSent = sendCanFrame(
        ServoProtocol::systemResetCommand(config_.rightMotorId));
    if (leftSent) {
      leftRunning_ = false;
      lastLeftCommandDps_ = 0;
    }
    if (rightSent) {
      rightRunning_ = false;
      lastRightCommandDps_ = 0;
    }
    return leftSent && rightSent;
  }
  const bool leftSent =
      sendCommand(true, MwdRs485Protocol::kSystemReset, config_.leftMotorId);
  const bool rightSent =
      sendCommand(false, MwdRs485Protocol::kSystemReset, config_.rightMotorId);
  if (leftSent) {
    leftRunning_ = false;
    lastLeftCommandDps_ = 0;
  }
  if (rightSent) {
    rightRunning_ = false;
    lastRightCommandDps_ = 0;
  }
  return leftSent && rightSent;
}

bool WheelMotorController::pollFeedback(WheelMotorFeedback* feedback,
                                        bool requestStatus) {
  if (canMode_) {
    return pollCanFeedback(requestStatus, feedback);
  }
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
    stored = parsed;
    updated = true;
  };
  const auto consumeFrames = [this, &updateMotorFeedback](bool fromLeftPort) {
    MwdRs485Frame frame;
    QByteArray& receiveBuffer = receiveBufferFor(fromLeftPort);
    while (MwdRs485Protocol::takeFrame(&receiveBuffer, &frame)) {
      const bool leftFrame = sharedPort_
                                 ? frame.motorId == config_.leftMotorId
                                 : fromLeftPort &&
                                       frame.motorId == config_.leftMotorId;
      const bool rightFrame = sharedPort_
                                  ? frame.motorId == config_.rightMotorId
                                  : !fromLeftPort &&
                                        frame.motorId == config_.rightMotorId;
      if (!leftFrame && !rightFrame) {
        continue;
      }

      if (const auto parsed = MwdRs485Protocol::parseMotorFeedback(frame);
          parsed.has_value()) {
        updateMotorFeedback(leftFrame, *parsed);
      }
      if (const auto encoder =
              MwdRs485Protocol::parseMultiTurnEncoderPosition(frame);
          encoder.has_value()) {
        if (leftFrame) {
          leftEncoderValue_ = encoder.value();
        } else {
          rightEncoderValue_ = encoder.value();
        }
      }
      if (const auto angle = MwdRs485Protocol::parseMultiTurnAngle(frame);
          angle.has_value()) {
        if (leftFrame) {
          leftAngleHundredthDegree_ = angle.value();
        } else {
          rightAngleHundredthDegree_ = angle.value();
        }
      }
    }
  };
  const auto collectResponse = [this, &consumeFrames](bool leftMotor,
                                                       bool waitForReply) {
    QSerialPort* port = serialPortFor(leftMotor);
    QByteArray& receiveBuffer = receiveBufferFor(leftMotor);
    receiveBuffer.append(port->readAll());
    consumeFrames(leftMotor);
    if (!waitForReply || !port->waitForReadyRead(kFeedbackReplyWaitMs)) {
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

  const auto requestPosition = [this, &collectResponse](bool leftMotor,
                                                          std::uint8_t motorId) {
    if (!sendCommand(leftMotor, MwdRs485Protocol::kReadMultiTurnAngle,
                     motorId)) {
      return false;
    }
    collectResponse(leftMotor, true);
    if (!sendCommand(leftMotor, MwdRs485Protocol::kReadMultiTurnEncoder,
                     motorId)) {
      return false;
    }
    collectResponse(leftMotor, true);
    return true;
  };
  if (leftFeedbackUpdated_) {
    requestPosition(true, config_.leftMotorId);
  }
  if (rightFeedbackUpdated_) {
    requestPosition(false, config_.rightMotorId);
  }

  const double positionScale = config_.wheelRadiusM * kPi / 18000.0 /
                               config_.motorOutputToWheelRatio;
  const double speedScale = config_.wheelRadiusM * kPi / 180.0 /
                             config_.motorOutputToWheelRatio;
  feedback->leftUpdated = leftFeedbackUpdated_;
  feedback->rightUpdated = rightFeedbackUpdated_;
  feedback->valid = leftFeedbackUpdated_ && rightFeedbackUpdated_;
  feedback->leftEncoder = leftEncoderValue_;
  feedback->rightEncoder = rightEncoderValue_;
  feedback->leftPositionM = leftAngleHundredthDegree_ * positionScale *
                            config_.leftDirectionSign;
  feedback->rightPositionM = rightAngleHundredthDegree_ * positionScale *
                             config_.rightDirectionSign;
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

bool WheelMotorController::pollCanFeedback(bool requestStatus,
                                           WheelMotorFeedback* feedback) {
  if (canSerialPort_ == nullptr || !canSerialPort_->isOpen() ||
      feedback == nullptr) {
    return false;
  }
  leftCanFeedbackUpdated_ = false;
  rightCanFeedbackUpdated_ = false;
  const auto consume = [this] {
    CanFrame frame;
    while (takeSlcanFrame(&canReceiveBuffer_, &frame)) {
      if (const auto parsed = ServoProtocol::parseFeedback(frame);
          parsed.has_value()) {
        if (parsed->motorId == config_.leftMotorId) {
          leftCanFeedback_ = *parsed;
          leftCanFeedbackUpdated_ = true;
        } else if (parsed->motorId == config_.rightMotorId) {
          rightCanFeedback_ = *parsed;
          rightCanFeedbackUpdated_ = true;
        }
      }
      if (const auto angle = ServoProtocol::parseMultiTurnAngle(frame);
          angle.has_value()) {
        if (frame.id == ServoProtocol::kFeedbackIdBase +
                            config_.leftMotorId) {
          leftAngleHundredthDegree_ = *angle;
        } else if (frame.id == ServoProtocol::kFeedbackIdBase +
                                   config_.rightMotorId) {
          rightAngleHundredthDegree_ = *angle;
        }
      }
      if (const auto encoder =
              ServoProtocol::parseMultiTurnEncoderPosition(frame);
          encoder.has_value()) {
        if (frame.id == ServoProtocol::kFeedbackIdBase +
                            config_.leftMotorId) {
          leftEncoderValue_ = *encoder;
        } else if (frame.id == ServoProtocol::kFeedbackIdBase +
                                   config_.rightMotorId) {
          rightEncoderValue_ = *encoder;
        }
      }
    }
  };
  canReceiveBuffer_.append(canSerialPort_->readAll());
  consume();

  const auto sendRequest = [this](std::uint8_t motorId,
                                  std::uint8_t command) {
    CanFrame frame;
    frame.id = ServoProtocol::kCommandIdBase + motorId;
    frame.data[0] = command;
    return sendCanFrame(frame);
  };
  const auto collectReply = [this, &consume] {
    if (canSerialPort_->waitForReadyRead(kCanAckWaitMs)) {
      canReceiveBuffer_.append(canSerialPort_->readAll());
      consume();
    }
  };
  if (requestStatus) {
    if (!leftCanFeedbackUpdated_ &&
        sendRequest(config_.leftMotorId, ServoProtocol::kSpeedFeedbackQuery)) {
      collectReply();
    }
    if (!rightCanFeedbackUpdated_ &&
        sendRequest(config_.rightMotorId, ServoProtocol::kSpeedFeedbackQuery)) {
      collectReply();
    }
  }
  if (!leftCanFeedbackUpdated_) {
    sendRequest(config_.leftMotorId, ServoProtocol::kSpeedFeedbackQuery);
    collectReply();
  }
  if (!rightCanFeedbackUpdated_) {
    sendRequest(config_.rightMotorId, ServoProtocol::kSpeedFeedbackQuery);
    collectReply();
  }

  const double positionScale = config_.wheelRadiusM * kPi / 18000.0 /
                               config_.motorOutputToWheelRatio;
  const double speedScale = config_.wheelRadiusM * kPi / 180.0 /
                            config_.motorOutputToWheelRatio;
  feedback->leftUpdated = leftCanFeedbackUpdated_;
  feedback->rightUpdated = rightCanFeedbackUpdated_;
  feedback->valid = leftCanFeedbackUpdated_ && rightCanFeedbackUpdated_;
  feedback->leftEncoder = leftEncoderValue_;
  feedback->rightEncoder = rightEncoderValue_;
  feedback->leftPositionM = leftAngleHundredthDegree_ * positionScale *
                            config_.leftDirectionSign;
  feedback->rightPositionM = rightAngleHundredthDegree_ * positionScale *
                             config_.rightDirectionSign;
  feedback->leftMotorSpeedDps =
      leftCanFeedback_.outputSpeedDps * config_.leftDirectionSign;
  feedback->rightMotorSpeedDps =
      rightCanFeedback_.outputSpeedDps * config_.rightDirectionSign;
  feedback->leftMotorControlValue = leftCanFeedback_.torqueCurrentA;
  feedback->rightMotorControlValue = rightCanFeedback_.torqueCurrentA;
  feedback->leftSpeedMps = feedback->leftMotorSpeedDps * speedScale;
  feedback->rightSpeedMps = feedback->rightMotorSpeedDps * speedScale;
  feedback->leftTemperatureC = leftCanFeedback_.temperatureC;
  feedback->rightTemperatureC = rightCanFeedback_.temperatureC;
  return leftCanFeedbackUpdated_ || rightCanFeedbackUpdated_;
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

bool WheelMotorController::sendCanFrame(const CanFrame& frame) {
  if (!canMode_ || canSerialPort_ == nullptr ||
      !canSerialPort_->isOpen()) {
    return false;
  }
  const QByteArray encoded = encodeSlcanFrame(frame);
  if (encoded.isEmpty() ||
      canSerialPort_->write(encoded) != encoded.size() ||
      !canSerialPort_->waitForBytesWritten(20)) {
    return false;
  }
  if (canSerialPort_->waitForReadyRead(kCanAckWaitMs)) {
    const QByteArray response = canSerialPort_->readAll();
    if (response.contains('\a')) {
      return false;
    }
    canReceiveBuffer_.append(response);
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
                     motorId, hold.mid(4, 7))) {
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

double WheelMotorController::toMotorSpeedDps(double wheelMps, int directionSign) const {
  const double bounded = std::clamp(std::isfinite(wheelMps) ? wheelMps : 0.0,
                                   -config_.maximumWheelSpeedMps,
                                   config_.maximumWheelSpeedMps);
  const double dps = bounded / config_.wheelRadiusM * 180.0 / kPi *
                     config_.motorOutputToWheelRatio * directionSign;
  const double limitedDps = std::clamp(dps, -config_.maximumMotorSpeedDps,
                                      config_.maximumMotorSpeedDps);
  return std::round(limitedDps * 100.0) / 100.0;
}

double WheelMotorController::toWheelSpeedMps(double motorSpeedDps,
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
