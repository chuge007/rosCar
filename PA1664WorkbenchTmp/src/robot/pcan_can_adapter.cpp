#include "pcan_can_adapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cstddef>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace crawling {
namespace {
void setError(QString* error, const QString& message) {
  if (error) *error = message;
}

quint16 usbHandle(int number) {
  if (number < 1 || number > 16) return 0;
  return quint16(number <= 8 ? 0x50 + number : 0x500 + number);
}

quint16 channelHandle(const QString& port) {
  const QString name = port.trimmed().toUpper();
  static const QRegularExpression usb(QStringLiteral("^PCAN_USBBUS([0-9]{1,2})$"));
  const auto match = usb.match(name);
  if (match.hasMatch()) return usbHandle(match.captured(1).toInt());
  const bool prefixed = name.startsWith(QStringLiteral("PCAN:"));
  if (!prefixed && !name.startsWith(QStringLiteral("PCAN_0X"))) return 0;
  bool ok = false;
  const uint handle = name.mid(prefixed ? 5 : 7).toUInt(&ok, 16);
  return ok && handle > 0 && handle <= 0xFFFF ? quint16(handle) : 0;
}

QString channelId(quint16 handle) {
  return QStringLiteral("pcan:%1").arg(handle, 0, 16);
}

QString channelName(quint16 handle) {
  for (int number = 1; number <= 16; ++number)
    if (handle == usbHandle(number)) return QStringLiteral("PCAN_USBBUS%1").arg(number);
  return QStringLiteral("PCAN_0x%1").arg(handle, 0, 16);
}

#ifdef Q_OS_WIN
// Minimal ABI declarations from PEAK's PCANBasic.h; use normal Windows alignment.
struct PcanMessage {
  quint32 id;
  quint8 type;
  quint8 length;
  quint8 data[8];
};
struct PcanTimestamp {
  quint32 millis;
  quint16 overflow;
  quint16 micros;
};
struct PcanChannelInfo {
  quint16 handle;
  quint8 deviceType;
  quint8 controller;
  quint32 features;
  char deviceName[33];
  quint32 deviceId;
  quint32 condition;
};
static_assert(sizeof(PcanMessage) == 16 && offsetof(PcanMessage, data) == 6);
static_assert(sizeof(PcanTimestamp) == 8);
static_assert(sizeof(PcanChannelInfo) == 52 && offsetof(PcanChannelInfo, deviceId) == 44);

constexpr quint32 kQueueEmpty = 0x20;
constexpr quint32 kCaution = 0x2000000;
constexpr quint8 kChannelCondition = 0x0D;
constexpr quint8 kAttachedCount = 0x2A;
constexpr quint8 kAttachedChannels = 0x2B;

quint16 bitrateCode(int bitrate) {
  switch (bitrate) {
    case 1000000: return 0x0014;
    case 800000: return 0x0016;
    case 500000: return 0x001C;
    case 250000: return 0x011C;
    case 125000: return 0x031C;
    case 100000: return 0x432F;
    case 50000: return 0x472F;
    case 20000: return 0x532F;
    case 10000: return 0x672F;
    default: return 0;
  }
}

class PcanApi {
 public:
  ~PcanApi() { library_.unload(); }
  using Initialize = quint32 (__stdcall*)(quint16, quint16, quint8, quint32, quint16);
  using Uninitialize = quint32 (__stdcall*)(quint16);
  using Read = quint32 (__stdcall*)(quint16, PcanMessage*, PcanTimestamp*);
  using Write = quint32 (__stdcall*)(quint16, PcanMessage*);
  using GetValue = quint32 (__stdcall*)(quint16, quint8, void*, quint32);
  using GetStatus = quint32 (__stdcall*)(quint16);
  using GetErrorText = quint32 (__stdcall*)(quint32, quint16, char*);
  Initialize initialize = nullptr;
  Uninitialize uninitialize = nullptr;
  Read read = nullptr;
  Write write = nullptr;
  GetValue getValue = nullptr;
  GetStatus getStatus = nullptr;
  GetErrorText getErrorText = nullptr;

