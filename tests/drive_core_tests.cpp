#include <QtTest>

#include "differential_mixer.h"
#include "servo_protocol.h"
#include "wheel_synchronizer.h"

namespace crawling {

class DriveCoreTests final : public QObject {
  Q_OBJECT

 private slots:
  void straightMotionCommandsBothWheelsEqually();
  void pivotTurnCommandsOppositeWheelDirections();
  void translatingTurnKeepsBothWheelsMovingForward();
  void translatingLeftTurnMirrorsWheelRatio();
  void speedLimitPreservesWheelRatio();
  void feedbackSynchronizerCorrectsTheSlowerSide();
  void feedbackSynchronizerKeepsReverseDirection();
  void staleFeedbackDisablesSynchronization();
  void protocolEncodesAndDecodesSpeedFrames();
  void protocolAcceptsStopFeedbackFrames();
  void protocolEncodesAndDecodesStatusFrames();
};

void DriveCoreTests::straightMotionCommandsBothWheelsEqually() {
  const auto wheel = DifferentialMixer::mix(0.15, 0.0, 0.30, 0.50);
  QCOMPARE(wheel.leftMps, 0.15);
  QCOMPARE(wheel.rightMps, 0.15);
}

void DriveCoreTests::pivotTurnCommandsOppositeWheelDirections() {
  const auto wheel = DifferentialMixer::mix(0.0, 0.8, 0.30, 0.50);
  QCOMPARE(wheel.leftMps, 0.12);
  QCOMPARE(wheel.rightMps, -0.12);
  QCOMPARE(wheel.angularRadps, 0.8);
}

void DriveCoreTests::translatingTurnKeepsBothWheelsMovingForward() {
  // Positive angular speed is a right turn: left wheel is the outer wheel.
  const auto wheel = DifferentialMixer::mix(0.025, 0.30, 0.30, 0.50);
  QVERIFY(wheel.leftMps > 0.0);
  QVERIFY(wheel.rightMps > 0.0);
  QVERIFY(wheel.leftMps > wheel.rightMps);
  QCOMPARE(wheel.rightMps / wheel.leftMps, 0.50);
}

void DriveCoreTests::translatingLeftTurnMirrorsWheelRatio() {
  // Negative angular speed is a left turn: right wheel is the outer wheel.
  const auto wheel = DifferentialMixer::mix(0.025, -0.30, 0.30, 0.50);
  QVERIFY(wheel.leftMps > 0.0);
  QVERIFY(wheel.rightMps > 0.0);
  QVERIFY(wheel.rightMps > wheel.leftMps);
  QCOMPARE(wheel.leftMps / wheel.rightMps, 0.50);
}

void DriveCoreTests::speedLimitPreservesWheelRatio() {
  WheelTargets target;
  target.leftMps = 0.10;
  target.rightMps = 0.30;
  target.linearMps = 0.20;
  target.angularRadps = -1.0;
  const auto limited = DifferentialMixer::limitUniformly(target, 0.12);
  QCOMPARE(limited.leftMps, 0.04);
  QCOMPARE(limited.rightMps, 0.12);
  QCOMPARE(limited.leftMps / limited.rightMps, 1.0 / 3.0);
}

void DriveCoreTests::feedbackSynchronizerCorrectsTheSlowerSide() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(0.10, 0.10, 0.07, 0.10, true, 0.02,
                                          SynchronizerConfig{});
  QVERIFY(result.active);
  QVERIFY(result.normalizedError > 0.0);
  QVERIFY(result.correctionMps > 0.0);
  QVERIFY(result.leftMps > 0.10);
  QVERIFY(result.rightMps < 0.10);
}

void DriveCoreTests::feedbackSynchronizerKeepsReverseDirection() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(-0.10, -0.10, -0.07, -0.10, true, 0.02,
                                          SynchronizerConfig{});
  QVERIFY(result.active);
  QVERIFY(result.leftMps < -0.10);
  QVERIFY(result.rightMps > -0.10);
  QVERIFY(result.leftMps <= 0.0);
  QVERIFY(result.rightMps <= 0.0);
}

void DriveCoreTests::staleFeedbackDisablesSynchronization() {
  WheelSynchronizer synchronizer;
  const auto result = synchronizer.update(0.10, 0.10, 0.0, 0.10, false, 0.02,
                                          SynchronizerConfig{});
  QVERIFY(!result.active);
  QCOMPARE(result.leftMps, 0.10);
  QCOMPARE(result.rightMps, 0.10);
}

void DriveCoreTests::protocolEncodesAndDecodesSpeedFrames() {
  const CanFrame command = ServoProtocol::speedCommand(1, 100.0);
  QCOMPARE(command.id, 0x141U);
  QCOMPARE(command.data[0], 0xA2U);
  QCOMPARE(command.data[4], 0x10U);
  QCOMPARE(command.data[5], 0x27U);

  CanFrame response;
  response.id = 0x241;
  response.data = {0xA2, 50, 100, 0, 244, 1, 45, 0};
  const auto feedback = ServoProtocol::parseFeedback(response);
  QVERIFY(feedback.has_value());
  QCOMPARE(feedback->motorId, static_cast<std::uint8_t>(1));
  QCOMPARE(feedback->temperatureC, 50);
  QCOMPARE(feedback->outputSpeedDps, 500.0);
}

void DriveCoreTests::protocolEncodesAndDecodesStatusFrames() {
  const CanFrame query = ServoProtocol::statusQuery(7);
  QCOMPARE(query.id, 0x147U);
  QCOMPARE(query.data[0], 0x9AU);

  CanFrame response;
  response.id = 0x247;
  response.data = {0x9A, 42, 0, 0, 0, 0, 0x34, 0x12};
  const auto status = ServoProtocol::parseStatus(response);
  QVERIFY(status.has_value());
  QCOMPARE(status->motorId, static_cast<std::uint8_t>(7));
  QCOMPARE(status->temperatureC, 42);
  QCOMPARE(status->errorState, static_cast<std::uint16_t>(0x1234));
}

void DriveCoreTests::protocolAcceptsStopFeedbackFrames() {
  CanFrame response;
  response.id = 0x242;
  response.data = {0x81, 0, 0, 0, 0, 0, 0, 0};
  const auto feedback = ServoProtocol::parseFeedback(response);
  QVERIFY(feedback.has_value());
  QCOMPARE(feedback->motorId, static_cast<std::uint8_t>(2));
}

}  // namespace crawling

QTEST_APPLESS_MAIN(crawling::DriveCoreTests)

#include "drive_core_tests.moc"
