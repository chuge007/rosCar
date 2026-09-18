#include "synchronized_drive_controller.h"

#include "servo_protocol.h"
#include "utf8_compat.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
// Give the motor controller time to process the zero-speed command before
// applying its holding brake. Emergency and fault paths remain immediate.
constexpr int kBrakeSettleMs = 100;
// Hardware motion is intentionally disabled while the desktop integration is being debugged.
constexpr bool kMotorOutputEnabled = true;

double approachVectorComponent(double current, double target, double scale) {
  return current + (target - current) * scale;
}

}  // namespace

SynchronizedDriveController::SynchronizedDriveController(QObject* parent)
    : QObject(parent), transport_(this), controlTimer_(this), brakeTimer_(this) {
  controlTimer_.setInterval(20);
  controlTimer_.setTimerType(Qt::PreciseTimer);
  connect(&controlTimer_, &QTimer::timeout, this, &SynchronizedDriveController::controlTick);
  brakeTimer_.setSingleShot(true);
  brakeTimer_.setTimerType(Qt::PreciseTimer);
  connect(&brakeTimer_, &QTimer::timeout, this, &SynchronizedDriveController::applyDelayedBrake);
  connect(&transport_, &SlcanTransport::frameReceived, this,
          &SynchronizedDriveController::onCanFrame);
  connect(&transport_, &SlcanTransport::connectionChanged, this,
          &SynchronizedDriveController::onTransportChanged);
  connect(&transport_, &SlcanTransport::transportError, this,
          &SynchronizedDriveController::onTransportError);
  connect(&transport_, &SlcanTransport::activity, this,
          &SynchronizedDriveController::logMessage);
  clock_.start();
}

void SynchronizedDriveController::startControlLoop() {
  lastTickMs_ = nowMs();
  controlTimer_.start();
}

void SynchronizedDriveController::connectAdapter(const DriveSettings& settings) {
  if (state_ == DriveState::Enabled || state_ == DriveState::Arming) {
    requestEnable(false);
  }
  const QString validationError = settings.validationError();
  if (!validationError.isEmpty()) {
    enterFault(validationError);
    return;
  }
  settings_ = settings;
  resetMotionState();
  leftFeedback_ = {};
  rightFeedback_ = {};
  leftSpeedFeedbackMs_ = -1000;
  rightSpeedFeedbackMs_ = -1000;
  transport_.open(settings_);
}

void SynchronizedDriveController::autoDetectCanDevices(const QString& excludedPort) {
  if (transport_.isOpen()) {
    emit logMessage(CRAWLING_TEXT("\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\xB7\xB2""\xE5\x8F\x96""\xE6\xB6\x88""\xEF\xBC\x9A""SLCAN \xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE6\xAD\xA3""\xE5\x9C\xA8""\xE4\xBD\xBF""\xE7\x94\xA8""\xEF\xBC\x8C""\xE8\xAF\xB7""\xE5\x85\x88""\xE6\x96\xAD""\xE5\xBC\x80""\xE5\xBA\x95""\xE7\x9B\x98""\xE3\x80\x82"""));
    emit canSettingsDetected(HardwareDetectionResult{});
    return;
  }
  const HardwareDetectionResult result = HardwareDiscovery::probeCanPorts(excludedPort);
  emit canSettingsDetected(result);
  QStringList details = result.details;
  if (details.isEmpty()) {
    details << CRAWLING_TEXT("\xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0""\xE8\xBD\xAE""\xE6\xAF\x82""\xE4\xBC\xBA""\xE6\x9C\x8D""\xE6\x88\x96""\xE5\xA4\xB9""\xE5\xAD\x90"" CANopen \xE8\x8A\x82""\xE7\x82\xB9""");
  }
  emit logMessage(details.join(CRAWLING_TEXT("\xEF\xBC\x9B""")));
}

