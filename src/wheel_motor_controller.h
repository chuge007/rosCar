#pragma once

#include "mwd_rs485_protocol.h"

#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <memory>

namespace crawling {

// The two wheel motors may use separate MWD RS485 adapters. Motor IDs are
// scoped to their configured serial port (1..32), not globally to the robot.
struct WheelMotorConfig {
  static constexpr double kMaximumSynchronizedMotorSpeedDps = 4300.0;

  QString leftSerialPort;
  int leftBaudRate = 115200;
  QString rightSerialPort;
  int rightBaudRate = 115200;
  std::uint8_t leftMotorId = 1;
  std::uint8_t rightMotorId = 2;
  int leftDirectionSign = 1;
  int rightDirectionSign = 1;
  double wheelRadiusM = 0.040;
  double motorOutputToWheelRatio = 36.0;
  double maximumWheelSpeedMps = 0.30;
  // The installed left drive tops out near 4,350 dps while the right drive
  // reaches about 14,300 dps. Keep paired motion below the slower drive's
  // verified range so a short manual jog is synchronized from its first frame.
  double maximumMotorSpeedDps = kMaximumSynchronizedMotorSpeedDps;
};

struct WheelMotorFeedback {
  bool valid = false;
  bool leftUpdated = false;
  bool rightUpdated = false;
  int leftEncoder = 0;
  int rightEncoder = 0;
  double leftPositionM = 0.0;
  double rightPositionM = 0.0;
  double leftSpeedMps = 0.0;
  double rightSpeedMps = 0.0;
  double leftMotorSpeedDps = 0.0;
  double rightMotorSpeedDps = 0.0;
  double leftMotorControlValue = 0.0;
  double rightMotorControlValue = 0.0;
  int leftTemperatureC = 0;
  int rightTemperatureC = 0;
};

// Small synchronous adapter for the MWD RS485 protocol. The controller is
// intentionally not a QObject; callers poll it from their existing timer.
class WheelMotorController final {
 public:
  WheelMotorController() = default;
  ~WheelMotorController();

  WheelMotorController(const WheelMotorController&) = delete;
  WheelMotorController& operator=(const WheelMotorController&) = delete;

  bool initialize(const WheelMotorConfig& config, QString* errorMessage = nullptr);
  void shutdown();

  bool isInitialized() const { return initialized_; }
  bool isStopped() const { return !leftRunning_ && !rightRunning_; }
  int lastLeftCommandDps() const { return lastLeftCommandDps_; }
  int lastRightCommandDps() const { return lastRightCommandDps_; }
  double lastLeftCommandMps() const;
  double lastRightCommandMps() const;
  double maximumCommandableWheelSpeedMps() const;
  std::int64_t lastLeftHoldAngleHundredthDegree() const {
    return lastLeftHoldAngleHundredthDegree_;
  }
  std::int64_t lastRightHoldAngleHundredthDegree() const {
    return lastRightHoldAngleHundredthDegree_;
  }
  double leftRawFeedbackDps() const { return leftFeedback_.speedDps; }
  double rightRawFeedbackDps() const { return rightFeedback_.speedDps; }
  bool setWheelSpeeds(double leftMps, double rightMps);
  bool stop();
  bool reset();
  bool pollFeedback(WheelMotorFeedback* feedback, bool requestStatus = true);

  static bool resolvePort(const QString& specification, QString* serialPort,
                          QString* errorMessage = nullptr);

 private:
  bool sendCommand(bool leftMotor, std::uint8_t command, std::uint8_t motorId,
                   const QByteArray& data = {});
  bool holdCurrentPosition(bool leftMotor, std::uint8_t motorId,
                           std::int64_t* holdAngleHundredthDegree);
  bool sendSpeed(bool leftMotor, std::uint8_t motorId, double wheelMps,
                 int directionSign);
  QSerialPort* serialPortFor(bool leftMotor) const;
  QByteArray& receiveBufferFor(bool leftMotor);
  int toMotorSpeedDps(double wheelMps, int directionSign) const;
  double toWheelSpeedMps(int motorSpeedDps, int directionSign) const;
  void setError(QString* errorMessage, const QString& text) const;

  WheelMotorConfig config_;
  std::unique_ptr<QSerialPort> leftSerialPort_;
  std::unique_ptr<QSerialPort> rightSerialPort_;
  QByteArray leftReceiveBuffer_;
  QByteArray rightReceiveBuffer_;
  bool initialized_ = false;
  bool sharedPort_ = false;
  bool leftRunning_ = false;
  bool rightRunning_ = false;
  int lastLeftCommandDps_ = 0;
  int lastRightCommandDps_ = 0;
  std::int64_t lastLeftHoldAngleHundredthDegree_ = 0;
  std::int64_t lastRightHoldAngleHundredthDegree_ = 0;
  MwdMotorFeedback leftFeedback_;
  MwdMotorFeedback rightFeedback_;
  bool leftFeedbackUpdated_ = false;
  bool rightFeedbackUpdated_ = false;
  std::int32_t leftEncoderValue_ = 0;
  std::int32_t rightEncoderValue_ = 0;
  std::int64_t leftAngleHundredthDegree_ = 0;
  std::int64_t rightAngleHundredthDegree_ = 0;
};

}  // namespace crawling
