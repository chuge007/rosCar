#include "synchronized_drive_controller.h"

#include "utf8_compat.h"

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr int kMotorCommunicationIntervalMs = 334;
constexpr double kMotionEpsilonMps = 1e-9;
constexpr bool kMotorOutputEnabled = true;

double approachVectorComponent(double current, double target, double scale) {
  return current + (target - current) * scale;
}

}  // namespace

SynchronizedDriveController::SynchronizedDriveController(QObject* parent)
    : QObject(parent), controlTimer_(this) {
  controlTimer_.setInterval(20);
  controlTimer_.setTimerType(Qt::PreciseTimer);
  connect(&controlTimer_, &QTimer::timeout, this, &SynchronizedDriveController::controlTick);
  clock_.start();
}

void SynchronizedDriveController::startControlLoop() {
  lastTickMs_ = nowMs();
  controlTimer_.start();
}

void SynchronizedDriveController::connectAdapter(const DriveSettings& settings) {
  emit logMessage(QStringLiteral("event=connect_start module=DRIVE.MOTOR left_port=%1 left_baud=%2 left_id=%3 left_sign=%4 right_port=%5 right_baud=%6 right_id=%7 right_sign=%8 wheel_radius_m=%9 motor_to_wheel_ratio=%10")
                      .arg(settings.leftMotorSerialPort).arg(settings.leftMotorBaudRate)
                      .arg(settings.leftMotorId).arg(settings.leftMotorSign)
                      .arg(settings.rightMotorSerialPort).arg(settings.rightMotorBaudRate)
                      .arg(settings.rightMotorId).arg(settings.rightMotorSign)
                      .arg(settings.wheelRadiusM, 0, 'f', 6)
                      .arg(settings.motorOutputToWheelRatio, 0, 'f', 4));
  if (state_ == DriveState::Enabled || state_ == DriveState::Arming) {
    requestEnable(false);
  }
  const QString validationError = settings.validationError();
  if (!validationError.isEmpty()) {
    enterFault(validationError);
    return;
  }
  settings_ = settings;
  settings_.feedbackTimeoutMs = std::max(settings_.feedbackTimeoutMs,
                                         DriveSettings::kMinimumFeedbackTimeoutMs);
  resetMotionState();
  leftFeedback_ = {};
  rightFeedback_ = {};
  leftSpeedFeedbackMs_ = -1000;
  rightSpeedFeedbackMs_ = -1000;
  lastMotorCommunicationMs_ = -1000;
  lastSynchronizationFeedbackMs_ = -1000;

  WheelMotorConfig motorConfig;
  QString portError;
  if (!WheelMotorController::resolvePort(settings_.leftMotorSerialPort,
                                          &motorConfig.leftSerialPort, &portError) ||
      !WheelMotorController::resolvePort(settings_.rightMotorSerialPort,
                                          &motorConfig.rightSerialPort, &portError)) {
    enterFault(portError);
    emit connectionChanged(false, portError);
    return;
  }
  motorConfig.leftBaudRate = settings_.leftMotorBaudRate;
  motorConfig.rightBaudRate = settings_.rightMotorBaudRate;
  motorConfig.leftMotorId = static_cast<std::uint8_t>(settings_.leftMotorId);
  motorConfig.rightMotorId = static_cast<std::uint8_t>(settings_.rightMotorId);
  motorConfig.leftDirectionSign = settings_.leftMotorSign;
  motorConfig.rightDirectionSign = settings_.rightMotorSign;
  motorConfig.wheelRadiusM = settings_.wheelRadiusM;
  motorConfig.motorOutputToWheelRatio = settings_.motorOutputToWheelRatio;
  motorConfig.maximumWheelSpeedMps = settings_.maximumWheelSpeedMps;
  QString motorError;
  if (!wheelMotors_.initialize(motorConfig, &motorError)) {
    enterFault(motorError);
    emit connectionChanged(false, motorError);
    return;
  }
  emit logMessage(QStringLiteral("event=connect_complete result=OK module=DRIVE.MOTOR left_port=%1 right_port=%2")
                      .arg(motorConfig.leftSerialPort, motorConfig.rightSerialPort));
  emit logMessage(QStringLiteral(
      "event=wheel_position_hold result=OK module=DRIVE.MOTOR reason=connect "
      "commands=0x81+0x92+0xA4 left_target_deg=%1 right_target_deg=%2")
                      .arg(wheelMotors_.lastLeftHoldAngleHundredthDegree() / 100.0,
                           0, 'f', 2)
                      .arg(wheelMotors_.lastRightHoldAngleHundredthDegree() / 100.0,
                           0, 'f', 2));
  emit logMessage(QStringLiteral(
      "event=motor_communication_profile cyclic_hz=3 interval_ms=%1 "
      "feedback_timeout_ms=%2 mode=A2_REPLY_WHILE_MOVING_9C_WHILE_STOPPED")
                      .arg(kMotorCommunicationIntervalMs)
                      .arg(settings_.feedbackTimeoutMs));
  emit logMessage(QStringLiteral(
      "event=wheel_command_limit configured_mps=%1 effective_mps=%2 "
      "motor_limit_dps=%3")
                      .arg(settings_.maximumWheelSpeedMps, 0, 'f', 4)
                      .arg(wheelMotors_.maximumCommandableWheelSpeedMps(),
                           0, 'f', 4)
                      .arg(motorConfig.maximumMotorSpeedDps, 0, 'f', 0));
  emit connectionChanged(true, QStringLiteral("MWD RS485 wheel motor bus connected"));
  setState(DriveState::Idle,
           QStringLiteral("MWD RS485 motor bus connected; drive output disabled"));
}

