#include "remote_platform.h"

#include "robot_app_logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

namespace crawling {
namespace {

QByteArray hexToBytes(const QString &text) {
  QString cleaned = text;
  cleaned.remove(QLatin1Char(' '));
  cleaned.remove(QLatin1Char('\t'));
  cleaned.remove(QLatin1Char('-'));
  if (cleaned.size() % 2 != 0) return {};
  return QByteArray::fromHex(cleaned.toLatin1());
}

QString channelKindToString(RemoteChannelKind kind) {
  switch (kind) {
    case RemoteChannelKind::Knob: return QStringLiteral("knob");
    case RemoteChannelKind::Wheel: return QStringLiteral("wheel");
    case RemoteChannelKind::Switch3: return QStringLiteral("switch3");
    case RemoteChannelKind::Button: return QStringLiteral("button");
    case RemoteChannelKind::Axis:
    default: return QStringLiteral("axis");
  }
}

RemoteChannelKind channelKindFromString(const QString &text) {
  const QString value = text.trimmed().toLower();
  if (value == QStringLiteral("knob")) return RemoteChannelKind::Knob;
  if (value == QStringLiteral("wheel")) return RemoteChannelKind::Wheel;
  if (value == QStringLiteral("switch3")) return RemoteChannelKind::Switch3;
  if (value == QStringLiteral("button")) return RemoteChannelKind::Button;
  return RemoteChannelKind::Axis;
}

QString checksumKindToString(RemoteChecksumKind kind) {
  switch (kind) {
    case RemoteChecksumKind::Sum8: return QStringLiteral("sum8");
    case RemoteChecksumKind::Xor8: return QStringLiteral("xor8");
    case RemoteChecksumKind::Crc8: return QStringLiteral("crc8");
    case RemoteChecksumKind::Crc16Modbus: return QStringLiteral("crc16modbus");
    case RemoteChecksumKind::None:
    default: return QStringLiteral("none");
  }
}

RemoteChecksumKind checksumKindFromString(const QString &text) {
  const QString value = text.trimmed().toLower();
  if (value == QStringLiteral("sum8")) return RemoteChecksumKind::Sum8;
  if (value == QStringLiteral("xor8")) return RemoteChecksumKind::Xor8;
  if (value == QStringLiteral("crc8")) return RemoteChecksumKind::Crc8;
  if (value == QStringLiteral("crc16modbus")) return RemoteChecksumKind::Crc16Modbus;
  return RemoteChecksumKind::None;
}

RemoteChannel readChannel(const QJsonObject &object) {
  RemoteChannel channel;
  channel.key = object.value(QStringLiteral("key")).toString().trimmed();
  channel.label = object.value(QStringLiteral("label")).toString();
  channel.kind = channelKindFromString(
      object.value(QStringLiteral("kind")).toString(QStringLiteral("axis")));
  channel.offset = object.value(QStringLiteral("offset")).toInt(0);
  channel.valueSize = object.value(QStringLiteral("valueSize")).toInt(2) == 1 ? 1 : 2;
  channel.bigEndian = object.value(QStringLiteral("bigEndian")).toBool(false);
  channel.minimum = object.value(QStringLiteral("min")).toInt(0);
  channel.maximum = object.value(QStringLiteral("max")).toInt(1200);
  channel.center = object.value(QStringLiteral("center")).toInt(
      (channel.minimum + channel.maximum) / 2);
  return channel;
}

RemoteButton readButton(const QJsonObject &object) {
  RemoteButton button;
  button.key = object.value(QStringLiteral("key")).toString().trimmed();
  button.offset = object.value(QStringLiteral("offset")).toInt(0);
  button.bit = object.value(QStringLiteral("bit")).toInt(0);
  button.activeLow = object.value(QStringLiteral("activeLow")).toBool(true);
  return button;
}

RemoteAxisMapping readAxisMapping(const QJsonObject &object) {
  RemoteAxisMapping mapping;
  mapping.channel = object.value(QStringLiteral("channel")).toString().trimmed();
  mapping.sign = object.value(QStringLiteral("sign")).toInt(1) < 0 ? -1 : 1;
  return mapping;
}

RemoteMotorTarget readMotorTarget(const QJsonObject &object) {
  RemoteMotorTarget target;
  target.axis = object.value(QStringLiteral("axis")).toInt(0);
  target.index = object.value(QStringLiteral("index")).toInt(0);
  target.sign = object.value(QStringLiteral("sign")).toInt(1) < 0 ? -1 : 1;
  return target;
}

QJsonObject axisMappingToJson(const RemoteAxisMapping &mapping) {
  QJsonObject object;
  object.insert(QStringLiteral("channel"), mapping.channel);
  object.insert(QStringLiteral("sign"), mapping.sign);
  return object;
}

QJsonObject motorTargetToJson(const RemoteMotorTarget &target) {
  QJsonObject object;
  object.insert(QStringLiteral("axis"), target.axis);
  object.insert(QStringLiteral("index"), target.index);
  object.insert(QStringLiteral("sign"), target.sign);
  return object;
}

void appendChannel(RemotePlatformProfile *profile, const QString &key,
                   const QString &label, RemoteChannelKind kind, int offset) {
  RemoteChannel channel;
  channel.key = key;
  channel.label = label;
  channel.kind = kind;
  channel.offset = offset;
  channel.minimum = 0;
  channel.maximum = 1200;
  channel.center = 600;
  profile->channels.append(channel);
}

void appendButton(RemotePlatformProfile *profile, const QString &key, int offset,
                  int bit) {
  RemoteButton button;
  button.key = key;
  button.offset = offset;
  button.bit = bit;
  button.activeLow = true;
  profile->buttons.append(button);
}

} // namespace

const RemoteChannel *RemotePlatformProfile::channel(const QString &key) const {
  for (const auto &channel : channels) {
    if (channel.key.compare(key, Qt::CaseInsensitive) == 0) return &channel;
  }
  return nullptr;
}

const RemoteButton *RemotePlatformProfile::button(const QString &key) const {
  for (const auto &button : buttons) {
    if (button.key.compare(key, Qt::CaseInsensitive) == 0) return &button;
  }
  return nullptr;
}

QString RemotePlatformProfile::validationError() const {
  if (id.trimmed().isEmpty()) return QStringLiteral("平台 id 为空");
  if (displayName.trimmed().isEmpty()) return QStringLiteral("平台显示名为空");
  if (frame.header.size() < 2) return QStringLiteral("帧头长度不足");
  if (frame.fixedFrameLength > 0) {
    if (frame.fixedFrameLength < frame.header.size() + 1)
      return QStringLiteral("定长帧长度小于帧头");
  } else if (frame.lengthFieldOffset < 0) {
    return QStringLiteral("既没有定长也没有长度字段");
  } else if (frame.lengthExtra <= 0) {
    return QStringLiteral("帧长规则 lengthExtra 非法");
  }
  if (frame.checksum != RemoteChecksumKind::None &&
      frame.checksumOffsetFromEnd < 0)
    return QStringLiteral("校验字段位置非法");
  if (channels.isEmpty()) return QStringLiteral("未定义任何物理通道");

  QSet<QString> keys;
  for (const auto &channel : channels) {
    if (channel.key.isEmpty()) return QStringLiteral("存在无 key 的物理通道");
    if (keys.contains(channel.key))
      return QStringLiteral("物理通道 key 重复：%1").arg(channel.key);
    keys.insert(channel.key);
    if (channel.maximum <= channel.minimum)
      return QStringLiteral("物理通道 %1 的值域非法").arg(channel.key);
  }
  for (const auto &button : buttons) {
    if (button.key.isEmpty()) return QStringLiteral("存在无 key 的按键");
    if (button.bit < 0 || button.bit > 7)
      return QStringLiteral("按键 %1 的 bit 越界").arg(button.key);
  }

  const auto checkChannel = [this](const RemoteAxisMapping &mapping,
                                   const QString &semantic) {
    if (!mapping.bound()) return QString();
    if (!channel(mapping.channel))
      return QStringLiteral("语义 %1 引用了不存在的通道 %2")
          .arg(semantic, mapping.channel);
    return QString();
  };
  const QVector<QPair<QString, RemoteAxisMapping>> axisMappings{
      {QStringLiteral("drive.linear"), mapping.linear},
      {QStringLiteral("drive.angular"), mapping.angular},
      {QStringLiteral("probe.spacing"), mapping.spacing},
      {QStringLiteral("probe.lift"), mapping.lift}};
  for (const auto &pair : axisMappings) {
    const QString error = checkChannel(pair.second, pair.first);
    if (!error.isEmpty()) return error;
  }
  const QVector<QPair<QString, QString>> switchMappings{
      {QStringLiteral("mode.interlock"), mapping.interlockSwitch},
      {QStringLiteral("speed.preset"), mapping.speedSwitch}};
  for (const auto &pair : switchMappings) {
    if (pair.second.isEmpty()) continue;
    if (!channel(pair.second))
      return QStringLiteral("语义 %1 引用了不存在的开关 %2")
          .arg(pair.first, pair.second);
  }
  const QVector<QPair<QString, QString>> buttonMappings{
      {QStringLiteral("action.enable"), mapping.buttonEnable},
      {QStringLiteral("action.startCorrection"), mapping.buttonStartCorrection},
      {QStringLiteral("action.stopCorrection"), mapping.buttonStopCorrection},
      {QStringLiteral("action.pauseToggle"), mapping.buttonPauseToggle},
      {QStringLiteral("action.emergencyStop"), mapping.buttonEmergencyStop}};
  for (const auto &pair : buttonMappings) {
    if (pair.second.isEmpty()) continue;
    if (!button(pair.second))
      return QStringLiteral("语义 %1 引用了不存在的按键 %2")
          .arg(pair.first, pair.second);
  }
  if (mapping.lift.bound() && mapping.liftMotors.isEmpty())
    return QStringLiteral("probe.lift 未指定目标电机");
  return QString();
}

QJsonObject RemotePlatformProfile::toJson() const {
  QJsonObject root;
  root.insert(QStringLiteral("schemaVersion"), schemaVersion);
  root.insert(QStringLiteral("id"), id);
  root.insert(QStringLiteral("displayName"), displayName);

  QJsonObject serial;
  serial.insert(QStringLiteral("baud"), baudRate);
  serial.insert(QStringLiteral("dataBits"), dataBits);
  serial.insert(QStringLiteral("parity"), parity);
  serial.insert(QStringLiteral("stopBits"), stopBits);
  serial.insert(QStringLiteral("defaultPort"), defaultPort);
  root.insert(QStringLiteral("serial"), serial);

  QJsonObject frameObject;
  frameObject.insert(QStringLiteral("request"),
                     QString::fromLatin1(frame.request.toHex(' ').toUpper()));
  frameObject.insert(QStringLiteral("requestIntervalMs"), frame.requestIntervalMs);
  frameObject.insert(QStringLiteral("header"),
                     QString::fromLatin1(frame.header.toHex(' ').toUpper()));
  frameObject.insert(QStringLiteral("lengthOffset"), frame.lengthFieldOffset);
  frameObject.insert(QStringLiteral("lengthSize"), frame.lengthFieldSize);
  frameObject.insert(QStringLiteral("lengthBigEndian"), frame.lengthBigEndian);
  frameObject.insert(QStringLiteral("lengthExtra"), frame.lengthExtra);
  frameObject.insert(QStringLiteral("fixedLength"), frame.fixedFrameLength);
  frameObject.insert(
      QStringLiteral("trailer"),
      frame.trailer < 0
          ? QString()
          : QString::fromLatin1(
                QByteArray(1, static_cast<char>(frame.trailer)).toHex().toUpper()));
  frameObject.insert(QStringLiteral("answerTypeOffset"), frame.answerTypeOffset);
  frameObject.insert(QStringLiteral("answerType"), frame.answerTypeValue);
  frameObject.insert(QStringLiteral("checksum"), checksumKindToString(frame.checksum));
  frameObject.insert(QStringLiteral("checksumOffsetFromEnd"),
                     frame.checksumOffsetFromEnd);
  frameObject.insert(QStringLiteral("checksumBigEndian"), frame.checksumBigEndian);
  frameObject.insert(QStringLiteral("checksumCoversHeader"),
                     frame.checksumCoversHeader);
  frameObject.insert(QStringLiteral("streaming"), frame.streaming);
  root.insert(QStringLiteral("frame"), frameObject);

  QJsonObject tuningObject;
  tuningObject.insert(QStringLiteral("deadzone"), tuning.deadzone);
  tuningObject.insert(QStringLiteral("hysteresis"), tuning.hysteresis);
  tuningObject.insert(QStringLiteral("switchLowMax"), tuning.switchLowMax);
  tuningObject.insert(QStringLiteral("switchHighMin"), tuning.switchHighMin);
  tuningObject.insert(QStringLiteral("staleMs"), tuning.staleMs);
  root.insert(QStringLiteral("tuning"), tuningObject);

  QJsonArray channelArray;
  for (const auto &channel : channels) {
    QJsonObject object;
    object.insert(QStringLiteral("key"), channel.key);
    object.insert(QStringLiteral("label"), channel.label);
    object.insert(QStringLiteral("kind"), channelKindToString(channel.kind));
    object.insert(QStringLiteral("offset"), channel.offset);
    object.insert(QStringLiteral("valueSize"), channel.valueSize);
    object.insert(QStringLiteral("bigEndian"), channel.bigEndian);
    object.insert(QStringLiteral("min"), channel.minimum);
    object.insert(QStringLiteral("max"), channel.maximum);
    object.insert(QStringLiteral("center"), channel.center);
    channelArray.append(object);
  }
  root.insert(QStringLiteral("channels"), channelArray);

  QJsonArray buttonArray;
  for (const auto &button : buttons) {
    QJsonObject object;
    object.insert(QStringLiteral("key"), button.key);
    object.insert(QStringLiteral("offset"), button.offset);
    object.insert(QStringLiteral("bit"), button.bit);
    object.insert(QStringLiteral("activeLow"), button.activeLow);
    buttonArray.append(object);
  }
  root.insert(QStringLiteral("buttons"), buttonArray);

  QJsonObject mappingObject;
  mappingObject.insert(QStringLiteral("drive.linear"), axisMappingToJson(mapping.linear));
  mappingObject.insert(QStringLiteral("drive.angular"),
                       axisMappingToJson(mapping.angular));
  QJsonObject spacing = axisMappingToJson(mapping.spacing);
  spacing.insert(QStringLiteral("motor"), motorTargetToJson(mapping.spacingMotor));
  mappingObject.insert(QStringLiteral("probe.spacing"), spacing);
  QJsonObject lift = axisMappingToJson(mapping.lift);
  QJsonArray liftMotors;
  for (const auto &motor : mapping.liftMotors)
    liftMotors.append(motorTargetToJson(motor));
  lift.insert(QStringLiteral("motors"), liftMotors);
  mappingObject.insert(QStringLiteral("probe.lift"), lift);

  QJsonObject interlock;
  interlock.insert(QStringLiteral("switch"), mapping.interlockSwitch);
  interlock.insert(QStringLiteral("armed"), mapping.interlockArmedBand);
  mappingObject.insert(QStringLiteral("mode.interlock"), interlock);

  QJsonObject speed;
  speed.insert(QStringLiteral("switch"), mapping.speedSwitch);
  speed.insert(QStringLiteral("down"), mapping.speedDown);
  speed.insert(QStringLiteral("mid"), mapping.speedMid);
  speed.insert(QStringLiteral("up"), mapping.speedUp);
  mappingObject.insert(QStringLiteral("speed.preset"), speed);

  const auto insertButton = [&mappingObject](const QString &key, const QString &value) {
    QJsonObject object;
    object.insert(QStringLiteral("button"), value);
    mappingObject.insert(key, object);
  };
  insertButton(QStringLiteral("action.enable"), mapping.buttonEnable);
  insertButton(QStringLiteral("action.startCorrection"), mapping.buttonStartCorrection);
  insertButton(QStringLiteral("action.stopCorrection"), mapping.buttonStopCorrection);
  insertButton(QStringLiteral("action.pauseToggle"), mapping.buttonPauseToggle);
  insertButton(QStringLiteral("action.emergencyStop"), mapping.buttonEmergencyStop);

  root.insert(QStringLiteral("mapping"), mappingObject);
  return root;
}

bool RemotePlatformProfile::fromJson(const QJsonObject &object,
                                     RemotePlatformProfile *out, QString *error) {
  if (!out) return false;
  RemotePlatformProfile profile;
  profile.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(1);
  profile.id = object.value(QStringLiteral("id")).toString().trimmed();
  profile.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
  if (profile.displayName.isEmpty()) profile.displayName = profile.id;

  const QJsonObject serial = object.value(QStringLiteral("serial")).toObject();
  profile.baudRate = serial.value(QStringLiteral("baud")).toInt(115200);
  profile.dataBits = serial.value(QStringLiteral("dataBits")).toInt(8);
  profile.parity = serial.value(QStringLiteral("parity")).toString(QStringLiteral("none"));
  profile.stopBits = serial.value(QStringLiteral("stopBits")).toInt(1);
  profile.defaultPort =
      serial.value(QStringLiteral("defaultPort")).toString(QStringLiteral("COM5"));

  const QJsonObject frame = object.value(QStringLiteral("frame")).toObject();
  profile.frame.request =
      hexToBytes(frame.value(QStringLiteral("request")).toString());
  profile.frame.requestIntervalMs =
      qBound(5, frame.value(QStringLiteral("requestIntervalMs")).toInt(25), 1000);
  profile.frame.header = hexToBytes(frame.value(QStringLiteral("header")).toString());
  profile.frame.lengthFieldOffset =
      frame.value(QStringLiteral("lengthOffset")).toInt(2);
  profile.frame.lengthFieldSize =
      frame.value(QStringLiteral("lengthSize")).toInt(2) == 1 ? 1 : 2;
  profile.frame.lengthBigEndian =
      frame.value(QStringLiteral("lengthBigEndian")).toBool(false);
  profile.frame.lengthExtra = frame.value(QStringLiteral("lengthExtra")).toInt(5);
  profile.frame.fixedFrameLength =
      qMax(0, frame.value(QStringLiteral("fixedLength")).toInt(0));
  // trailer 未写 = 沿用默认 0x5F；显式写空串 = 该品牌没有尾字节。
  if (frame.contains(QStringLiteral("trailer"))) {
    const QByteArray trailer =
        hexToBytes(frame.value(QStringLiteral("trailer")).toString());
    profile.frame.trailer =
        trailer.isEmpty() ? -1 : static_cast<unsigned char>(trailer.at(0));
  }
  profile.frame.answerTypeOffset =
      frame.value(QStringLiteral("answerTypeOffset")).toInt(4);
  profile.frame.answerTypeValue = frame.value(QStringLiteral("answerType")).toInt(-1);
  profile.frame.checksum = checksumKindFromString(
      frame.value(QStringLiteral("checksum")).toString(QStringLiteral("none")));
  profile.frame.checksumOffsetFromEnd =
      qMax(0, frame.value(QStringLiteral("checksumOffsetFromEnd")).toInt(2));
  profile.frame.checksumBigEndian =
      frame.value(QStringLiteral("checksumBigEndian")).toBool(false);
  profile.frame.checksumCoversHeader =
      frame.value(QStringLiteral("checksumCoversHeader")).toBool(true);
  profile.frame.streaming = frame.value(QStringLiteral("streaming")).toBool(false);

  const QJsonObject tuning = object.value(QStringLiteral("tuning")).toObject();
  profile.tuning.deadzone =
      qBound(20, tuning.value(QStringLiteral("deadzone")).toInt(80), 300);
  profile.tuning.hysteresis =
      qBound(1, tuning.value(QStringLiteral("hysteresis")).toInt(20), 100);
  profile.tuning.switchLowMax =
      tuning.value(QStringLiteral("switchLowMax")).toInt(400);
  profile.tuning.switchHighMin =
      tuning.value(QStringLiteral("switchHighMin")).toInt(800);
  profile.tuning.staleMs =
      qBound(60, tuning.value(QStringLiteral("staleMs")).toInt(300), 5000);

  for (const auto &value : object.value(QStringLiteral("channels")).toArray())
    profile.channels.append(readChannel(value.toObject()));
  for (const auto &value : object.value(QStringLiteral("buttons")).toArray())
    profile.buttons.append(readButton(value.toObject()));

  const QJsonObject mapping = object.value(QStringLiteral("mapping")).toObject();
  profile.mapping.linear =
      readAxisMapping(mapping.value(QStringLiteral("drive.linear")).toObject());
  profile.mapping.angular =
      readAxisMapping(mapping.value(QStringLiteral("drive.angular")).toObject());

  const QJsonObject spacing = mapping.value(QStringLiteral("probe.spacing")).toObject();
  profile.mapping.spacing = readAxisMapping(spacing);
  if (spacing.contains(QStringLiteral("motor")))
    profile.mapping.spacingMotor =
        readMotorTarget(spacing.value(QStringLiteral("motor")).toObject());

  const QJsonObject lift = mapping.value(QStringLiteral("probe.lift")).toObject();
  profile.mapping.lift = readAxisMapping(lift);
  for (const auto &value : lift.value(QStringLiteral("motors")).toArray())
    profile.mapping.liftMotors.append(readMotorTarget(value.toObject()));

  const QJsonObject interlock = mapping.value(QStringLiteral("mode.interlock")).toObject();
  profile.mapping.interlockSwitch =
      interlock.value(QStringLiteral("switch")).toString().trimmed();
  const QString armed =
      interlock.value(QStringLiteral("armed")).toString().trimmed().toLower();
  if (armed == QStringLiteral("down") || armed == QStringLiteral("mid") ||
      armed == QStringLiteral("up"))
    profile.mapping.interlockArmedBand = armed;

  const QJsonObject speed = mapping.value(QStringLiteral("speed.preset")).toObject();
  profile.mapping.speedSwitch =
      speed.value(QStringLiteral("switch")).toString().trimmed();
  profile.mapping.speedDown =
      qBound(5, speed.value(QStringLiteral("down")).toInt(20), 100);
  profile.mapping.speedMid =
      qBound(5, speed.value(QStringLiteral("mid")).toInt(50), 100);
  profile.mapping.speedUp =
      qBound(5, speed.value(QStringLiteral("up")).toInt(80), 100);

  const auto readButtonKey = [&mapping](const QString &key) {
    return mapping.value(key).toObject()
        .value(QStringLiteral("button")).toString().trimmed();
  };
  profile.mapping.buttonEnable = readButtonKey(QStringLiteral("action.enable"));
  profile.mapping.buttonStartCorrection =
      readButtonKey(QStringLiteral("action.startCorrection"));
  profile.mapping.buttonStopCorrection =
      readButtonKey(QStringLiteral("action.stopCorrection"));
  profile.mapping.buttonPauseToggle =
      readButtonKey(QStringLiteral("action.pauseToggle"));
  profile.mapping.buttonEmergencyStop =
      readButtonKey(QStringLiteral("action.emergencyStop"));

  const QString validation = profile.validationError();
  if (!validation.isEmpty()) {
    if (error) *error = validation;
    return false;
  }
  *out = profile;
  return true;
}

RemotePlatformProfile RemotePlatformRegistry::builtinHc10Pro() {
  RemotePlatformProfile profile;
  profile.id = QStringLiteral("hc10pro");
  profile.displayName = QStringLiteral("HC10pro 手持地面站");
  profile.baudRate = 115200;
  profile.dataBits = 8;
  profile.parity = QStringLiteral("none");
  profile.stopBits = 1;
  profile.defaultPort = QStringLiteral("COM5");

  // 40 字节物理通道应答帧：FE A5 | len(2) | type | 16 个 uint16 通道 | 位域 2 字节 | 5F
  profile.frame.request = QByteArray::fromHex("FEE50100005F");
  profile.frame.requestIntervalMs = 25;
  profile.frame.header = QByteArray::fromHex("FEA5");
  profile.frame.lengthFieldOffset = 2;
  profile.frame.lengthFieldSize = 2;
  profile.frame.lengthBigEndian = false;
  profile.frame.lengthExtra = 5;
  profile.frame.fixedFrameLength = 0;
  profile.frame.trailer = 0x5F;
  profile.frame.answerTypeOffset = 4;
  profile.frame.answerTypeValue = -1;
  profile.frame.checksum = RemoteChecksumKind::None;
  profile.frame.streaming = false;

  for (int i = 0; i < 8; ++i) {
    const bool primary = i < 4;
    appendChannel(&profile, QStringLiteral("T%1").arg(i + 1),
                  primary ? QStringLiteral("主摇杆%1").arg(i + 1)
                          : QStringLiteral("副摇杆%1").arg(i - 3),
                  RemoteChannelKind::Axis, 5 + i * 2);
  }
  appendChannel(&profile, QStringLiteral("LS"), QStringLiteral("左旋钮"),
                RemoteChannelKind::Knob, 21);
  appendChannel(&profile, QStringLiteral("RS"), QStringLiteral("右旋钮"),
                RemoteChannelKind::Knob, 23);
  appendChannel(&profile, QStringLiteral("LC"), QStringLiteral("左滚轮"),
                RemoteChannelKind::Wheel, 25);
  appendChannel(&profile, QStringLiteral("RC"), QStringLiteral("右滚轮"),
                RemoteChannelKind::Wheel, 27);
  const char *switchNames[] = {"SA", "SB", "SC", "SD"};
  for (int i = 0; i < 4; ++i)
    appendChannel(&profile, QString::fromLatin1(switchNames[i]),
                  QStringLiteral("三档开关%1").arg(QChar('A' + i)),
                  RemoteChannelKind::Switch3, 29 + i * 2);

  // 偏移 37：bit0~3 = L1~L4，bit4~7 = R1~R4；偏移 38：bit0 = RETURN。0=按下。
  for (int i = 0; i < 4; ++i)
    appendButton(&profile, QStringLiteral("L%1").arg(i + 1), 37, i);
  for (int i = 0; i < 4; ++i)
    appendButton(&profile, QStringLiteral("R%1").arg(i + 1), 37, 4 + i);
  appendButton(&profile, QStringLiteral("RETURN"), 38, 0);

  profile.mapping.linear.channel = QStringLiteral("T2");
  profile.mapping.linear.sign = 1;
  profile.mapping.angular.channel = QStringLiteral("T1");
  profile.mapping.angular.sign = 1;
  profile.mapping.spacing.channel = QStringLiteral("T3");
  profile.mapping.spacing.sign = 1;
  profile.mapping.spacingMotor.axis = 0;
  profile.mapping.spacingMotor.index = 1;
  profile.mapping.spacingMotor.sign = 1;
  profile.mapping.lift.channel = QStringLiteral("T4");
  profile.mapping.lift.sign = 1;
  profile.mapping.liftMotors.append(RemoteMotorTarget{2, 0, 1});
  profile.mapping.liftMotors.append(RemoteMotorTarget{2, 1, 1});
  profile.mapping.interlockSwitch = QStringLiteral("SA");
  profile.mapping.interlockArmedBand = QStringLiteral("up");
  profile.mapping.speedSwitch = QStringLiteral("SB");
  profile.mapping.speedDown = 20;
  profile.mapping.speedMid = 50;
  profile.mapping.speedUp = 80;
  profile.mapping.buttonEnable = QStringLiteral("R4");
  profile.mapping.buttonStartCorrection = QStringLiteral("R1");
  profile.mapping.buttonStopCorrection = QStringLiteral("R2");
  profile.mapping.buttonPauseToggle = QStringLiteral("R3");
  profile.mapping.buttonEmergencyStop = QStringLiteral("RETURN");
  return profile;
}

RemotePlatformRegistry &RemotePlatformRegistry::instance() {
  static RemotePlatformRegistry registry;
  return registry;
}

QString RemotePlatformRegistry::profileDir() const {
  const QDir base(QCoreApplication::applicationDirPath());
  const QString path = base.filePath(QStringLiteral("config/remote_platforms"));
  QDir().mkpath(path);
  return path;
}

void RemotePlatformRegistry::ensureBuiltinOnDisk() const {
  const QDir dir(profileDir());
  const QString path = dir.filePath(QStringLiteral("hc10pro.json"));
  if (!QFileInfo::exists(path)) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      file.write(
          QJsonDocument(builtinHc10Pro().toJson()).toJson(QJsonDocument::Indented));
      file.close();
    }
  }

  // 新品牌模板：后缀是 .json.txt，不会被 reload 的 *.json 扫到，复制改名即可用。
  const QString templatePath = dir.filePath(QStringLiteral("generic_template.json.txt"));
  if (QFileInfo::exists(templatePath)) return;
  static const char kTemplate[] = R"JSON({
  "_comment": "外部遥控器平台模板。复制本文件为 <品牌id>.json（去掉 .txt）后重启或点“刷新”，即出现在平台下拉框。以下字段全部可选，只写该品牌实际存在的项。",
  "schemaVersion": 1,
  "id": "mybrand",
  "displayName": "我的遥控器",
  "serial": {
    "baud": 115200,
    "dataBits": 8,
    "parity": "none",
    "stopBits": 1,
    "defaultPort": "COM5"
  },
  "frame": {
    "_comment": "帧长三种写法：fixedLength>0 定长；lengthOffset/lengthSize/lengthExtra 变长；都没有则判为非法。trailer 写空串表示无尾字节，不写默认 5F。answerTypeOffset 写 -1 表示没有类型字节。checksum 可选 none/sum8/xor8/crc8/crc16modbus。streaming=true 表示设备主动上报、不发请求。",
    "request": "FE E5 01 00 00 5F",
    "requestIntervalMs": 25,
    "header": "FE A5",
    "lengthOffset": 2,
    "lengthSize": 2,
    "lengthBigEndian": false,
    "lengthExtra": 5,
    "fixedLength": 0,
    "trailer": "5F",
    "answerTypeOffset": 4,
    "answerType": -1,
    "checksum": "none",
    "checksumOffsetFromEnd": 2,
    "checksumBigEndian": false,
    "checksumCoversHeader": true,
    "streaming": false
  },
  "tuning": {
    "_comment": "deadzone/hysteresis 用原始通道值；switchLowMax/switchHighMin 决定三档开关落点；staleMs 是失联判定时长。",
    "deadzone": 80,
    "hysteresis": 20,
    "switchLowMax": 400,
    "switchHighMin": 800,
    "staleMs": 300
  },
  "channels": [
    { "key": "T1", "label": "左摇杆左右", "kind": "axis", "offset": 5, "valueSize": 2, "bigEndian": false, "min": 0, "max": 1200, "center": 600 },
    { "key": "T2", "label": "左摇杆前后", "kind": "axis", "offset": 7, "valueSize": 2, "bigEndian": false, "min": 0, "max": 1200, "center": 600 },
    { "key": "SA", "label": "模式开关", "kind": "switch3", "offset": 29, "valueSize": 2, "bigEndian": false, "min": 0, "max": 1200, "center": 600 }
  ],
  "buttons": [
    { "key": "B1", "offset": 37, "bit": 0, "activeLow": true }
  ],
  "mapping": {
    "_comment": "语义键固定这几个；某品牌没有的控件就留空（不写或写空串）。channel/button 填上面的 key。",
    "drive.linear": { "channel": "T2", "sign": 1 },
    "drive.angular": { "channel": "T1", "sign": 1 },
    "probe.spacing": { "channel": "", "sign": 1, "motor": { "axis": 0, "index": 1, "sign": 1 } },
    "probe.lift": { "channel": "", "sign": 1, "motors": [] },
    "mode.interlock": { "switch": "SA", "armed": "up" },
    "speed.preset": { "switch": "", "down": 20, "mid": 50, "up": 80 },
    "action.enable": { "button": "" },
    "action.startCorrection": { "button": "" },
    "action.stopCorrection": { "button": "" },
    "action.pauseToggle": { "button": "" },
    "action.emergencyStop": { "button": "B1" }
  }
}
)JSON";
  QFile templateFile(templatePath);
  if (!templateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
  templateFile.write(kTemplate);
  templateFile.close();
}

