#include <gtest/gtest.h>

#include "crawling_robot_drivers/differential_drive.hpp"
#include "crawling_robot_drivers/servo_v38.hpp"
#include "crawling_robot_drivers/rim302_protocol.hpp"

namespace crawling_robot_drivers {

TEST(DifferentialDriveTest, KeepsTranslatingTurnWheelsSameDirection) {
  const auto left_turn = mixDifferentialWheelSpeeds(0.025, 0.30, 0.30, 0.5);
  EXPECT_GT(left_turn.left_m_s, 0.0);
  EXPECT_GT(left_turn.left_m_s, left_turn.right_m_s);
  EXPECT_NEAR(left_turn.right_m_s / left_turn.left_m_s, 0.5, 1e-12);
  EXPECT_NEAR(left_turn.applied_angular_rad_s, 0.05555555555555556, 1e-12);

  const auto right_turn = mixDifferentialWheelSpeeds(-0.025, -0.30, 0.30, 0.5);
  EXPECT_LT(right_turn.left_m_s, 0.0);
  EXPECT_LT(right_turn.right_m_s, 0.0);
  EXPECT_LT(right_turn.left_m_s, right_turn.right_m_s);
}

TEST(DifferentialDriveTest, StraightMotionKeepsPhysicalWheelsSynchronized) {
  const auto forward = mixDifferentialWheelSpeeds(0.025, 0.0, 0.30, 0.5);
  const auto reverse = mixDifferentialWheelSpeeds(-0.025, 0.0, 0.30, 0.5);
  EXPECT_DOUBLE_EQ(forward.left_m_s, forward.right_m_s);
  EXPECT_DOUBLE_EQ(reverse.left_m_s, reverse.right_m_s);
}

TEST(DifferentialDriveTest, WheelCapPreservesTurnRatio) {
  const auto limited = limitDifferentialWheelSpeeds({0.10, 0.30, 1.0}, 0.12);
  EXPECT_NEAR(limited.left_m_s, 0.04, 1e-12);
  EXPECT_NEAR(limited.right_m_s, 0.12, 1e-12);
  EXPECT_NEAR(limited.left_m_s / limited.right_m_s, 1.0 / 3.0, 1e-12);
  EXPECT_NEAR(limited.applied_angular_rad_s, 0.4, 1e-12);
}

TEST(DifferentialDriveTest, AllowsPivotTurnWhenLinearSpeedIsZero) {
  const auto pivot = mixDifferentialWheelSpeeds(0.0, 0.30, 0.30);
  EXPECT_GT(pivot.left_m_s, 0.0);
  EXPECT_LT(pivot.right_m_s, 0.0);
  EXPECT_DOUBLE_EQ(pivot.applied_angular_rad_s, 0.30);
}

TEST(ServoV38Test, EncodesSpeedAndDecodesFeedback) {
  const CanFrame command = ServoV38::speedCommand(1, 100.0);
  EXPECT_EQ(command.id, 0x141U);
  EXPECT_EQ(command.data[0], 0xA2U);
  EXPECT_EQ(command.data[4], 0x10U);
  EXPECT_EQ(command.data[5], 0x27U);

  CanFrame reply;
  reply.id = 0x241;
  reply.data = {0xA2, 50, 100, 0, 244, 1, 45, 0};
  const auto feedback = ServoV38::parseFeedback(reply);
  ASSERT_TRUE(feedback.has_value());
  EXPECT_EQ(feedback->motor_id, 1);
  EXPECT_EQ(feedback->temperature_c, 50);
  EXPECT_DOUBLE_EQ(feedback->torque_current_a, 1.0);
  EXPECT_DOUBLE_EQ(feedback->output_speed_dps, 500.0);
  EXPECT_DOUBLE_EQ(feedback->output_angle_deg, 45.0);
}

TEST(Rim302ProtocolTest, QueriesAndParsesDefaultOutput) {
  const auto request = Rim302Protocol::buildGetDefaultOutput();
  ASSERT_EQ(request.size(), 8U);
  EXPECT_EQ(request[2], Rim302Protocol::kGetDefaultOutput);
  EXPECT_EQ(request[7], 0x03U);

  const std::vector<std::uint8_t> frame = {
      0xFF, 0x02, 0x57, 0x00, 0x24,
      0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40,
      0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0xA0, 0x40, 0x00, 0x00, 0xC0, 0x40,
      0x00, 0x00, 0xE0, 0x40, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x10, 0x41};
  auto complete = frame;
  const auto crc = Rim302Protocol::crc16(complete.data() + 2, 39);
  complete.push_back(static_cast<std::uint8_t>(crc >> 8U));
  complete.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
  complete.push_back(0x03);

  Rim302FrameParser parser;
  const auto samples = parser.consume(complete.data(), complete.size());
  ASSERT_EQ(samples.size(), 1U);
  EXPECT_DOUBLE_EQ(samples.front().roll_rad, 1.0);
  EXPECT_DOUBLE_EQ(samples.front().pitch_rad, 2.0);
  EXPECT_DOUBLE_EQ(samples.front().linear_acceleration_z_m_s2, 9.0);
}

}  // namespace crawling_robot_drivers
