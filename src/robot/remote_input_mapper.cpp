#include "remote_input_mapper.h"

#include <QDateTime>

namespace crawling {
namespace {

// 三档开关阈值、迟滞、失联时长随平台 profile 走（不同品牌落点不同），
// 这里只保留与平台无关的去重带宽。
constexpr int kPercentDedupeBand = 2;

} // namespace

RemoteInputMapper::RemoteInputMapper(QObject *parent) : QObject(parent) {}

void RemoteInputMapper::setProfile(const RemotePlatformProfile &profile) {
  reset();
  profile_ = profile;
  deadzone_ = qBound(20, profile.tuning.deadzone, 300);
  lastInterlockBand_ = -2;
  lastSpeedPreset_ = -1;
  lastButton_.clear();
}

void RemoteInputMapper::setTakeoverEnabled(bool enabled) {
  if (takeover_ == enabled) return;
  takeover_ = enabled;
  if (!takeover_) reset();
  updateArmed(lastInterlockBand_);
}

void RemoteInputMapper::setDeadzone(int deadzone) {
  deadzone_ = qBound(20, deadzone, 300);
}

void RemoteInputMapper::reset() {
  lastSampleMs_ = 0;
  lastButton_.clear();
  lastMotorPercent_.clear();
  lastSpeedPreset_ = -1;
  if (lastLinear_ != 0 || lastAngular_ != 0) {
    lastLinear_ = 0;
    lastAngular_ = 0;
    emit driveAxesChanged(0, 0);
  }
  for (const auto &motor : profile_.mapping.liftMotors)
    emitMotorJog(motor.axis, motor.index, 0);
  if (profile_.mapping.spacing.bound())
    emitMotorJog(profile_.mapping.spacingMotor.axis,
                 profile_.mapping.spacingMotor.index, 0);
  if (armed_) {
    armed_ = false;
    emit armedChanged(false);
  }
}

int RemoteInputMapper::axisLevel(const RemoteChannelSample &sample,
                                 const RemoteAxisMapping &mapping,
                                 int previous) const {
  if (!mapping.bound()) return 0;
  const RemoteChannel *channel = profile_.channel(mapping.channel);
  if (!channel || !sample.values.contains(channel->key)) return 0;
  const int value = sample.channelValue(channel->key, channel->center);
  const int delta = (value - channel->center) * (mapping.sign < 0 ? -1 : 1);
  const int threshold = deadzone_;
  const int release = qMax(1, deadzone_ - profile_.tuning.hysteresis);
  if (delta > threshold) return 1;
  if (delta < -threshold) return -1;
  if (previous > 0 && delta > release) return 1;
  if (previous < 0 && delta < -release) return -1;
  return 0;
}

int RemoteInputMapper::axisPercent(const RemoteChannelSample &sample,
                                   const RemoteAxisMapping &mapping) const {
  if (!mapping.bound()) return 0;
  const RemoteChannel *channel = profile_.channel(mapping.channel);
  if (!channel || !sample.values.contains(channel->key)) return 0;
  const int value = sample.channelValue(channel->key, channel->center);
  const int delta = (value - channel->center) * (mapping.sign < 0 ? -1 : 1);
  const int magnitude = qAbs(delta);
  if (magnitude <= deadzone_) return 0;
  const int span = qMax(1, (channel->maximum - channel->center) - deadzone_);
  int percent = (magnitude - deadzone_) * 100 / span;
  percent = qBound(0, percent, 100);
  return delta > 0 ? percent : -percent;
}

int RemoteInputMapper::switchBand(const RemoteChannelSample &sample,
                                  const QString &key) const {
  if (key.trimmed().isEmpty()) return -2;
  const RemoteChannel *channel = profile_.channel(key);
  if (!channel || !sample.values.contains(channel->key)) return -2;
  const int value = sample.channelValue(channel->key, channel->center);
  if (value <= profile_.tuning.switchLowMax) return -1;
  if (value >= profile_.tuning.switchHighMin) return 1;
  return 0;
}

void RemoteInputMapper::updateArmed(int band) {
  int required = 1;
  if (profile_.mapping.interlockArmedBand == QStringLiteral("down"))
    required = -1;
  else if (profile_.mapping.interlockArmedBand == QStringLiteral("mid"))
    required = 0;
  const bool armed = takeover_ && band == required;
  if (armed == armed_) return;
  armed_ = armed;
  emit armedChanged(armed);
  if (!armed_) {
    if (lastLinear_ != 0 || lastAngular_ != 0) {
      lastLinear_ = 0;
      lastAngular_ = 0;
      emit driveAxesChanged(0, 0);
    }
    for (const auto &motor : profile_.mapping.liftMotors)
      emitMotorJog(motor.axis, motor.index, 0);
    if (profile_.mapping.spacing.bound())
      emitMotorJog(profile_.mapping.spacingMotor.axis,
                   profile_.mapping.spacingMotor.index, 0);
  }
}

void RemoteInputMapper::emitMotorJog(int axis, int index, int percent) {
  const QString key = QStringLiteral("%1:%2").arg(axis).arg(index);
  const int previous = lastMotorPercent_.value(key, 0);
  if (percent == previous) return;
  if (percent != 0 && qAbs(percent - previous) < kPercentDedupeBand && previous != 0)
    return;
  lastMotorPercent_.insert(key, percent);
  emit motorJogChanged(axis, index, percent);
}

void RemoteInputMapper::handleButton(const RemoteChannelSample &sample,
                                     const QString &key, RemoteAction action,
                                     bool requireTakeover) {
  if (key.trimmed().isEmpty()) return;
  const RemoteButton *button = profile_.button(key);
  if (!button || !sample.buttons.contains(button->key)) return;
  const bool pressed = sample.buttonPressed(button->key);
  if (lastButton_.value(button->key, false) == pressed) return;
  lastButton_.insert(button->key, pressed);
  if (!pressed) return;
  if (requireTakeover && !takeover_) return;
  emit actionTriggered(static_cast<int>(action));
}

void RemoteInputMapper::applySample(const RemoteChannelSample &sample) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (lastSampleMs_ > 0 && now - lastSampleMs_ > profile_.tuning.staleMs) reset();
  lastSampleMs_ = now;