void SynchronizedDriveController::autoDetectCanDevices(const QString& excludedPort) {
  if (wheelMotors_.isInitialized()) {
    emit logMessage(QStringLiteral("MWD RS485 motor bus is already connected"));
    emit canSettingsDetected(HardwareDetectionResult{});
    return;
  }
  // Keep CAN discovery for the clamp and legacy diagnostics. Wheel control
  // itself uses the configured MWD RS485 bus.
  const HardwareDetectionResult result = HardwareDiscovery::probeCanPorts(excludedPort);
  emit canSettingsDetected(result);
  QStringList details = result.details;
  if (details.isEmpty()) {
    details << QStringLiteral("No CANopen device detected; wheel motors use MWD RS485");
  }
  emit logMessage(details.join(QStringLiteral("; ")));
}

void SynchronizedDriveController::disconnectAdapter() {
  resetMotionState();
  setState(DriveState::Disconnected,
           CRAWLING_TEXT("\xE6\x93\x8D\xE4\xBD\x9C\xE5\x91\x98\xE5\xB7\xB2\xE6\x96\xAD\xE5\xBC\x80\xE9\x80\x82\xE9\x85\x8D\xE5\x99\xA8"));
  sendStopPair(true);
  wheelMotors_.shutdown();
  wheelCommandFailureActive_ = false;
  emit connectionChanged(false, QStringLiteral("MWD RS485 wheel motor bus disconnected"));
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
    resetMotionState();
    setState(wheelMotors_.isInitialized() ? DriveState::Idle : DriveState::Disconnected,
             CRAWLING_TEXT("\xE5\xba\x95\xE7\x9B\x98\xE8\xbe\x93\xE5\x87\xba\xE5\xb7\xb2\xE7\xa6\x81\xE7\x94\xa8"));
    if (wheelMotors_.isInitialized()) {
      sendStopPair(true);
    }
    return;
  }
  if (state_ == DriveState::EmergencyStop) {
    emit logMessage(CRAWLING_TEXT("\xE8\xaf\xb7\xE5\x85\x88\xE7\xA6\x81\xE7\x94\xa8\xE5\xba\x95\xE7\x9B\x98\xEF\xbc\x8C\xE5\x86\x8D\xE9\x87\x8D\xE6\x96\xb0\xE4\xbd\xbf\xE8\x83\xbd\xE4\xbb\xa5\xE8\xa7\xa3\xE9\x99\xa4\xE6\x80\xa5\xE5\x81\x9c"));
    return;
  }
  if (!wheelMotors_.isInitialized()) {
    enterFault(CRAWLING_TEXT("\xE8\xbd\xae\xE5\xAD\x90\xE7\x94\xb5\xE6\x9C\xba\xE5\xba\x93\xE6\x9C\xaa\xE8\xbf\x9E\xE6\x8E\xa5"));
    return;
  }
  resetMotionState();
  input_.valid = true;
  input_.receivedAtMs = nowMs();
  setState(DriveState::Enabled,
           CRAWLING_TEXT("\xE5\xba\x95\xE7\x9B\x98\xE5\xb7\xb2\xE4\xbd\xbf\xE8\x83\xbd\xEF\xbc\x8C\xE5\x8F\xaf\xE6\x8E\xa7\xE5\x88\xb6"));
  emit logMessage(QStringLiteral("Drive enabled: MWD RS485 wheel speed control is active"));
  // Enabling only arms command processing. Keep both motors stopped until a
  // real manual-jog or automatic-correction motion command arrives.
  sendStopPair(true);
}

