#pragma once

#include "drive_types.h"

#include <optional>

namespace crawling {

struct ServoFeedback {
  std::uint8_t motorId = 0;
  int temperatureC = 0;
  double torqueCurrentA = 0.0;
  double outputSpeedDps = 0.0;
  double outputAngleDeg = 0.0;
};

struct ServoStatus {
  std::uint8_t motorId = 0;
  int temperatureC = 0;
  bool brakeReleased = false;
  std::uint16_t errorState = 0;
};

// 命令码依据官方《伺服电机控制协议 V3.8》（82 页版）的 2.x 章节：
//   0x80 电机关闭 / 0x81 电机停止 / 0x76 系统复位 / 0x77 系统抱闸释放 /
//   0x78 系统抱闸锁死 / 0x9A 状态1+错误标志 / 0x9C 状态2 / 0x9D 状态3 /
//   0xA2 速度闭环 / 0xA1 转矩闭环 / 0xB2 软件版本日期 / 0xB5 电机型号。
// 注意：0x88、0x8C 不在命令表里（早期版本按夹爪实现猜的），驱动器只会原样回显。
class ServoProtocol final {
 public:
  static constexpr std::uint8_t kSpeedClosedLoop = 0xA2;
  // 电机关闭（0x80）：关输出、清运行状态，不是"系统复位"。
  static constexpr std::uint8_t kMotorOff = 0x80;
  static constexpr std::uint8_t kStatusQuery = 0x9A;
  static constexpr std::uint8_t kSpeedFeedbackQuery = 0x9C;
  static constexpr std::uint8_t kMotorStop = 0x81;
  // 系统复位（0x76）：驱动器重启，收到后不返回应答，通讯会中断数秒。
  static constexpr std::uint8_t kSystemReset = 0x76;
  // 抱闸释放 0x77 / 锁死 0x78：回复与发送帧相同；0x9A 的 Data[3] 反映执行结果。
  static constexpr std::uint8_t kBrakeRelease = 0x77;
  static constexpr std::uint8_t kBrakeLock = 0x78;
  static constexpr std::uint32_t kCommandIdBase = 0x140;
  static constexpr std::uint32_t kFeedbackIdBase = 0x240;

  static CanFrame speedCommand(std::uint8_t motorId, double outputSpeedDps);
  static CanFrame statusQuery(std::uint8_t motorId);
  // 抱闸状态用 0x9A 的 Data[3] 读取（官方协议里没有单独的抱闸查询命令）。
  static CanFrame brakeStatusQuery(std::uint8_t motorId);
  static CanFrame stopCommand(std::uint8_t motorId);
  static CanFrame brakeCommand(std::uint8_t motorId, bool release);
  static CanFrame systemResetCommand(std::uint8_t motorId);
  static std::optional<ServoFeedback> parseFeedback(const CanFrame& frame);
  static std::optional<ServoStatus> parseStatus(const CanFrame& frame);
  static std::optional<bool> parseBrakeStatus(const CanFrame& frame);
  static std::optional<std::int32_t> parseMultiTurnEncoderPosition(
      const CanFrame& frame);
  static std::optional<std::int32_t> parseMultiTurnAngle(
      const CanFrame& frame);
};

}  // namespace crawling
