#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace crawling {

// 物理通道类型：决定界面监视条怎么画，以及映射时按“轴”还是“档位/按键”处理。
enum class RemoteChannelKind {
  Axis,
  Knob,
  Wheel,
  Switch3,
  Button,
};

// 平台语义动作。任何平台都必须能映射到这些动作，代码只认动作不认物理键名。
enum class RemoteAction {
  Enable,
  StartCorrection,
  StopCorrection,
  PauseToggle,
  EmergencyStop,
};

struct RemoteChannel {
  QString key;                       // "T1"
  QString label;                     // "主摇杆1"
  RemoteChannelKind kind = RemoteChannelKind::Axis;
  int offset = 0;                    // 帧内字节偏移（小端 uint16）
  int minimum = 0;
  int maximum = 1200;
  int center = 600;
};

struct RemoteButton {
  QString key;                       // "L1"
  int offset = 0;                    // 帧内字节偏移
  int bit = 0;
  bool activeLow = true;             // 0=按下
};

struct RemoteAxisMapping {
  QString channel;                   // 物理通道 key；空表示未绑定
  int sign = 1;
  bool bound() const { return !channel.trimmed().isEmpty(); }
};

struct RemoteMotorTarget {
  int axis = 0;                      // 0=X 1=Y 2=Z
  int index = 0;                     // 轴内 0 基索引（界面显示为 1 基）
  int sign = 1;                      // 机械装反时设 -1
};

struct RemoteSemanticMapping {
  RemoteAxisMapping linear;                   // drive.linear：左摇杆前后
  RemoteAxisMapping angular;                  // drive.angular：左摇杆左右
  RemoteAxisMapping spacing;                  // probe.spacing：右摇杆左右
  RemoteAxisMapping lift;                     // probe.lift：右摇杆上下
  RemoteMotorTarget spacingMotor;             // 间距电机（X2）
  QVector<RemoteMotorTarget> liftMotors;      // 升降电机（Z1、Z2 同步）
  QString interlockSwitch;                    // mode.interlock：三档开关 key
  QString interlockArmedBand = QStringLiteral("up");
  QString speedSwitch;                        // speed.preset：三档开关 key
  int speedDown = 20;
  int speedMid = 50;
  int speedUp = 80;
  QString buttonEnable;
  QString buttonStartCorrection;
  QString buttonStopCorrection;
  QString buttonPauseToggle;
  QString buttonEmergencyStop;
};

struct RemoteFrameFormat {
  QByteArray request;                // 请求帧（十六进制串解析）
  int requestIntervalMs = 25;
  QByteArray header;                 // 帧头
  int lengthOffset = 2;              // 长度字段偏移（uint16 小端）
  int lengthExtra = 5;               // 整帧长 = 长度字段值 + 5
  int trailer = 0x5F;                // 尾字节
  int answerTypeOffset = 4;
  bool streaming = false;            // 流式上报：不发请求，只读
};

struct RemotePlatformProfile {
  int schemaVersion = 1;
  QString id;
  QString displayName;
  int baudRate = 115200;
  int dataBits = 8;
  QString parity = QStringLiteral("none");
  int stopBits = 1;
  QString defaultPort = QStringLiteral("COM5");
  RemoteFrameFormat frame;
  QVector<RemoteChannel> channels;
  QVector<RemoteButton> buttons;
  RemoteSemanticMapping mapping;

  const RemoteChannel *channel(const QString &key) const;
  const RemoteButton *button(const QString &key) const;
  // 语义键齐全性、帧规则、通道偏移重复等检查；空串表示通过。
  QString validationError() const;
  QJsonObject toJson() const;
  static bool fromJson(const QJsonObject &object, RemotePlatformProfile *out,
                       QString *error);
};

// 平台注册表：内置 HC10pro，其余从 bin/config/remote_platforms/*.json 加载。
// 换品牌只需新增一个 json（通道名、帧规则、按键位、映射），不改 C++。
class RemotePlatformRegistry {
 public:
  static RemotePlatformRegistry &instance();

  void reload();
  QVector<RemotePlatformProfile> profiles() const { return profiles_; }
  QStringList displayNames() const;
  RemotePlatformProfile profile(const QString &id) const;
  QString profileDir() const;
  QString lastError() const { return lastError_; }

  static RemotePlatformProfile builtinHc10Pro();

 private:
  void ensureBuiltinOnDisk() const;
  QVector<RemotePlatformProfile> profiles_;
  QString lastError_;
};

} // namespace crawling