  bool load(QString* error) {
    if (initialize) return true;
    // Absolute paths avoid loading a DLL from an unrelated working directory.
    QStringList paths{QDir(QCoreApplication::applicationDirPath())
                          .filePath(QStringLiteral("PCANBasic.dll"))};
    wchar_t systemPath[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
      paths.append(QDir(QString::fromWCharArray(systemPath))
                       .filePath(QStringLiteral("PCANBasic.dll")));
    QString details;
    for (const auto& path : paths) {
      library_.setFileName(path);
      if (!library_.load()) { details = library_.errorString(); continue; }
      const auto init = reinterpret_cast<Initialize>(library_.resolve("CAN_Initialize"));
      uninitialize = reinterpret_cast<Uninitialize>(library_.resolve("CAN_Uninitialize"));
      read = reinterpret_cast<Read>(library_.resolve("CAN_Read"));
      write = reinterpret_cast<Write>(library_.resolve("CAN_Write"));
      getValue = reinterpret_cast<GetValue>(library_.resolve("CAN_GetValue"));
      getStatus = reinterpret_cast<GetStatus>(library_.resolve("CAN_GetStatus"));
      getErrorText = reinterpret_cast<GetErrorText>(library_.resolve("CAN_GetErrorText"));
      if (init && uninitialize && read && write && getValue && getStatus && getErrorText) {
        initialize = init;
        return true;
      }
      details = QStringLiteral("PCANBasic.dll 缺少必要接口");
      library_.unload();
    }
    setError(error, QStringLiteral("无法加载 PCAN-Basic，请安装与程序位数一致的 PEAK 驱动及 PCANBasic.dll：%1")
                        .arg(details));
    return false;
  }

  QString errorText(quint32 status) const {
    char buffer[256]{};
    const QString detail = getErrorText && getErrorText(status, 0x09, buffer) == 0
        ? QString::fromLocal8Bit(buffer).trimmed() : QStringLiteral("PCAN 错误");
    return QStringLiteral("%1（0x%2）").arg(detail).arg(status, 8, 16, QLatin1Char('0'));
  }
 private:
  QLibrary library_;
};
#endif
} // namespace

struct PcanCanAdapter::Private {
  bool running = false;
  QString fault;
#ifdef Q_OS_WIN
  PcanApi api;
  quint16 channel = 0;
  bool initialized = false;
  QTimer pollTimer;

  void close() {
    pollTimer.stop();
    running = false;
    // Never uninitialize NONEBUS: that would release other clients' channels.
    if (initialized && channel) api.uninitialize(channel);
    initialized = false;
    channel = 0;
  }

  void fail(PcanCanAdapter* owner, quint32 status) {
    if (!running || !fault.isEmpty()) return;
    pollTimer.stop();
    fault = QStringLiteral("%1 通信故障：%2")
        .arg(channelName(channel), api.errorText(status));
    // The direct controller slot tries stop frames while this channel can still
    // accept them (e.g. a bus warning), then inhibits further motion commands.
    emit owner->faulted(fault);
    running = false;
  }

