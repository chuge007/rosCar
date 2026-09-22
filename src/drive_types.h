#pragma once

#include <QMetaType>
#include <QString>
#include "utf8_compat.h"

#include <array>
#include <cstdint>

namespace crawling {

struct CanFrame {
  std::uint32_t id = 0;
  std::array<std::uint8_t, 8> data{};
};

struct MotorFeedback {
  bool valid = false;
  double wheelSpeedMps = 0.0;
  double wheelPositionRad = 0.0;
  double motorSpeedDps = 0.0;
  // MWD status 2 exposes a model-dependent torque/current or power value.
  double motorControlValue = 0.0;
  double torqueCurrentA = 0.0;
  int temperatureC = 0;
  qint64 receivedAtMs = 0;
};

enum class DriveState {
  Disconnected,
  Idle,
  Arming,
  Enabled,
  Fault,
  EmergencyStop,
};

inline QString driveStateText(DriveState state) {
  switch (state) {
    case DriveState::Disconnected:
      return CRAWLING_TEXT("\xE6\x9C\xAA""\xE8\xBF\x9E""\xE6\x8E\xA5""");
    case DriveState::Idle:
      return CRAWLING_TEXT("\xE5\xB0\xB1""\xE7\xBB\xAA""");
    case DriveState::Arming:
      return CRAWLING_TEXT("\xE6\xAD\xA3""\xE5\x9C\xA8""\xE6\xA3\x80""\xE6\x9F\xA5""\xE5\x8F\x8D""\xE9\xA6\x88""");
    case DriveState::Enabled:
      return CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98""\xE5\xB7\xB2""\xE4\xBD\xBF""\xE8\x83\xBD""");
    case DriveState::Fault:
      return CRAWLING_TEXT("\xE5\xAE\x89""\xE5\x85\xA8""\xE6\x95\x85""\xE9\x9A\x9C""");
    case DriveState::EmergencyStop:
      return CRAWLING_TEXT("\xE7\xB4\xA7""\xE6\x80\xA5""\xE5\x81\x9C""\xE6\xAD\xA2""");
  }
  return CRAWLING_TEXT("\xE6\x9C\xAA""\xE7\x9F\xA5""");
}

struct DriveTelemetry {
  DriveState state = DriveState::Disconnected;
  QString reason;
  bool commandFresh = false;
  bool feedbackFresh = false;
  double targetLinearMps = 0.0;
  double targetAngularRadps = 0.0;
  double appliedLinearMps = 0.0;
  double appliedAngularRadps = 0.0;
  double leftTargetMps = 0.0;
  double rightTargetMps = 0.0;
  MotorFeedback left;
  MotorFeedback right;
  double synchronizationError = 0.0;
  double synchronizationCorrectionMps = 0.0;
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::DriveState)
Q_DECLARE_METATYPE(crawling::DriveTelemetry)