void SynchronizedDriveController::emergencyStop() {
  resetMotionState();
  setState(DriveState::EmergencyStop,
           CRAWLING_TEXT("\xE5\xb7\xb2\xE8\xaf\xb7\xE6\xb1\x82\xE7\xb4\xA7\xE6\x80\xa5\xE5\x81\x9C\xE6\xad\xa2"));
  sendStopPair(true);
}

void SynchronizedDriveController::systemReset() {
  if (!wheelMotors_.isInitialized()) {
    emit logMessage(QStringLiteral("System reset skipped: MWD RS485 motor bus is disconnected"));
    return;
  }
  resetMotionState();
  setState(DriveState::Idle, QStringLiteral("MWD RS485 motor reset/stop command sent"));
  emit logMessage(QStringLiteral("MWD RS485 motor reset/stop result=%1")
                      .arg(wheelMotors_.reset() ? QStringLiteral("OK")
                                                 : QStringLiteral("FAILED")));
}

void SynchronizedDriveController::clearAlarm() {
  if (!wheelMotors_.isInitialized()) {
    emit logMessage(QStringLiteral("Clear alarm skipped: MWD RS485 motor bus is disconnected"));
    return;
  }
  emit logMessage(QStringLiteral("MWD RS485 motor clear alarm/stop result=%1")
                      .arg(wheelMotors_.reset() ? QStringLiteral("OK")
                                                 : QStringLiteral("FAILED")));
}

void SynchronizedDriveController::shutdown() {
  controlTimer_.stop();
  resetMotionState();
  setState(DriveState::Disconnected, CRAWLING_TEXT("\xE7\xa8\x8B\xE5\xba\x8F\xE5\xb7\xb2\xE5\x85\xb3\xE9\x97\xad"));
  sendStopPair(true);
  wheelMotors_.shutdown();
}

