#pragma once

#include "differential_mixer.h"
#include "drive_settings.h"
#include "drive_types.h"
#include "hardware_discovery.h"
#include "wheel_motor_controller.h"
#include "wheel_synchronizer.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace crawling {

class SynchronizedDriveController final : public QObject {
  Q_OBJECT

 public:
  explicit SynchronizedDriveController(QObject* parent = nullptr);

 public slots:
  void startControlLoop();
  void connectAdapter(const crawling::DriveSettings& settings);
  void autoDetectCanDevices(const QString& excludedPort);
  void disconnectAdapter();
  void setInputCommand(double linearMps, double angularRadps);
  void setCorrectionCommand(double linearMps, double angularRadps);
  void requestEnable(bool enabled);
  void emergencyStop();
  void systemReset();
  void clearAlarm();
  void shutdown();

 signals:
  void telemetryChanged(const crawling::DriveTelemetry& telemetry);
  void stateChanged(crawling::DriveState state, const QString& reason);
  void connectionChanged(bool connected, const QString& message);
  void canSettingsDetected(const crawling::HardwareDetectionResult& result);
  void logMessage(const QString& message);

 private slots:
  void controlTick();

 private:
  struct InputCommand {
    double linearMps = 0.0;
    double angularRadps = 0.0;
    qint64 receivedAtMs = 0;
    bool valid = false;
    bool preserveLinearSpeed = false;
  };

  qint64 nowMs() const;
  bool feedbackFresh(qint64 now) const;
  bool speedFeedbackFresh(qint64 now) const;
  bool commandFresh(qint64 now) const;
  void setState(DriveState state, const QString& reason);
  void enterFault(const QString& reason);
  void sendStopPair(bool urgent = false);
  void sendSpeedPair(double leftMps, double rightMps, bool urgent = false);
  void publishTelemetry(const WheelTargets& output = {});
  void resetMotionState();

  DriveSettings settings_;
  WheelMotorController wheelMotors_;
  QTimer controlTimer_;
  QElapsedTimer clock_;
  qint64 lastTickMs_ = 0;
  qint64 lastStopMs_ = -1000;
  qint64 lastMotorCommunicationMs_ = -1000;
  qint64 lastTelemetryEmitMs_ = -1000;
  qint64 leftSpeedFeedbackMs_ = -1000;
  qint64 rightSpeedFeedbackMs_ = -1000;
  qint64 lastSynchronizationFeedbackMs_ = -1000;
  InputCommand input_;
  MotorFeedback leftFeedback_;
  MotorFeedback rightFeedback_;
  WheelSynchronizer synchronizer_;
  DriveState state_ = DriveState::Disconnected;
  QString stateReason_ = CRAWLING_TEXT("\xE6\x9C\xAA""\xE8\xBF\x9E""\xE6\x8E\xA5""");
  double appliedLinearMps_ = 0.0;
  double appliedAngularRadps_ = 0.0;
  bool motionOutputStopped_ = true;
  bool wheelCommandFailureActive_ = false;
  double lastSynchronizationError_ = 0.0;
  double lastSynchronizationCorrectionMps_ = 0.0;
  double lastLeftResponseFactor_ = 1.0;
  double lastRightResponseFactor_ = 1.0;
  double lastLeftCommandScale_ = 1.0;
  double lastRightCommandScale_ = 1.0;
  bool lastCorrectionSteeringLimited_ = false;
};

}  // namespace crawling