void RemotePlatformRegistry::reload() {
  profiles_.clear();
  lastError_.clear();
  profiles_.append(builtinHc10Pro());
  ensureBuiltinOnDisk();

  const QDir dir(profileDir());
  const auto files =
      dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  for (const auto &info : files) {
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
      lastError_ = QStringLiteral("无法读取 %1").arg(info.fileName());
      continue;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    RemotePlatformProfile profile;
    QString error;
    if (parseError.error != QJsonParseError::NoError) {
      lastError_ = QStringLiteral("%1 解析失败：%2")
                       .arg(info.fileName(), parseError.errorString());
      AppLogger::warning(QStringLiteral("REMOTE.PLATFORM"),
                         QStringLiteral("event=profile_parse_failed file=%1 error=%2")
                             .arg(info.fileName(), parseError.errorString()));
      continue;
    }
    if (!RemotePlatformProfile::fromJson(document.object(), &profile, &error)) {
      lastError_ = QStringLiteral("%1 校验失败：%2").arg(info.fileName(), error);
      AppLogger::warning(QStringLiteral("REMOTE.PLATFORM"),
                         QStringLiteral("event=profile_invalid file=%1 error=%2")
                             .arg(info.fileName(), error));
      continue;
    }
    bool replaced = false;
    for (auto &existing : profiles_) {
      if (existing.id == profile.id) {
        existing = profile;
        replaced = true;
        break;
      }
    }
    if (!replaced) profiles_.append(profile);
  }

  QStringList ids;
  for (const auto &profile : profiles_) ids.append(profile.id);
  AppLogger::write(QStringLiteral("REMOTE.PLATFORM"),
                   QStringLiteral("event=profiles_loaded count=%1 ids=%2 dir=%3")
                       .arg(profiles_.size())
                       .arg(ids.join(QLatin1Char(',')))
                       .arg(profileDir()));
}

QStringList RemotePlatformRegistry::displayNames() const {
  QStringList names;
  for (const auto &profile : profiles_) names.append(profile.displayName);
  return names;
}

RemotePlatformProfile RemotePlatformRegistry::profile(const QString &id) const {
  for (const auto &profile : profiles_) {
    if (profile.id == id) return profile;
  }
  return builtinHc10Pro();
}

} // namespace crawling
