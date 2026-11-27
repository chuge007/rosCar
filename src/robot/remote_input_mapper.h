#pragma once

#include "remote_channel_codec.h"

#include <QHash>
#include <QObject>

namespace crawling {

// 把平台无关的原始通道样本翻译成“语义意图”：
//   摇杆 → 底盘方向（-1/0/1）、电机点动百分比（±100）、速度档、按键动作。
// 这里只做判定与去抖，不碰任何控制器；输出由 RobotControlPanel 接到现有按钮/
// 滑块上，所以运动互锁（使能、急停、纠偏占用）仍然由原有逻辑负责。
class RemoteInputMapper : public QObject {
  Q_OBJECT
 public:
  explicit RemoteInputMapper(QObject *parent = nullptr);

  void setProfile(const RemotePlatformProfile &profile);
  const RemotePlatformProfile &profile() const { return profile_; }

  // 操作员在弹窗里勾选“接管”后才允许驱动；急停不受此限制。
  void setTakeoverEnabled(bool enabled);
  bool takeoverEnabled() const { return takeover_; }
  // 接管 + 三档模式开关处于允许档位。
  bool armed() const { return armed_; }

  void setDeadzone(int deadzone);
  int deadzone() const { return deadzone_; }

  // 失联/断开/切平台时调用：把所有输出归零并清掉按键边沿状态。
  void reset();

 signals:
  void driveAxesChanged(int linear, int angular);
  void motorJogChanged(int axis, int index, int percent);
  void speedPresetChanged(int percent);
  void driveSpeedStepChanged(int direction); // drive.speed：-1 减一格，+1 加一格
  void actionTriggered(int action); // crawling::RemoteAction
  void armedChanged(bool armed);
  void statusMessage(const QString &message);

 public slots:
  void applySample(const crawling::RemoteChannelSample &sample);

 private:
  int axisLevel(const RemoteChannelSample &sample, const RemoteAxisMapping &mapping,
                int previous) const;
  int axisPercent(const RemoteChannelSample &sample,
                  const RemoteAxisMapping &mapping) const;
  // -2 未绑定，-1 下档，0 中档，1 上档
  int switchBand(const RemoteChannelSample &sample, const QString &key) const;
  void updateArmed(int band);
  void emitMotorJog(int axis, int index, int percent);
  void handleButton(const RemoteChannelSample &sample, const QString &key,
                    RemoteAction action, bool requireTakeover);

  RemotePlatformProfile profile_;
  bool takeover_ = false;
  bool armed_ = false;
  int deadzone_ = 80;
  int lastLinear_ = 0;
  int lastAngular_ = 0;
  int lastSpeedPreset_ = -1;
  int lastDriveSpeedStep_ = 0;
  qint64 lastDriveSpeedTickMs_ = 0;
  int lastInterlockBand_ = -2;
  QHash<QString, bool> lastButton_;
  QHash<QString, int> lastMotorPercent_;
  qint64 lastSampleMs_ = 0;
};

} // namespace crawling
