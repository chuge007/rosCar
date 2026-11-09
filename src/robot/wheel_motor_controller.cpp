#include "wheel_motor_controller.h"

#include "robot_app_logger.h"

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
// The left drive needs a wider reply window than the right one; the connect
// probe proves it answers when the bus is not switched to the other motor
// immediately after the query.
constexpr int kExtendedAckWaitMs = 40;
constexpr int kStaleMissThreshold = 3;
constexpr int kFallbackFeedbackQueryIntervalMs = 200;
constexpr int kPositionQueryIntervalMs = 1000;

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

// Human-readable CAN frame trace for the COMM.WHEEL_CAN channel. Every TX/RX
// frame is recorded so "we sent it but the motor never answered" is directly
// observable instead of inferred from feedback ages.
QString canFrameTrace(const char* direction, const CanFrame& frame) {
  QString data;
  data.reserve(16);
  for (const std::uint8_t byte : frame.data) {
    data += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
  }
  QString text = QStringLiteral("%1 id=0x%2 data=%3")
                     .arg(QLatin1String(direction))
                     .arg(frame.id, 3, 16, QLatin1Char('0'))
                     .arg(data);
  QString op;
  switch (frame.data[0]) {
    case ServoProtocol::kSpeedClosedLoop: op = QStringLiteral("SPEED_A2"); break;
    case 0xA0: op = QStringLiteral("SPEED_OPEN_A0"); break;
    case 0x83: op = QStringLiteral("MODE_CLOSED_LOOP_83"); break;
    case 0x82: op = QStringLiteral("MODE_OPEN_LOOP_82"); break;
    case ServoProtocol::kMotorStop: op = QStringLiteral("STOP_81"); break;
    case ServoProtocol::kMotorRun: op = QStringLiteral("RUN_88"); break;
    case ServoProtocol::kBrakeControl: op = QStringLiteral("BRAKE_8C"); break;
    case ServoProtocol::kStatusQuery: op = QStringLiteral("QUERY_STATUS_9A"); break;
    case ServoProtocol::kSpeedFeedbackQuery: op = QStringLiteral("QUERY_SPEED_9C"); break;
    case 0x35: op = QStringLiteral("QUERY_35"); break;
    case 0x30: op = QStringLiteral("QUERY_FW_30"); break;
    case ServoProtocol::kSystemReset: op = QStringLiteral("RESET_80"); break;
    case 0xF3: op = QStringLiteral("SPEED_V4_F3"); break;
    default: break;
  }
  if (!op.isEmpty()) text += QStringLiteral(" op=%1").arg(op);
  if (frame.data[0] == ServoProtocol::kSpeedClosedLoop) {
    const std::int32_t raw = static_cast<std::int32_t>(frame.data[1]) |
                             (static_cast<std::int32_t>(frame.data[2]) << 8) |
                             (static_cast<std::int32_t>(frame.data[3]) << 16) |
                             (static_cast<std::int32_t>(frame.data[4]) << 24);
    text += QStringLiteral(" speed_dps=%1 acc=%2 sync=%3")
                .arg(raw / 100.0, 0, 'f', 2)
                .arg(int(frame.data[5]))
                .arg(int(frame.data[6]));
  }
  return text;
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
  canLinkRxTotal_ = 0;
  canLinkRxLeft_ = 0;
  canLinkRxRight_ = 0;
  canLinkRxOther_ = 0;
  canLinkBel_ = 0;
  canLinkTx_ = 0;
  canLinkTxFailed_ = 0;
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
    probeCanBusIds();
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
  feedbackClock_.start();
  lastFeedbackQueryMs_ = -1;
  lastPositionQueryMs_ = -1;
  nextFeedbackSide_ = 0;
  nextPositionSide_ = 0;
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
  feedbackClock_.invalidate();
  lastFeedbackQueryMs_ = -1;
  lastPositionQueryMs_ = -1;
  nextFeedbackSide_ = 0;
  nextPositionSide_ = 0;
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
    // Both drives receive the identical ServoProtocol::speedCommand encoding,
    // exactly as in the integrated build. Per-side reserved bytes, a legacy
    // 0xF3 fallback and extra enable/brake frames made one wheel obey a
    // different command set than the other, which is what showed up as a
    // permanent left turn.
    const CanFrame speedFrame =
        ServoProtocol::speedCommand(motorId, encodedSpeedDps);
    if (!*running) {
      // Log the real wire payload once per run: the intended dps in the control
      // log is not enough, an encoding bug silently shipped a different speed.
      QString hex;
      for (std::uint8_t byte : speedFrame.data)
        hex += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
      crawling::AppLogger::write(
          QStringLiteral("DRIVE.CAN"),
          QStringLiteral("event=can_speed_frame motor_id=%1 intended_dps=%2 "
                         "frame_type=0x%3 frame_id=0x%4 data=%5")
              .arg(int(motorId))
              .arg(encodedSpeedDps, 0, 'f', 2)
              .arg(speedFrame.data[0], 2, 16, QLatin1Char('0'))
              .arg(speedFrame.id, 3, 16, QLatin1Char('0'))
              .arg(hex));
    }
    sent = sendCanFrame(speedFrame);
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

  const qint64 now = feedbackClock_.isValid() ? feedbackClock_.elapsed() : 0;
  const bool queryFeedback = requestStatus || lastFeedbackQueryMs_ < 0 ||
      now - lastFeedbackQueryMs_ >= kFallbackFeedbackQueryIntervalMs;
  if (queryFeedback) {
    lastFeedbackQueryMs_ = now;
    const bool queryLeftFirst = nextFeedbackSide_ == 0;
    nextFeedbackSide_ ^= 1;
    const auto requestSide = [this, &collectResponse](bool leftMotor) {
      const bool updated = leftMotor ? leftFeedbackUpdated_ : rightFeedbackUpdated_;
      if (updated) return;
      const std::uint8_t motorId =
          leftMotor ? config_.leftMotorId : config_.rightMotorId;
      if (sendCommand(leftMotor, MwdRs485Protocol::kReadStatus2, motorId))
        collectResponse(leftMotor, true);
    };
    if (sharedPort_) {
      requestSide(true);
      requestSide(false);
    } else {
      requestSide(queryLeftFirst);
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
  if (lastPositionQueryMs_ < 0 ||
      now - lastPositionQueryMs_ >= kPositionQueryIntervalMs) {
    lastPositionQueryMs_ = now;
    const bool queryLeft = nextPositionSide_ == 0;
    nextPositionSide_ ^= 1;
    requestPosition(queryLeft, queryLeft ? config_.leftMotorId
                                         : config_.rightMotorId);
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
      ++canDiagTotal_;
      ++canLinkRxTotal_;
      if (frame.id == ServoProtocol::kFeedbackIdBase + config_.leftMotorId) {
        ++canDiagLeft_;
        ++canLinkRxLeft_;
      } else if (frame.id ==
                 ServoProtocol::kFeedbackIdBase + config_.rightMotorId) {
        ++canDiagRight_;
        ++canLinkRxRight_;
      } else {
        ++canDiagOther_;
        ++canLinkRxOther_;
      }
      if (const auto status = ServoProtocol::parseStatus(frame);
          status.has_value()) {
        if (status->motorId == config_.leftMotorId) {
          leftStatus_ = status;
        } else if (status->motorId == config_.rightMotorId) {
          rightStatus_ = status;
        }
        continue;
      }
      if (const auto parsed = ServoProtocol::parseFeedback(frame);
          parsed.has_value()) {
        if (parsed->motorId == config_.leftMotorId) {
          leftCanFeedback_ = *parsed;
          leftCanFeedbackUpdated_ = true;
          if (frame.data[0] == ServoProtocol::kSpeedFeedbackQuery) {
            std::copy(std::begin(frame.data), std::end(frame.data),
                      leftSpeedFrame_.begin());
            leftSpeedFrameValid_ = true;
          }
        } else if (parsed->motorId == config_.rightMotorId) {
          rightCanFeedback_ = *parsed;
          rightCanFeedbackUpdated_ = true;
          if (frame.data[0] == ServoProtocol::kSpeedFeedbackQuery) {
            std::copy(std::begin(frame.data), std::end(frame.data),
                      rightSpeedFrame_.begin());
            rightSpeedFrameValid_ = true;
          }
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
  const QByteArray received = canSerialPort_->readAll();
  const int belBytes = received.count('\a');
  canDiagBel_ += belBytes;
  canLinkBel_ += belBytes;
  canReceiveBuffer_.append(received);
  consume();

  const auto sendRequest = [this](std::uint8_t motorId,
                                  std::uint8_t command) {
    CanFrame frame;
    frame.id = ServoProtocol::kCommandIdBase + motorId;
    frame.data[0] = command;
    return sendCanFrame(frame);
  };
  const auto collectReply = [this, &consume](int extendedWaitMs) {
    // One query, one wait. Sending both motor queries back to back made the
    // left drive miss its reply: it answers every command when queried alone
    // (the connect-time probe waits 80 ms per command), but stayed silent under
    // the batched 15 ms poll.
    const int firstWaitMs = extendedWaitMs > 0 ? extendedWaitMs : kCanAckWaitMs;
    for (int attempt = 0; attempt < 2; ++attempt) {
      const int waitMs = attempt == 0 ? firstWaitMs : kInterByteWaitMs;
      if (!canSerialPort_->waitForReadyRead(waitMs)) break;
      canReceiveBuffer_.append(canSerialPort_->readAll());
      consume();
    }
  };
  const qint64 now = feedbackClock_.isValid() ? feedbackClock_.elapsed() : 0;
  const bool queryFeedback = requestStatus || lastFeedbackQueryMs_ < 0 ||
      now - lastFeedbackQueryMs_ >= kFallbackFeedbackQueryIntervalMs;
  if (queryFeedback) {
    lastFeedbackQueryMs_ = now;
    const bool leftNeeded = !leftCanFeedbackUpdated_;
    const bool rightNeeded = !rightCanFeedbackUpdated_;
    // A side that keeps missing gets a relaxed retry window, so a drive with a
    // slow reply cannot be starved forever by the other motor's traffic.
    const bool leftExtended = leftNeeded && leftMissCount_ >= kStaleMissThreshold;
    const bool rightExtended = rightNeeded && rightMissCount_ >= kStaleMissThreshold;
    if (leftNeeded &&
        sendRequest(config_.leftMotorId, ServoProtocol::kSpeedFeedbackQuery)) {
      collectReply(leftExtended ? kExtendedAckWaitMs : 0);
    }
    if (rightNeeded &&
        sendRequest(config_.rightMotorId, ServoProtocol::kSpeedFeedbackQuery)) {
      collectReply(rightExtended ? kExtendedAckWaitMs : 0);
    }
    leftMissCount_ = leftCanFeedbackUpdated_ ? 0 : leftMissCount_ + 1;
    rightMissCount_ = rightCanFeedbackUpdated_ ? 0 : rightMissCount_ + 1;
    if ((leftExtended || rightExtended) &&
        (canStaleLogMs_ < 0 || now - canStaleLogMs_ >= 5000)) {
      canStaleLogMs_ = now;
      const QString message =
          QStringLiteral("event=can_feedback_stale left_misses=%1 right_misses=%2 "
                         "extended_wait_ms=%3 left_id=%4 right_id=%5")
              .arg(leftMissCount_)
              .arg(rightMissCount_)
              .arg(kExtendedAckWaitMs)
              .arg(int(config_.leftMotorId))
              .arg(int(config_.rightMotorId));
      crawling::AppLogger::write(QStringLiteral("DRIVE.CAN"), message);
      if (diagnosticSink_) diagnosticSink_(message);
    }
  }

  const double positionScale = config_.wheelRadiusM * kPi / 18000.0 /
                               config_.motorOutputToWheelRatio;
  const double speedScale = config_.wheelRadiusM * kPi / 180.0 /
                            config_.motorOutputToWheelRatio;
  // No per-side fallback ladder: both wheels keep the same 0xA2 speed command
  // forever. The feedback below remains diagnostic only.

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
  if (canDiagLastLogMs_ < 0) {
    canDiagLastLogMs_ = now;
  } else if (now - canDiagLastLogMs_ >= 2000) {
    // Ask each drive for a status frame; brake state and error code are the
    // only way to tell "command ignored because the shaft is braked" apart
    // from "command never arrived".
    if (sendCanFrame(ServoProtocol::statusQuery(config_.leftMotorId)) &&
        canSerialPort_->waitForReadyRead(kCanAckWaitMs)) {
      canReceiveBuffer_.append(canSerialPort_->readAll());
    }
    if (sendCanFrame(ServoProtocol::statusQuery(config_.rightMotorId)) &&
        canSerialPort_->waitForReadyRead(kCanAckWaitMs)) {
      canReceiveBuffer_.append(canSerialPort_->readAll());
    }
    consume();
    const QString message =
        QStringLiteral("event=can_rx_stats window_ms=%1 rx_total=%2 "
                       "rx_left_id=%3 rx_right_id=%4 rx_other=%5 bel=%6 "
                       "tx=%7 tx_failed=%8 buf=%9 port=%10 left_motor_id=%11 "
                       "right_motor_id=%12 left_9c=%13 right_9c=%14 "
                       "left_brake=%15 left_err=%16 right_brake=%17 right_err=%18")
            .arg(now - canDiagLastLogMs_)
            .arg(canDiagTotal_)
            .arg(canDiagLeft_)
            .arg(canDiagRight_)
            .arg(canDiagOther_)
            .arg(canDiagBel_)
            .arg(canDiagTx_)
            .arg(canDiagTxFailed_)
            .arg(canReceiveBuffer_.size())
            .arg(canSerialPort_ != nullptr ? canSerialPort_->portName()
                                           : QString())
            .arg(int(config_.leftMotorId))
            .arg(int(config_.rightMotorId))
            .arg(lastSpeedFrameHex(true), lastSpeedFrameHex(false))
            .arg(leftStatus_.has_value()
                     ? (leftStatus_->brakeReleased ? QStringLiteral("released")
                                                   : QStringLiteral("LOCKED"))
                     : QStringLiteral("none"))
            .arg(leftStatus_.has_value()
                     ? QStringLiteral("0x%1").arg(leftStatus_->errorState, 4, 16,
                                                  QLatin1Char('0'))
                     : QStringLiteral("none"))
            .arg(rightStatus_.has_value()
                     ? (rightStatus_->brakeReleased ? QStringLiteral("released")
                                                    : QStringLiteral("LOCKED"))
                     : QStringLiteral("none"))
            .arg(rightStatus_.has_value()
                     ? QStringLiteral("0x%1").arg(rightStatus_->errorState, 4, 16,
                                                  QLatin1Char('0'))
                     : QStringLiteral("none"));
    crawling::AppLogger::write(QStringLiteral("DRIVE.CAN"), message);
    // The same window line goes to the operator console so a flaky motor can
    // be diagnosed remotely without copying the host log file.
    if (diagnosticSink_) diagnosticSink_(message);
    canDiagTotal_ = 0;
    canDiagLeft_ = 0;
    canDiagRight_ = 0;
    canDiagOther_ = 0;
    canDiagBel_ = 0;
    canDiagTx_ = 0;
    canDiagTxFailed_ = 0;
    canDiagLastLogMs_ = now;
  }
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

void WheelMotorController::probeCanBusIds() {
  if (!canMode_ || canSerialPort_ == nullptr || !canSerialPort_->isOpen()) {
    return;
  }
  // One status query per candidate ID; log which nodes answer so a wrong
  // leftMotorId (or a silent node) is visible immediately after connect.
  QString replies;
  for (std::uint8_t id = 1; id <= 8; ++id) {
    CanFrame query;
    query.id = ServoProtocol::kCommandIdBase + id;
    query.data[0] = ServoProtocol::kStatusQuery;
    if (!sendCanFrame(query)) {
      continue;
    }
    if (canSerialPort_->waitForReadyRead(50)) {
      canReceiveBuffer_.append(canSerialPort_->readAll());
    }
    CanFrame frame;
    while (takeSlcanFrame(&canReceiveBuffer_, &frame)) {
      if (frame.id == ServoProtocol::kFeedbackIdBase + id) {
        replies += QStringLiteral(" id%1=OK").arg(int(id));
      }
    }
  }
  crawling::AppLogger::write(
      QStringLiteral("DRIVE.CAN"),
      QStringLiteral("event=can_bus_probe result=done replies:%1")
          .arg(replies.isEmpty() ? QStringLiteral("(none)") : replies));

  // The integrated build sends no mode-switch, enable, brake, firmware or
  // legacy-speed probes at connect. Those extra frames (sent separately per
  // motor, each with a blocking 80 ms wait) left the two drives in different
  // command states before the first motion command, so they are removed.
  // Only read-only bus discovery above remains, for wrong-ID diagnosis.
}

WheelMotorController::CanLinkDiagnostics
WheelMotorController::canLinkDiagnostics() const {
  CanLinkDiagnostics stats;
  stats.canMode = canMode_;
  stats.rxTotal = canLinkRxTotal_;
  stats.rxLeftId = canLinkRxLeft_;
  stats.rxRightId = canLinkRxRight_;
  stats.rxOther = canLinkRxOther_;
  stats.bel = canLinkBel_;
  stats.tx = canLinkTx_;
  stats.txFailed = canLinkTxFailed_;
  stats.receiveBufferBytes = canReceiveBuffer_.size();
  return stats;
}

void WheelMotorController::setDiagnosticSink(
    std::function<void(const QString&)> sink) {
  diagnosticSink_ = std::move(sink);
}

QString WheelMotorController::lastSpeedFrameHex(bool leftMotor) const {
  const auto& frame = leftMotor ? leftSpeedFrame_ : rightSpeedFrame_;
  const bool valid = leftMotor ? leftSpeedFrameValid_ : rightSpeedFrameValid_;
  if (!valid) return QStringLiteral("none");
  QString hex;
  for (std::uint8_t byte : frame)
    hex += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
  return hex;
}

bool WheelMotorController::sendCanFrame(const CanFrame& frame) {
  const QString canId = QStringLiteral("0x%1")
                            .arg(static_cast<quint32>(frame.id), 3, 16,
                                 QLatin1Char('0'))
                            .toUpper();
  const QString command = QStringLiteral("0x%1")
                              .arg(static_cast<int>(frame.data[0]), 2, 16,
                                   QLatin1Char('0'))
                              .toUpper();
  if (!canMode_ || canSerialPort_ == nullptr ||
      !canSerialPort_->isOpen()) {
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=FAILED reason=port_not_ready "
                       "can_id=%1 command=%2")
            .arg(canId, command));
    return false;
  }
  const QByteArray encoded = encodeSlcanFrame(frame);
  if (encoded.isEmpty()) {
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=FAILED reason=encode_failed "
                       "can_id=%1 command=%2")
            .arg(canId, command));
    return false;
  }
  ++canDiagTx_;
  ++canLinkTx_;
  const qint64 written = canSerialPort_->write(encoded);
  if (written != encoded.size()) {
    ++canDiagTxFailed_;
    ++canLinkTxFailed_;
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=FAILED reason=short_write "
                       "can_id=%1 command=%2 written=%3 expected=%4 "
                       "port_error=\"%5\"")
            .arg(canId, command)
            .arg(written)
            .arg(encoded.size())
            .arg(canSerialPort_->errorString()));
    return false;
  }
  if (!canSerialPort_->waitForBytesWritten(20)) {
    ++canDiagTxFailed_;
    ++canLinkTxFailed_;
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=FAILED reason=write_timeout "
                       "can_id=%1 command=%2 port_error=\"%3\"")
            .arg(canId, command)
            .arg(canSerialPort_->errorString()));
    return false;
  }
  AppLogger::write(
      QStringLiteral("WHEEL.CAN_TX"),
      QStringLiteral("event=tx result=SENT can_id=%1 command=%2 frame=\"%3\"")
          .arg(canId, command)
          .arg(QString::fromLatin1(encoded).replace(QChar('\r'), QStringLiteral("\\r"))));
  // Wait for each CAN reply before writing the next command. Sending the left
  // and right 0xA2 frames back-to-back can make the SLCAN adapter drop the left
  // reply, leaving the left wheel stationary while the right wheel moves.
  QByteArray response = canSerialPort_->readAll();
  if (response.isEmpty() &&
      canSerialPort_->waitForReadyRead(kCanAckWaitMs)) {
    response = canSerialPort_->readAll();
  }
  while (canSerialPort_->waitForReadyRead(kInterByteWaitMs)) {
    response += canSerialPort_->readAll();
  }
  if (response.contains('\a')) {
    ++canDiagTxFailed_;
    ++canLinkTxFailed_;
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=FAILED reason=adapter_bel "
                       "can_id=%1 command=%2 response=\"%3\"")
            .arg(canId, command)
            .arg(QString::fromLatin1(response).replace(QChar('\a'), QStringLiteral("<BEL>"))));
    return false;
  }
  if (frame.data[0] == ServoProtocol::kSpeedClosedLoop &&
      response.isEmpty()) {
    AppLogger::write(
        QStringLiteral("WHEEL.CAN_TX"),
        QStringLiteral("event=tx result=NO_REPLY can_id=%1 command=%2 "
                       "wait_ms=%3")
            .arg(canId, command)
            .arg(kCanAckWaitMs));
  }
  canReceiveBuffer_.append(response);
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