  void pollReceive(PcanCanAdapter* owner) {
    // Forward bus frames, but never treat status/error/RTR frames as motor replies.
    for (int count = 0; running && count < 1024; ++count) {
      PcanMessage message{};
      const quint32 status = api.read(channel, &message, nullptr);
      if (status == kQueueEmpty) break;
      if (status != 0) { fail(owner, status); return; }
      if (!(message.type & 0xC1) && message.length <= 8)
        emit owner->frameReceived(message.id,
            QByteArray(reinterpret_cast<const char*>(message.data), message.length),
            (message.type & 0x02) != 0);
    }
    if (running) {
      const quint32 status = api.getStatus(channel);
      if (status != 0) fail(owner, status);
    }
  }
#else
  void close() { running = false; }
#endif
};

PcanCanAdapter::PcanCanAdapter(QObject* parent) : QObject(parent), d_(new Private) {
#ifdef Q_OS_WIN
  d_->pollTimer.setInterval(10);
  connect(&d_->pollTimer, &QTimer::timeout, this, [this] { d_->pollReceive(this); });
#endif
}

PcanCanAdapter::~PcanCanAdapter() { d_->close(); }

bool PcanCanAdapter::isPcanPort(const QString& port) {
  const QString name = port.trimmed();
  return name.startsWith(QStringLiteral("pcan:"), Qt::CaseInsensitive) ||
      name.startsWith(QStringLiteral("PCAN_"), Qt::CaseInsensitive);
}

QString PcanCanAdapter::canonicalPort(const QString& port) {
  const quint16 handle = channelHandle(port);
  return handle ? channelId(handle) : QString();
}

QString PcanCanAdapter::portName(const QString& port) {
  const quint16 handle = channelHandle(port);
  return handle ? channelName(handle) : port.trimmed();
}

QVector<PcanCanAdapter::Port> PcanCanAdapter::availablePorts(QString* error) {
  setError(error, {});
  QVector<Port> ports;
#ifdef Q_OS_WIN
  PcanApi api;
  if (!api.load(error)) return ports;
  quint32 count = 0;
  quint32 status = api.getValue(0, kAttachedCount, &count, sizeof(count));
  if (status == 0) {
    if (count > 256) {
      setError(error, QStringLiteral("PCAN 返回的通道数量无效"));
      return ports;
    }
    if (count == 0) return ports;
    QVector<PcanChannelInfo> channels(static_cast<int>(count));
    status = api.getValue(0, kAttachedChannels, channels.data(),
                         quint32(channels.size() * sizeof(PcanChannelInfo)));
    if (status == 0) {
      std::sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) {
        return a.handle < b.handle;
      });
      for (const auto& channel : channels) {
        if (!channel.handle || !(channel.condition & 3)) continue;
        const auto end = std::find(channel.deviceName, channel.deviceName + 33, '\0');
        const QString device = QString::fromLocal8Bit(
            channel.deviceName, int(end - channel.deviceName));
        ports.append({channelName(channel.handle), channelId(channel.handle),
            QStringLiteral("%1 / 设备 ID %2 / %3\nPCAN-Basic 经典 CAN")
                .arg(device).arg(channel.deviceId)
                .arg(channel.condition & 2 ? QStringLiteral("通道已占用") : QStringLiteral("可用"))});
      }
      return ports;
    }
  }
  // Older PCAN-Basic releases do not provide the attached-channel array.
  for (int number = 1; number <= 16; ++number) {
    const quint16 handle = usbHandle(number);
    quint32 condition = 0;
    if (api.getValue(handle, kChannelCondition, &condition, sizeof(condition)) == 0 &&
        (condition & 3))
      ports.append({channelName(handle), channelId(handle),
          condition & 2 ? QStringLiteral("PCAN-Basic：通道已占用")
                        : QStringLiteral("PCAN-Basic：可用")});
  }
  if (ports.isEmpty())
    setError(error, QStringLiteral("PCAN 通道枚举失败：%1").arg(api.errorText(status)));
#else
  setError(error, QStringLiteral("PCAN-Basic 原生连接仅支持 Windows"));
#endif
  return ports;
}

bool PcanCanAdapter::open(const QString& port, int bitrate, QString* error) {
  d_->close();
  d_->fault.clear();
  setError(error, {});
#ifdef Q_OS_WIN
  const quint16 handle = channelHandle(port);
  const quint16 baud = bitrateCode(bitrate);
  if (!handle || !baud) {
    setError(error, QStringLiteral("PCAN 通道或 CAN 波特率无效：%1 / %2").arg(port).arg(bitrate));
    return false;
  }
  if (!d_->api.load(error)) return false;
  const quint32 status = d_->api.initialize(handle, baud, 0, 0, 0);
  if (status != 0) {
    // CAUTION means initialized, but with another bitrate. Reject and release it.
    if (status & kCaution) d_->api.uninitialize(handle);
    setError(error, QStringLiteral("%1 初始化失败（请求 %2 bit/s）：%3")
        .arg(channelName(handle)).arg(bitrate).arg(d_->api.errorText(status)));
    return false;
  }
  d_->channel = handle;
  d_->initialized = true;
  d_->running = true;
  d_->pollTimer.start();
  return true;
#else
  Q_UNUSED(port);
  Q_UNUSED(bitrate);
  setError(error, QStringLiteral("PCAN-Basic 原生连接仅支持 Windows"));
  return false;
#endif
}

bool PcanCanAdapter::isOpen() const { return d_->running; }

bool PcanCanAdapter::sendFrame(std::uint32_t id, const QByteArray& data,
                               bool extended, QString* error) {
  setError(error, {});
  if (!d_->running) {
    setError(error, d_->fault.isEmpty() ? QStringLiteral("PCAN 通道未连接") : d_->fault);
    return false;
  }
  if (id > (extended ? 0x1FFFFFFFU : 0x7FFU) || data.isEmpty() || data.size() > 8) {
    setError(error, QStringLiteral("PCAN 经典 CAN 帧格式无效"));
    return false;
  }
#ifdef Q_OS_WIN
  PcanMessage message{};
  message.id = id;
  message.type = extended ? 0x02 : 0x00;
  message.length = quint8(data.size());
  std::memcpy(message.data, data.constData(), std::size_t(data.size()));
  const quint32 status = d_->api.write(d_->channel, &message);
  if (status == 0) return true;
  d_->fail(this, status);
  setError(error, d_->fault);
#endif
  return false;
}

} // namespace crawling