void SynchronizedDriveController::disconnectAdapter() {
  brakeTimer_.stop();
  brakePending_ = false;
  resetMotionState();
  setState(DriveState::Disconnected, CRAWLING_TEXT("\xE6\x93\x8D""\xE4\xBD\x9C""\xE5\x91\x98""\xE5\xB7\xB2""\xE6\x96\xAD""\xE5\xBC\x80""\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8"""));
  sendStopPair(true);
  transport_.close();
}

void SynchronizedDriveController::setInputCommand(double linearMps, double angularRadps) {
  if (!std::isfinite(linearMps) || !std::isfinite(angularRadps)) {
    return;
  }
  input_.linearMps = linearMps;
  input_.angularRadps = angularRadps;
  input_.receivedAtMs = nowMs();
  input_.valid = true;
}

void SynchronizedDriveController::requestEnable(bool enabled) {
  if (!enabled) {
    brakeTimer_.stop();
    brakePending_ = false;
    resetMotionState();
    setState(transport_.isOpen() ? DriveState::Idle : DriveState::Disconnected,
             CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98""\xE8\xBE\x93""\xE5\x87\xBA""\xE5\xB7\xB2""\xE7\xA6\x81""\xE7\x94\xA8"""));
    // Normal stop is deliberately ordered: first command both motors to zero
    // speed, then apply 0x81 stop/brake frames after a short settle interval.
    // The transport clears stale output only for the urgent zero-speed
    // command, so an older non-zero command cannot overtake it.
    if (transport_.isOpen()) {
      sendSpeedPair(0.0, 0.0, true);
      brakePending_ = true;
      brakeTimer_.start(kBrakeSettleMs);
    }
    return;
  }
  // A new enable request supersedes a delayed brake from an earlier stop.
  brakeTimer_.stop();
  brakePending_ = false;
  if (state_ == DriveState::EmergencyStop) {
    emit logMessage(CRAWLING_TEXT("\xE8\xAF\xB7""\xE5\x85\x88""\xE7\xA6\x81""\xE7\x94\xA8""\xE5\xBA\x95""\xE7\x9B\x98""\xEF\xBC\x8C""\xE5\x86\x8D""\xE9\x87\x8D""\xE6\x96\xB0""\xE4\xBD\xBF""\xE8\x83\xBD""\xE4\xBB\xA5""\xE8\xA7\xA3""\xE9\x99\xA4""\xE6\x80\xA5""\xE5\x81\x9C""\xE3\x80\x82"""));
    return;
  }
  const QString validationError = settings_.validationError();
  if (!validationError.isEmpty()) {
    enterFault(validationError);
    return;
  }
  if (!transport_.isOpen()) {
    enterFault(CRAWLING_TEXT("CAN \xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE6\x9C\xAA""\xE8\xBF\x9E""\xE6\x8E\xA5""\xE3\x80\x82"""));
    return;
  }
  if (!kMotorOutputEnabled) {
    setState(DriveState::Enabled, CRAWLING_TEXT("\xE8\xB0\x83""\xE8\xAF\x95""\xE6\xA8\xA1""\xE5\xBC\x8F""\xEF\xBC\x9A""\xE5\xB7\xB2""\xE8\xbf\x9B""\xE5\x85\xa5""\xE7\x94\xB5""\xE6\x9C\xBA""\xE5\x9B\x9E""\xE5\xA4\x8D""\xE7\x9B\x91""\xE5\x90\xAC"""));
    emit logMessage(CRAWLING_TEXT("\xE8\xB0\x83""\xE8\xAF\x95""\xE6\xA8\xA1""\xE5\xBC\x8F""\xEF\xBC\x9A""\xE5\x8F\xAA""\xE5\x8F\x91""\xE9\x80\x81""\xE9\x9B\xB6""\xE9\x80\x9F""\xE6\x8E\xA7""\xE5\x88\xB6""\xE5\xB8\xA7""\xEF\xBC\x8C""\xE4\xB8\x8D""\xE4\xBC\x9A""\xE8\xAE\xA9""\xE7\x94\xB5""\xE6\x9C\xBA""\xE8\xBD\xAC""\xE5\x8A\xA8"""));
    sendZeroSpeedPair();
    sendStatusQueryPair();
    return;
  }
  resetMotionState();
  // Seed a stationary command before entering Enabled. The UI refreshes its
  // manual command every 40 ms, while the control loop runs every 20 ms.
  // Without this seed, the first tick faults before the first UI update.
  input_.valid = true;
  input_.receivedAtMs = nowMs();
  // Enable is the operator's permission to move. Motor replies remain
  // visible in the diagnostic log but do not block manual control.
  setState(DriveState::Enabled, CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98""\xE5\xB7\xB2""\xE4\xBD\xBF""\xE8\x83\xBD""\xEF\xBC\x8C""\xE5\x8F\xAF""\xE6\x8E\xA7""\xE5\x88\xB6"""));
  emit logMessage(QStringLiteral("Drive enabled: manual 0xA2 speed control is permitted; feedback is diagnostic"));
  sendSpeedPair(0.0, 0.0, true);
}

void SynchronizedDriveController::emergencyStop() {
  brakeTimer_.stop();
  brakePending_ = false;
  resetMotionState();
  setState(DriveState::EmergencyStop, CRAWLING_TEXT("\xE5\xB7\xB2""\xE8\xAF\xB7""\xE6\xB1\x82""\xE7\xB4\xA7""\xE6\x80\xA5""\xE5\x81\x9C""\xE6\xAD\xA2"""));
  sendStopPair(true);
}