  const int band = switchBand(sample, profile_.mapping.interlockSwitch);
  if (band != lastInterlockBand_) {
    lastInterlockBand_ = band;
    updateArmed(band);
    if (band == -2 && takeover_)
      emit statusMessage(QStringLiteral("平台未绑定模式开关，接管保持常开"));
  }

  // 急停永远生效，不受接管与档位限制。
  handleButton(sample, profile_.mapping.buttonEmergencyStop, RemoteAction::EmergencyStop,
               false);
  handleButton(sample, profile_.mapping.buttonEnable, RemoteAction::Enable, true);
  handleButton(sample, profile_.mapping.buttonStartCorrection,
               RemoteAction::StartCorrection, true);
  handleButton(sample, profile_.mapping.buttonStopCorrection,
               RemoteAction::StopCorrection, true);
  handleButton(sample, profile_.mapping.buttonPauseToggle, RemoteAction::PauseToggle,
               true);

  if (profile_.mapping.speedSwitch.trimmed().isEmpty() == false) {
    const int speedBand = switchBand(sample, profile_.mapping.speedSwitch);
    const int preset = speedBand > 0   ? profile_.mapping.speedUp
                       : speedBand < 0 ? profile_.mapping.speedDown
                                       : profile_.mapping.speedMid;
    if (speedBand != -2 && preset != lastSpeedPreset_) {
      lastSpeedPreset_ = preset;
      emit speedPresetChanged(preset);
    }
  }

  if (!armed_) return;

  const int linear = axisLevel(sample, profile_.mapping.linear, lastLinear_);
  const int angular = axisLevel(sample, profile_.mapping.angular, lastAngular_);
  if (linear != lastLinear_ || angular != lastAngular_) {
    lastLinear_ = linear;
    lastAngular_ = angular;
    emit driveAxesChanged(linear, angular);
  }

  if (profile_.mapping.spacing.bound()) {
    const int percent = axisPercent(sample, profile_.mapping.spacing) *
                        (profile_.mapping.spacingMotor.sign < 0 ? -1 : 1);
    emitMotorJog(profile_.mapping.spacingMotor.axis,
                 profile_.mapping.spacingMotor.index, percent);
  }
  if (profile_.mapping.lift.bound()) {
    const int base = axisPercent(sample, profile_.mapping.lift);
    for (const auto &motor : profile_.mapping.liftMotors)
      emitMotorJog(motor.axis, motor.index, base * (motor.sign < 0 ? -1 : 1));
  }
}

} // namespace crawling
