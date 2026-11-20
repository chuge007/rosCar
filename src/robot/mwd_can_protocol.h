#pragma once

#include "drive_types.h"

#include <cstdint>
#include <optional>

namespace crawling {

// 夹爪 MWD 电机的 CAN 协议，依据《电机CAN协议说明 V2.35》。
// 注意：它与驱动轮的 V3.8 是两套协议，命令码和 0x9A 字段含义都不同，不能混用。
//   0x9A 状态1：Data[1] 温度, Data[2..3] 母线电压 0.01V, Data[4..5] 母线电流,
//                Data[6] motorState, Data[7] errorState
//   0x9B 清除电机错误标志（电机状态未恢复正常时清不掉）
//   0x9C 状态2 / 0x9D 状态3
//   0x80 电机关闭 / 0x88 电机运行 / 0x81 电机停止
//   0x8C 抱闸器控制：Data[1] 0x00 断电刹车启动 / 0x01 通电刹车释放 / 0x10 读状态
//   0xA2 速度闭环 / 0xA1 转矩闭环
class MwdCanProtocol final {
 public:
  static constexpr std::uint8_t kStatusQuery = 0x9A;
  static constexpr std::uint8_t kClearError = 0x9B;
  static constexpr std::uint8_t kStatusQuery2 = 0x9C;
  static constexpr std::uint8_t kStatusQuery3 = 0x9D;
  static constexpr std::uint8_t kMotorOff = 0x80;
  static constexpr std::uint8_t kMotorRun = 0x88;
  static constexpr std::uint8_t kMotorStop = 0x81;
  static constexpr std::uint8_t kBrakeControl = 0x8C;
  static constexpr std::uint8_t kBrakeReadStatus = 0x10;
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  static constexpr std::uint8_t kTorqueClosedLoop = 0xA1;
  static constexpr std::uint8_t kReadMultiTurnAngle = 0x92;
  static constexpr std::uint8_t kMultiTurnPosition = 0xA4;
  static constexpr std::uint32_t kCommandIdBase = 0x140;
  static constexpr std::uint32_t kFeedbackIdBase = 0x240;

  // DATA[0] = command，其余字节 0。
  static CanFrame simple(std::uint8_t motorId, std::uint8_t command);
  // 0xA2 速度闭环，int32、0.01 dps/LSB，落在 DATA[4..7]。
  static CanFrame speedCommand(std::uint8_t motorId, double outputSpeedDps);
  // 0x8C：release=true 发 0x01（通电释放），false 发 0x00（断电刹车）。
  static CanFrame brakeCommand(std::uint8_t motorId, bool release);
  // 0x8C + Data[1]=0x10，读当前抱闸器状态。
  static CanFrame brakeStatusQuery(std::uint8_t motorId);
  // 0x8C 回复的 Data[1]：1 = 通电释放，0 = 断电刹车。
  static std::optional<bool> parseBrakeStatus(const CanFrame& frame);
};

}  // namespace crawling