void SynchronizedDriveController::controlTick() {
  const qint64 now = nowMs();
  const double deltaSeconds = std::clamp((now - lastTickMs_) / 1000.0, 0.001, 0.050);
  lastTickMs_ = now;

  if (wheelMotors_.isInitialized()) {
    WheelMotorFeedback feedback;
    const bool zeroInput = std::abs(input_.linearMps) <= kMotionEpsilonMps &&
                           std::abs(input_.angularRadps) <= kMotionEpsilonMps;
    const bool statusPollingState = wheelMotors_.isStopped() &&
        (state_ == DriveState::Idle || state_ == DriveState::Fault ||
         state_ == DriveState::EmergencyStop ||
         (state_ == DriveState::Enabled && motionOutputStopped_ && zeroInput));
    const bool requestStatus = statusPollingState &&
        now - lastMotorCommunicationMs_ >= kMotorCommunicationIntervalMs;
    if (requestStatus) {
      lastMotorCommunicationMs_ = now;
    }
    wheelMotors_.pollFeedback(&feedback, requestStatus);
    if (feedback.leftUpdated) {
      leftFeedback_.valid = true;
      leftFeedback_.wheelSpeedMps = feedback.leftSpeedMps;
      leftFeedback_.wheelPositionRad = feedback.leftPositionM / settings_.wheelRadiusM;
      leftFeedback_.motorSpeedDps = feedback.leftMotorSpeedDps;
      leftFeedback_.motorControlValue = feedback.leftMotorControlValue;
      leftFeedback_.temperatureC = feedback.leftTemperatureC;
      leftFeedback_.receivedAtMs = now;
      leftSpeedFeedbackMs_ = now;
    }
    if (feedback.rightUpdated) {
      rightFeedback_.valid = true;
      rightFeedback_.wheelSpeedMps = feedback.rightSpeedMps;
      rightFeedback_.wheelPositionRad = feedback.rightPositionM / settings_.wheelRadiusM;
      rightFeedback_.motorSpeedDps = feedback.rightMotorSpeedDps;
      rightFeedback_.motorControlValue = feedback.rightMotorControlValue;
      rightFeedback_.temperatureC = feedback.rightTemperatureC;
      rightFeedback_.receivedAtMs = now;
      rightSpeedFeedbackMs_ = now;
    }
  }
  if (!kMotorOutputEnabled) {
    publishTelemetry();
    return;
  }

  if (state_ == DriveState::Enabled) {
    const double targetLinear = std::clamp(input_.linearMps,
                                           -settings_.maximumLinearSpeedMps,
                                           settings_.maximumLinearSpeedMps);
    const double targetAngular = std::clamp(input_.angularRadps,
                                            -settings_.maximumAngularSpeedRadps,
                                            settings_.maximumAngularSpeedRadps);
    if (std::abs(targetLinear) <= 1e-12 && std::abs(targetAngular) <= 1e-12) {
      const bool stoppingNow = !motionOutputStopped_;
      appliedLinearMps_ = 0.0;
      appliedAngularRadps_ = 0.0;
      synchronizer_.reset();
      if (stoppingNow || !wheelMotors_.isStopped()) {
        sendStopPair(stoppingNow);
      }
      motionOutputStopped_ = true;
      publishTelemetry();
      return;
    }
    if (!commandFresh(now)) {
      enterFault(CRAWLING_TEXT("\xE8\xbf\x90\xe5\x8a\xa8\xE5\x91\xbd\xe4\xbb\xa4\xE7\x9C\x8B\xe9\x97\xa8\xe7\x8B\x97\xE8\xb6\x85\xe6\x97\xb6"));
      return;
    }
    if (!feedbackFresh(now)) {
      const qint64 leftAgeMs = leftFeedback_.valid
                                   ? now - leftFeedback_.receivedAtMs
                                   : -1;
      const qint64 rightAgeMs = rightFeedback_.valid
                                    ? now - rightFeedback_.receivedAtMs
                                    : -1;
      enterFault(QStringLiteral(
                     "MWD RS485 motor feedback timeout: "
                     "left_valid=%1 left_age_ms=%2 "
                     "right_valid=%3 right_age_ms=%4 timeout_ms=%5")
                     .arg(leftFeedback_.valid ? 1 : 0)
                     .arg(leftAgeMs)
                     .arg(rightFeedback_.valid ? 1 : 0)
                     .arg(rightAgeMs)
                     .arg(settings_.feedbackTimeoutMs));
      return;
    }
    const double linearDelta = targetLinear - appliedLinearMps_;
    const double angularDelta = targetAngular - appliedAngularRadps_;
    double rampScale = 1.0;
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
    const double effectiveWheelSpeedLimitMps =
        wheelMotors_.maximumCommandableWheelSpeedMps();
    output = DifferentialMixer::limitUniformly(output,
                                                effectiveWheelSpeedLimitMps);
    const bool synchronizationFeedbackUpdated =
        leftFeedback_.receivedAtMs > lastSynchronizationFeedbackMs_ &&
        rightFeedback_.receivedAtMs > lastSynchronizationFeedbackMs_;
    const double synchronizationDeltaSeconds = synchronizationFeedbackUpdated
        ? std::clamp((now - lastSynchronizationFeedbackMs_) / 1000.0,
                     0.001, 0.500)
        : deltaSeconds;
    const SynchronizerResult synchronization = synchronizer_.update(
        output.leftMps, output.rightMps, leftFeedback_.wheelSpeedMps,
        rightFeedback_.wheelSpeedMps, wheelMotors_.lastLeftCommandMps(),
        wheelMotors_.lastRightCommandMps(),
        speedFeedbackFresh(now) && synchronizationFeedbackUpdated,
        synchronizationDeltaSeconds,
        settings_.synchronizer);
    if (synchronizationFeedbackUpdated) {
      lastSynchronizationFeedbackMs_ = now;
    }
    output.leftMps = synchronization.leftMps;
    output.rightMps = synchronization.rightMps;
    output.linearMps = (output.leftMps + output.rightMps) * 0.5;
    output.angularRadps = (output.leftMps - output.rightMps) / settings_.trackWidthM;
    output = DifferentialMixer::limitUniformly(output,
                                                effectiveWheelSpeedLimitMps);
    lastSynchronizationError_ = synchronization.normalizedError;
    lastSynchronizationCorrectionMps_ = synchronization.correctionMps;
    lastLeftResponseFactor_ = synchronization.leftResponseFactor;
    lastRightResponseFactor_ = synchronization.rightResponseFactor;
    lastLeftCommandScale_ = synchronization.leftCommandScale;
    lastRightCommandScale_ = synchronization.rightCommandScale;
    sendSpeedPair(output.leftMps, output.rightMps, motionOutputStopped_);
    motionOutputStopped_ = false;
    publishTelemetry(output);
    return;
  }

  const bool stopRetryRequired = !wheelMotors_.isStopped() &&
      (state_ == DriveState::Fault || state_ == DriveState::EmergencyStop ||
       state_ == DriveState::Idle);
  if (stopRetryRequired &&
      now - lastStopMs_ >= kMotorCommunicationIntervalMs) {
    sendStopPair();
  }
  publishTelemetry();
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
  emit logMessage(CRAWLING_TEXT("event=state_changed state=%1 reason=%2")
                      .arg(driveStateText(state_), stateReason_));
}