void SynchronizedDriveController::systemReset() {
  brakeTimer_.stop();
  brakePending_ = false;
  if (!transport_.isOpen()) {
    emit logMessage(QStringLiteral("System reset skipped: adapter is disconnected"));
    return;
  }
  resetMotionState();
  setState(DriveState::Idle, QStringLiteral("System reset command sent"));
  const bool sent = transport_.sendDrivePair(
      ServoProtocol::systemResetCommand(static_cast<std::uint8_t>(settings_.leftMotorId)),
      ServoProtocol::systemResetCommand(static_cast<std::uint8_t>(settings_.rightMotorId)), true);
  emit logMessage(QStringLiteral("TX system reset 0x80 to CAN 0x%1/0x%2 result=%3")
                      .arg(ServoProtocol::kCommandIdBase + settings_.leftMotorId, 3, 16,
                           QLatin1Char('0'))
                      .arg(ServoProtocol::kCommandIdBase + settings_.rightMotorId, 3, 16,
                           QLatin1Char('0'))
                      .arg(sent ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                      .toUpper());
}

void SynchronizedDriveController::clearAlarm() {
  brakeTimer_.stop();
  brakePending_ = false;
  if (!transport_.isOpen()) {
    emit logMessage(QStringLiteral("Clear alarm skipped: adapter is disconnected"));
    return;
  }
  const bool sent = transport_.sendDrivePair(
      ServoProtocol::stopCommand(static_cast<std::uint8_t>(settings_.leftMotorId)),
      ServoProtocol::stopCommand(static_cast<std::uint8_t>(settings_.rightMotorId)), true);
  emit logMessage(QStringLiteral("TX clear alarm using protocol stop/reset sequence result=%1")
                      .arg(sent ? QStringLiteral("OK") : QStringLiteral("FAILED")));
}

void SynchronizedDriveController::shutdown() {
  brakeTimer_.stop();
  brakePending_ = false;
  controlTimer_.stop();
  resetMotionState();
  setState(DriveState::Disconnected, CRAWLING_TEXT("\xE7\xA8\x8B""\xE5\xBA\x8F""\xE5\xB7\xB2""\xE5\x85\xB3""\xE9\x97\xAD"""));
  sendStopPair(true);
  transport_.close();
}

void SynchronizedDriveController::controlTick() {
  const qint64 now = nowMs();
  const double deltaSeconds = std::clamp((now - lastTickMs_) / 1000.0, 0.001, 0.050);
  lastTickMs_ = now;

  // Keep motor status polling active in every connected state. The 0x9A
  // response is the liveness signal used during arming and motion control.
  if (transport_.isOpen() && now - lastDiagnosticQueryMs_ >= 50) {
    sendStatusQueryPair();
    lastDiagnosticQueryMs_ = now;
  }

  if (!kMotorOutputEnabled) {
    publishTelemetry();
    return;
  }

  if (state_ == DriveState::Arming) {
    if (feedbackFresh(now)) {
      setState(DriveState::Enabled, CRAWLING_TEXT("\xE4\xB8\xA4""\xE4\xB8\xAA""\xE8\xBD\xAE""\xE5\xAD\x90""\xE5\x8F\x8D""\xE9\xA6\x88""\xE6\x95\xB0""\xE6\x8D\xAE""\xE6\xB5\x81""\xE6\xAD\xA3""\xE5\xB8\xB8"""));
    } else if (now - armingStartedMs_ > settings_.armingTimeoutMs) {
      enterFault(CRAWLING_TEXT("\xE4\xB8\xA4""\xE4\xB8\xAA""\xE9\xA9\xB1""\xE5\x8A\xA8""\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\xB2\xA1""\xE6\x9C\x89""\xE6\x94\xB6""\xE5\x88\xB0""\xE6\x96\xB0""\xE9\xB2\x9C""\xE5\x8F\x8D""\xE9\xA6\x88""\xE3\x80\x82"""));
    } else {
      sendSpeedPair(0.0, 0.0);
      publishTelemetry();
      return;
    }
  }

  if (state_ == DriveState::Enabled) {
    if (!commandFresh(now)) {
      enterFault(CRAWLING_TEXT("\xE8\xBF\x90""\xE5\x8A\xA8""\xE5\x91\xBD""\xE4\xBB\xA4""\xE7\x9C\x8B""\xE9\x97\xA8""\xE7\x8B\x97""\xE8\xB6\x85""\xE6\x97\xB6""\xE3\x80\x82"""));
      return;
    }
    const double targetLinear = std::clamp(input_.linearMps,
                                           -settings_.maximumLinearSpeedMps,
                                           settings_.maximumLinearSpeedMps);
    const double targetAngular = std::clamp(input_.angularRadps,
                                            -settings_.maximumAngularSpeedRadps,
                                            settings_.maximumAngularSpeedRadps);
    // A zero command is an explicit stop from a released direction button.
    // Clear the ramp immediately so the next short click starts from a known
    // stationary state instead of creeping through a long deceleration tail.
    if (std::abs(targetLinear) <= 1e-12 && std::abs(targetAngular) <= 1e-12) {
      appliedLinearMps_ = 0.0;
      appliedAngularRadps_ = 0.0;
      motionOutputStopped_ = true;
      synchronizer_.reset();
      sendSpeedPair(0.0, 0.0);
      publishTelemetry();
      return;
    }
    const double linearDelta = targetLinear - appliedLinearMps_;
    const double angularDelta = targetAngular - appliedAngularRadps_;
    double rampScale = 1.0;
    // The first command after an explicit stop is an operator action and must
    // reach both wheels immediately. Limit subsequent changes for smoothness.
    if (!motionOutputStopped_ && std::abs(linearDelta) > 1e-12) {
      rampScale = std::min(rampScale,
                           settings_.maximumLinearAccelerationMps2 * deltaSeconds /
                               std::abs(linearDelta));
    }
    if (!motionOutputStopped_ && std::abs(angularDelta) > 1e-12) {
      rampScale = std::min(rampScale,
                           settings_.maximumAngularAccelerationRadps2 * deltaSeconds /
                               std::abs(angularDelta));
    }
    rampScale = std::clamp(rampScale, 0.0, 1.0);
    appliedLinearMps_ = approachVectorComponent(appliedLinearMps_, targetLinear, rampScale);
    appliedAngularRadps_ = approachVectorComponent(appliedAngularRadps_, targetAngular, rampScale);

    WheelTargets output = DifferentialMixer::mix(appliedLinearMps_, appliedAngularRadps_,
                                                  settings_.trackWidthM,
                                                  settings_.minimumInnerWheelRatio);
    output = DifferentialMixer::limitUniformly(output, settings_.maximumWheelSpeedMps);
    const SynchronizerResult synchronization = synchronizer_.update(
        output.leftMps, output.rightMps, leftFeedback_.wheelSpeedMps,
        rightFeedback_.wheelSpeedMps, speedFeedbackFresh(now), deltaSeconds,
        settings_.synchronizer);
    output.leftMps = synchronization.leftMps;
    output.rightMps = synchronization.rightMps;
    output.linearMps = (output.leftMps + output.rightMps) * 0.5;
    output.angularRadps = (output.leftMps - output.rightMps) / settings_.trackWidthM;
    output = DifferentialMixer::limitUniformly(output, settings_.maximumWheelSpeedMps);
    lastSynchronizationError_ = synchronization.normalizedError;
    lastSynchronizationCorrectionMps_ = synchronization.correctionMps;
    sendSpeedPair(output.leftMps, output.rightMps);
    motionOutputStopped_ = false;
    publishTelemetry(output);
    return;
  }

  if ((state_ == DriveState::Fault || state_ == DriveState::EmergencyStop ||
       state_ == DriveState::Idle || state_ == DriveState::Disconnected) &&
      !brakePending_ && now - lastStopMs_ >= 100) {
    sendStopPair();
  }
  publishTelemetry();
}

void SynchronizedDriveController::onCanFrame(const CanFrame& frame) {
  ++feedbackFrameCount_;
  const auto response = ServoProtocol::parseFeedback(frame);
  if (!response.has_value()) {
    const auto status = ServoProtocol::parseStatus(frame);
    if (status.has_value()) {
      const qint64 now = nowMs();
      // A 0x9A status response proves the motor is alive even when the
      // firmware does not emit a separate 0xA2 feedback frame.
      if (status->motorId == settings_.leftMotorId) {
        leftFeedback_.valid = true;
        leftFeedback_.temperatureC = status->temperatureC;
        leftFeedback_.receivedAtMs = now;
      } else if (status->motorId == settings_.rightMotorId) {
        rightFeedback_.valid = true;
        rightFeedback_.temperatureC = status->temperatureC;
        rightFeedback_.receivedAtMs = now;
      }
      // Status queries are sent periodically for liveness. Logging every
      // reply can flood the GUI event queue and delay manual command input.
      if (now - lastStatusLogMs_ >= 500) {
        lastStatusLogMs_ = now;
        emit logMessage(CRAWLING_TEXT("\xE7\x8A\xB6""\xE6\x80\x81""\xE5\x9B\x9E""\xE5\xA4\x8D"" CAN=0x%1 ID=%2 \xE6\xB8\xA9""\xE5\xBA\xA6""=%3 \xE9\x94\x99""\xE8\xAF\xAF""=0x%4")
                            .arg(frame.id, 3, 16, QLatin1Char('0')).arg(status->motorId)
                            .arg(status->temperatureC).arg(status->errorState, 4, 16, QLatin1Char('0')).toUpper());
      }
    } else {
      const qint64 now = nowMs();
      if (now - lastUnknownFrameLogMs_ >= 200) {
        lastUnknownFrameLogMs_ = now;
        QByteArray payload;
        for (const std::uint8_t byte : frame.data) {
          payload.append(QString::number(byte, 16).rightJustified(2, QLatin1Char('0')).toUpper().toLatin1());
        }
        emit logMessage(QStringLiteral("CAN unmatched response: ID=0x%1 DATA=%2")
                            .arg(frame.id, 3, 16, QLatin1Char('0')).arg(QString::fromLatin1(payload)).toUpper());
      }
    }
    return;
  }
  const qint64 now = nowMs();
  const bool isLeft = response->motorId == settings_.leftMotorId;
  const bool isRight = response->motorId == settings_.rightMotorId;
  const bool logFrame = now - lastFeedbackLogMs_ >= 200 || (!isLeft && !isRight);
  if (logFrame) {
    lastFeedbackLogMs_ = now;
    emit logMessage(CRAWLING_TEXT("\xE7\x94\xB5""\xE6\x9C\xBA""\xE5\x9B\x9E""\xE5\xA4\x8D"" #%1 CAN=0x%2 ID=%3 \xE6\xB8\xA9""\xE5\xBA\xA6""=%4 C \xE7\x94\xB5""\xE6\xB5\x81""=%5 A \xE8\xBD\xAC""\xE9\x80\x9F""=%6 deg/s \xE8\xA7\x92""\xE5\xBA\xA6""=%7 deg %8")
                        .arg(feedbackFrameCount_)
                        .arg(frame.id, 3, 16, QLatin1Char('0')).arg(response->motorId)
                        .arg(response->temperatureC).arg(response->torqueCurrentA, 0, 'f', 2)
                        .arg(response->outputSpeedDps, 0, 'f', 1)
                        .arg(response->outputAngleDeg, 0, 'f', 1)
                        .arg(isLeft ? CRAWLING_TEXT("\xE5\xB7\xA6""\xE8\xBD\xAE""") :
                             isRight ? CRAWLING_TEXT("\xE5\x8F\xB3""\xE8\xBD\xAE""") :
                                       CRAWLING_TEXT("\xE6\x9C\xAA""\xE7\x9F\xA5""")));
  }
  if (response->motorId == settings_.leftMotorId) {
    leftFeedback_ = toPhysicalFeedback(*response, settings_.leftMotorSign);
    leftSpeedFeedbackMs_ = now;
    if (logFrame) {
      emit logMessage(CRAWLING_TEXT("\xE5\xB7\xA6""\xE8\xBD\xAE""\xE5\x8F\x8D""\xE9\xA6\x88"" \xE8\xBD\xAE""\xE9\x80\x9F""=%1 mm/s \xE8\xBD\xAE""\xE4\xBD\x8D""\xE7\xBD\xAE""=%2 deg")
                          .arg(leftFeedback_.wheelSpeedMps * 1000.0, 0, 'f', 1)
                          .arg(leftFeedback_.wheelPositionRad * kRadiansToDegrees, 0, 'f', 1));
    }
  } else if (response->motorId == settings_.rightMotorId) {
    rightFeedback_ = toPhysicalFeedback(*response, settings_.rightMotorSign);
    rightSpeedFeedbackMs_ = now;
    if (logFrame) {
      emit logMessage(CRAWLING_TEXT("\xE5\x8F\xB3""\xE8\xBD\xAE""\xE5\x8F\x8D""\xE9\xA6\x88"" \xE8\xBD\xAE""\xE9\x80\x9F""=%1 mm/s \xE8\xBD\xAE""\xE4\xBD\x8D""=%2 deg")
                          .arg(rightFeedback_.wheelSpeedMps * 1000.0, 0, 'f', 1)
                          .arg(rightFeedback_.wheelPositionRad * kRadiansToDegrees, 0, 'f', 1));
    }
  }
}

void SynchronizedDriveController::onTransportChanged(bool connected, const QString& message) {
  emit connectionChanged(connected, message);
  if (connected) {
    if (state_ != DriveState::EmergencyStop) {
      setState(DriveState::Idle, CRAWLING_TEXT("\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""\xEF\xBC\x8C""\xE5\xBA\x95""\xE7\x9B\x98""\xE8\xBE\x93""\xE5\x87\xBA""\xE5\xB7\xB2""\xE7\xA6\x81""\xE7\x94\xA8"""));
    }
  } else if (state_ != DriveState::Disconnected) {
    brakeTimer_.stop();
    brakePending_ = false;
    resetMotionState();
    setState(DriveState::Disconnected, message);
  }
}

void SynchronizedDriveController::onTransportError(const QString& message) {
  emit logMessage(message);
  if (state_ == DriveState::Enabled || state_ == DriveState::Arming) {
    enterFault(message);
  }
}

qint64 SynchronizedDriveController::nowMs() const {
  return clock_.elapsed();
}

bool SynchronizedDriveController::feedbackFresh(qint64 now) const {
  return leftFeedback_.valid && rightFeedback_.valid &&
         now - leftFeedback_.receivedAtMs <= settings_.feedbackTimeoutMs &&
         now - rightFeedback_.receivedAtMs <= settings_.feedbackTimeoutMs;
}

bool SynchronizedDriveController::speedFeedbackFresh(qint64 now) const {
  return now - leftSpeedFeedbackMs_ <= settings_.feedbackTimeoutMs &&
         now - rightSpeedFeedbackMs_ <= settings_.feedbackTimeoutMs;
}

bool SynchronizedDriveController::commandFresh(qint64 now) const {
  return input_.valid && now - input_.receivedAtMs <= settings_.commandTimeoutMs;
}

void SynchronizedDriveController::setState(DriveState state, const QString& reason) {
  if (state_ == state && stateReason_ == reason) {
    return;
  }
  state_ = state;
  stateReason_ = reason;
  emit stateChanged(state_, stateReason_);
  emit logMessage(CRAWLING_TEXT("%1: %2").arg(driveStateText(state_), stateReason_));
}

void SynchronizedDriveController::enterFault(const QString& reason) {
  brakeTimer_.stop();
  brakePending_ = false;
  resetMotionState();
  setState(DriveState::Fault, reason);
  sendStopPair(true);
  publishTelemetry();
}

void SynchronizedDriveController::applyDelayedBrake() {
  if (!brakePending_) {
    return;
  }
  brakePending_ = false;
  if (!transport_.isOpen() || state_ == DriveState::Enabled ||
      state_ == DriveState::Arming) {
    return;
  }
  sendStopPair(false);
  emit logMessage(QStringLiteral("Normal stop: zero-speed command settled; brake applied"));
}

void SynchronizedDriveController::sendStopPair(bool urgent) {
  if (!transport_.isOpen()) {
    return;
  }
  transport_.sendDrivePair(ServoProtocol::stopCommand(static_cast<std::uint8_t>(settings_.leftMotorId)),
                           ServoProtocol::stopCommand(static_cast<std::uint8_t>(settings_.rightMotorId)),
                           urgent);
  lastStopMs_ = nowMs();
}

void SynchronizedDriveController::sendZeroSpeedPair() {
  if (!transport_.isOpen()) {
    return;
  }
  transport_.sendDrivePair(
      ServoProtocol::speedCommand(static_cast<std::uint8_t>(settings_.leftMotorId), 0.0),
      ServoProtocol::speedCommand(static_cast<std::uint8_t>(settings_.rightMotorId), 0.0));
}

void SynchronizedDriveController::sendStatusQueryPair() {
  if (!transport_.isOpen()) {
    return;
  }
  const CanFrame leftQuery = ServoProtocol::statusQuery(
      static_cast<std::uint8_t>(settings_.leftMotorId));
  const CanFrame rightQuery = ServoProtocol::statusQuery(
      static_cast<std::uint8_t>(settings_.rightMotorId));
  transport_.sendDrivePair(leftQuery, rightQuery);
}

void SynchronizedDriveController::sendSpeedPair(double leftMps, double rightMps, bool urgent) {
  if (!kMotorOutputEnabled) {
    Q_UNUSED(leftMps);
    Q_UNUSED(rightMps);
    Q_UNUSED(urgent);
    return;
  }
  if (!transport_.isOpen()) {
    return;
  }
  const double leftDps = wheelSpeedToMotorDps(leftMps, settings_.leftMotorSign);
  const double rightDps = wheelSpeedToMotorDps(rightMps, settings_.rightMotorSign);
  const bool sent = transport_.sendDrivePair(
      ServoProtocol::speedCommand(static_cast<std::uint8_t>(settings_.leftMotorId), leftDps),
      ServoProtocol::speedCommand(static_cast<std::uint8_t>(settings_.rightMotorId), rightDps),
      urgent);
  const qint64 now = nowMs();
  if (now - lastCommandLogMs_ >= 200) {
    lastCommandLogMs_ = now;
    emit logMessage(QStringLiteral("TX speed 0xA2: left=%1 dps right=%2 dps result=%3")
                        .arg(leftDps, 0, 'f', 1)
                        .arg(rightDps, 0, 'f', 1)
                        .arg(sent ? QStringLiteral("OK") : QStringLiteral("FAILED")));
  }
}

double SynchronizedDriveController::wheelSpeedToMotorDps(double wheelSpeedMps,
                                                          int motorSign) const {
  const double bounded = std::clamp(wheelSpeedMps, -settings_.maximumWheelSpeedMps,
                                    settings_.maximumWheelSpeedMps);
  return bounded / settings_.wheelRadiusM * kRadiansToDegrees *
         settings_.motorOutputToWheelRatio * motorSign;
}

MotorFeedback SynchronizedDriveController::toPhysicalFeedback(const ServoFeedback& feedback,
                                                               int motorSign) const {
  MotorFeedback physical;
  physical.valid = true;
  physical.motorSpeedDps = feedback.outputSpeedDps;
  physical.wheelSpeedMps = feedback.outputSpeedDps / settings_.motorOutputToWheelRatio /
                           motorSign * kDegreesToRadians * settings_.wheelRadiusM;
  physical.wheelPositionRad = feedback.outputAngleDeg / settings_.motorOutputToWheelRatio /
                              motorSign * kDegreesToRadians;
  physical.torqueCurrentA = feedback.torqueCurrentA;
  physical.temperatureC = feedback.temperatureC;
  physical.receivedAtMs = nowMs();
  return physical;
}

void SynchronizedDriveController::publishTelemetry(const WheelTargets& output) {
  // The control loop remains at 50 Hz, but the widgets do not need a full
  // repaint at that rate. Limiting this diagnostic signal prevents GUI event
  // backlog from competing with manual command delivery.
  const qint64 now = nowMs();
  if (now - lastTelemetryEmitMs_ < 50) {
    return;
  }
  lastTelemetryEmitMs_ = now;
  DriveTelemetry telemetry;
  telemetry.state = state_;
  telemetry.reason = stateReason_;
  telemetry.commandFresh = commandFresh(now);
  telemetry.feedbackFresh = feedbackFresh(now);
  telemetry.targetLinearMps = input_.linearMps;
  telemetry.targetAngularRadps = input_.angularRadps;
  telemetry.appliedLinearMps = output.linearMps;
  telemetry.appliedAngularRadps = output.angularRadps;
  telemetry.leftTargetMps = output.leftMps;
  telemetry.rightTargetMps = output.rightMps;
  telemetry.left = leftFeedback_;
  telemetry.right = rightFeedback_;
  telemetry.synchronizationError = lastSynchronizationError_;
  telemetry.synchronizationCorrectionMps = lastSynchronizationCorrectionMps_;
  emit telemetryChanged(telemetry);
}

void SynchronizedDriveController::resetMotionState() {
  appliedLinearMps_ = 0.0;
  appliedAngularRadps_ = 0.0;
  motionOutputStopped_ = true;
  lastSynchronizationError_ = 0.0;
  lastSynchronizationCorrectionMps_ = 0.0;
  synchronizer_.reset();
  input_ = {};
}

}  // namespace crawling