void SynchronizedDriveController::enterFault(const QString& reason) {
  resetMotionState();
  setState(DriveState::Fault, reason);
  sendStopPair(true);
  publishTelemetry();
}

void SynchronizedDriveController::sendStopPair(bool urgent) {
  if (wheelMotors_.isInitialized()) {
    const qint64 now = nowMs();
    if (!urgent && now - lastStopMs_ < kMotorCommunicationIntervalMs) {
      return;
    }
    const bool sent = wheelMotors_.stop();
    lastStopMs_ = nowMs();
    lastMotorCommunicationMs_ = lastStopMs_;
    if (!sent) {
      if (!wheelCommandFailureActive_) {
        wheelCommandFailureActive_ = true;
        emit logMessage(QStringLiteral(
            "event=wheel_position_hold result=FAILED module=DRIVE.MOTOR "
            "commands=0x81+0x92+0xA4"));
      }
    } else {
      if (wheelCommandFailureActive_) {
        wheelCommandFailureActive_ = false;
        emit logMessage(QStringLiteral(
            "event=wheel_command_write_recovered result=OK module=DRIVE.MOTOR"));
      }
      if (urgent) {
        emit logMessage(QStringLiteral(
            "event=wheel_position_hold result=OK module=DRIVE.MOTOR "
            "commands=0x81+0x92+0xA4 "
            "left_target_deg=%1 right_target_deg=%2")
                            .arg(wheelMotors_.lastLeftHoldAngleHundredthDegree() /
                                     100.0,
                                 0, 'f', 2)
                            .arg(wheelMotors_.lastRightHoldAngleHundredthDegree() /
                                     100.0,
                                 0, 'f', 2));
      }
    }
  }
}

void SynchronizedDriveController::sendSpeedPair(double leftMps, double rightMps, bool urgent) {
  if (!wheelMotors_.isInitialized()) {
    return;
  }
  const qint64 now = nowMs();
  const bool motionRequested = std::abs(leftMps) > kMotionEpsilonMps ||
                               std::abs(rightMps) > kMotionEpsilonMps;
  if (!motionRequested) {
    sendStopPair(urgent);
    return;
  }
  if (!urgent &&
      now - lastMotorCommunicationMs_ < kMotorCommunicationIntervalMs) {
    return;
  }
  lastMotorCommunicationMs_ = now;
  const bool sent = wheelMotors_.setWheelSpeeds(leftMps, rightMps);
  if (!sent && !wheelCommandFailureActive_) {
    wheelCommandFailureActive_ = true;
    emit logMessage(QStringLiteral("event=wheel_speed_write result=FAILED module=DRIVE.MOTOR left_mps=%1 right_mps=%2")
                        .arg(leftMps, 0, 'f', 3).arg(rightMps, 0, 'f', 3));
  } else if (sent && wheelCommandFailureActive_) {
    wheelCommandFailureActive_ = false;
    emit logMessage(QStringLiteral("event=wheel_command_write_recovered result=OK module=DRIVE.MOTOR"));
  }
  if (sent) {
    emit logMessage(QStringLiteral(
        "event=wheel_speed_write result=OK module=DRIVE.MOTOR "
        "input_linear_mps=%1 input_angular_radps=%2 "
        "left_target_mps=%3 right_target_mps=%4 "
        "left_feedback_mps=%5 right_feedback_mps=%6 "
        "left_motor_command_dps=%7 right_motor_command_dps=%8 "
        "left_motor_feedback_raw_dps=%9 right_motor_feedback_raw_dps=%10 "
        "left_sign=%11 right_sign=%12 effective_limit_mps=%13 "
        "sync_left_response=%14 sync_right_response=%15 "
        "sync_left_scale=%16 sync_right_scale=%17")
                        .arg(input_.linearMps, 0, 'f', 4)
                        .arg(input_.angularRadps, 0, 'f', 4)
                        .arg(leftMps, 0, 'f', 4)
                        .arg(rightMps, 0, 'f', 4)
                        .arg(leftFeedback_.wheelSpeedMps, 0, 'f', 4)
                        .arg(rightFeedback_.wheelSpeedMps, 0, 'f', 4)
                        .arg(wheelMotors_.lastLeftCommandDps())
                        .arg(wheelMotors_.lastRightCommandDps())
                        .arg(wheelMotors_.leftRawFeedbackDps(), 0, 'f', 1)
                        .arg(wheelMotors_.rightRawFeedbackDps(), 0, 'f', 1)
                        .arg(settings_.leftMotorSign)
                        .arg(settings_.rightMotorSign)
                        .arg(wheelMotors_.maximumCommandableWheelSpeedMps(),
                             0, 'f', 4)
                        .arg(lastLeftResponseFactor_, 0, 'f', 3)
                        .arg(lastRightResponseFactor_, 0, 'f', 3)
                        .arg(lastLeftCommandScale_, 0, 'f', 3)
                        .arg(lastRightCommandScale_, 0, 'f', 3));
  }
}

void SynchronizedDriveController::publishTelemetry(const WheelTargets& output) {
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
  lastLeftResponseFactor_ = 1.0;
  lastRightResponseFactor_ = 1.0;
  lastLeftCommandScale_ = 1.0;
  lastRightCommandScale_ = 1.0;
  synchronizer_.reset();
  input_ = {};
}

}  // namespace crawling
