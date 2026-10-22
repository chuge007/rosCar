#include "candle_can_adapter.h"

#include <QRegularExpression>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#endif

namespace crawling {
namespace {
void setError(QString* error, const QString& message) {
  if (error) *error = message;
}

#ifdef Q_OS_WIN
// candleLight's WinUSB interface GUID (also used by cangaroo/CandleAPI).
const GUID kCandleGuid = {0xc15b4308, 0x04d3, 0x11e6,
                         {0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43}};

QString windowsError(DWORD code) {
  wchar_t* buffer = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  const QString detail = buffer ? QString::fromWCharArray(buffer).trimmed() : QString();
  if (buffer) LocalFree(buffer);
  return QStringLiteral("%1（Windows %2）").arg(detail).arg(code);
}

void put32(QByteArray& bytes, int offset, quint32 value) {
  qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(bytes.data() + offset));
}

quint32 get32(const QByteArray& bytes, int offset) {
  return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

// Derive nominal timing from the device's actual clock/limits: both 48 MHz
// CANable 0.x and 170 MHz CANable 2.0 use the same gs_usb control protocol.
QByteArray bitTiming(const QByteArray& capabilities, int bitrate) {
  if (capabilities.size() != 40 || bitrate <= 0) return {};
  const quint32 clock = get32(capabilities, 4);
  const quint32 seg1Min = get32(capabilities, 8), seg1Max = get32(capabilities, 12);
  const quint32 seg2Min = get32(capabilities, 16), seg2Max = get32(capabilities, 20);
  const quint32 sjwMax = get32(capabilities, 24);
  const quint32 brpMin = get32(capabilities, 28), brpMax = get32(capabilities, 32);
  const quint32 increment = get32(capabilities, 36);
  if (!clock || !seg1Min || seg1Min > seg1Max || !seg2Min || seg2Min > seg2Max ||
      !sjwMax || !brpMin || brpMin > brpMax || !increment || brpMax > 1000000 ||
      seg1Max > 65535 || seg2Max > 65535) return {};
  QByteArray best;
  double bestError = std::numeric_limits<double>::max();
  for (quint64 brp = brpMin; brp <= brpMax; brp += increment) {
    const quint64 divisor = quint64(bitrate) * brp;
    if (clock % divisor) continue;
    const qint64 total = clock / divisor;
    const qint64 low = std::max<qint64>(seg2Min, total - 1 - seg1Max);
    const qint64 high = std::min<qint64>(seg2Max, total - 1 - seg1Min);
    if (low > high || total < 4) continue;
    const qint64 seg2 = qBound(low, (total + 4) / 8, high);
    const quint32 seg1 = quint32(total - 1 - seg2);
    const double error = qAbs(double(total - seg2) / double(total) - 0.875);
    if (error >= bestError) continue;
    bestError = error;
    best = QByteArray(20, '\0');
    put32(best, 0, 1); // prop_seg + phase_seg1 = tseg1
    put32(best, 4, seg1 - 1);
    put32(best, 8, quint32(seg2));
    put32(best, 12, 1); // sjw
    put32(best, 16, quint32(brp));
  }
  return best;
}
#endif
} // namespace

struct CandleCanAdapter::Private {
  bool running = false;
#ifdef Q_OS_WIN
  HANDLE file = INVALID_HANDLE_VALUE;
  WINUSB_INTERFACE_HANDLE usb = nullptr;
  UCHAR interfaceNumber = 0, input = 0, output = 0;
  OVERLAPPED receive{};
  std::array<UCHAR, 512> receiveBuffer{};
  bool pending = false;
  bool started = false;
  QTimer pollTimer;
  QString receiveError;

  bool control(UCHAR request, bool read, USHORT value, USHORT index,
               QByteArray& bytes) {
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = read ? 0xC1 : 0x41; // vendor/interface, device/host
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = static_cast<USHORT>(bytes.size());
    ULONG transferred = 0;
    return WinUsb_ControlTransfer(usb, setup,
               reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
               &transferred, nullptr) && transferred == static_cast<ULONG>(bytes.size());
  }

  bool mode(quint32 value) {
    QByteArray bytes(8, '\0');
    put32(bytes, 0, value);
    // Channel 0, classic CAN; FD, loopback and listen-only are disabled.
    return control(2, false, 0, interfaceNumber, bytes);
  }

  void failReceive(CandleCanAdapter* owner) {
    receiveError = windowsError(GetLastError());
    running = false;
    pollTimer.stop();
    if (started)
      emit owner->faulted(QStringLiteral("candle CAN 接收失败：") + receiveError);
  }

  // Only gs_usb RX frames (echo_id == -1) represent bus traffic; TX echoes do not.
  void pollReceive(CandleCanAdapter* owner) {
    for (int count = 0; usb && count < 32; ++count) {
      ULONG transferred = 0;
      if (pending) {
        if (!WinUsb_GetOverlappedResult(usb, &receive, &transferred, FALSE)) {
          if (GetLastError() != ERROR_IO_INCOMPLETE) failReceive(owner);
          return;
        }
        pending = false;
        for (ULONG offset = 0; offset + 20 <= transferred; offset += 20) {
          const QByteArray frame(reinterpret_cast<const char*>(receiveBuffer.data() + offset), 20);
          const quint32 echo = get32(frame, 0);
          const quint32 canId = get32(frame, 4);
          const int length = quint8(frame[8]);
          if (echo != 0xFFFFFFFFU || (canId & 0x60000000U) || length > 8) continue;
          emit owner->frameReceived(canId & 0x1FFFFFFFU, frame.mid(12, length),
                                    (canId & 0x80000000U) != 0);
        }
      }
      ResetEvent(receive.hEvent);
      if (!WinUsb_ReadPipe(usb, input, receiveBuffer.data(),
                          static_cast<ULONG>(receiveBuffer.size()), &transferred, &receive)) {
        if (GetLastError() == ERROR_IO_PENDING) pending = true;
        else failReceive(owner);
        return;
      }
      for (ULONG offset = 0; offset + 20 <= transferred; offset += 20) {
        const QByteArray frame(reinterpret_cast<const char*>(receiveBuffer.data() + offset), 20);
        const quint32 echo = get32(frame, 0);
        const quint32 canId = get32(frame, 4);
        const int length = quint8(frame[8]);
        if (echo != 0xFFFFFFFFU || (canId & 0x60000000U) || length > 8) continue;
        emit owner->frameReceived(canId & 0x1FFFFFFFU, frame.mid(12, length),
                                  (canId & 0x80000000U) != 0);
      }
    }
  }

  void close() {
    pollTimer.stop();
    running = false;
    if (usb) {
      if (started) mode(0);
      if (input) WinUsb_AbortPipe(usb, input);
      if (pending) {
        ULONG transferred = 0;
        WinUsb_GetOverlappedResult(usb, &receive, &transferred, TRUE);
      }
      WinUsb_Free(usb);
    }
    if (receive.hEvent) CloseHandle(receive.hEvent);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    usb = nullptr;
    file = INVALID_HANDLE_VALUE;
    receive = {};
    input = output = 0;
    pending = started = false;
    receiveError.clear();
  }
#endif
};

CandleCanAdapter::CandleCanAdapter(QObject* parent)
    : QObject(parent), d_(new Private) {
#ifdef Q_OS_WIN
  d_->pollTimer.setInterval(5);
  connect(&d_->pollTimer, &QTimer::timeout, this, [this] { d_->pollReceive(this); });
#endif
}

CandleCanAdapter::~CandleCanAdapter() {
#ifdef Q_OS_WIN
  d_->close();
#endif
}

bool CandleCanAdapter::isCandlePort(const QString& port) {
  const QString value = port.trimmed();
  static const QRegularExpression alias(QStringLiteral("^candle[0-9]+$"),
                                        QRegularExpression::CaseInsensitiveOption);
  return value.startsWith(QStringLiteral("candle:"), Qt::CaseInsensitive) ||
      alias.match(value).hasMatch();
}

QVector<CandleCanAdapter::Port> CandleCanAdapter::availablePorts() {
  QVector<Port> ports;
#ifdef Q_OS_WIN
  const HDEVINFO devices = SetupDiGetClassDevsW(&kCandleGuid, nullptr, nullptr,
                                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE) return ports;
  for (DWORD index = 0; ; ++index) {
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &kCandleGuid, index, &interfaceData))
      break;
    DWORD size = 0;
    SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &size, nullptr);
    if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
    // ULONG storage supplies the alignment required by the Windows structure.
    std::vector<ULONG> storage((size + sizeof(ULONG) - 1) / sizeof(ULONG));
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, size, nullptr, nullptr))
      continue;
    ports.append({QStringLiteral("candle%1").arg(index),
                  QStringLiteral("candle:") + QString::fromWCharArray(detail->DevicePath)});
  }
  SetupDiDestroyDeviceInfoList(devices);
#endif
  return ports;
}

bool CandleCanAdapter::open(const QString& port, int bitrate, QString* error) {
#ifdef Q_OS_WIN
  d_->close();
  QString id;
  for (const auto& entry : availablePorts()) {
    if (entry.id.compare(port.trimmed(), Qt::CaseInsensitive) == 0 ||
        entry.name.compare(port.trimmed(), Qt::CaseInsensitive) == 0) {
      id = entry.id;
      break;
    }
  }
  if (id.isEmpty()) {
    setError(error, QStringLiteral("未找到 candleLight 适配器，请检查 USB 连接和 WinUSB 驱动后刷新端口"));
    return false;
  }
  const QString path = id.mid(7);
  d_->file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  const auto fail = [this, error](const QString& message) {
    setError(error, message);
    d_->close();
    return false;
  };
  const auto failWindows = [&fail](const QString& message) {
    return fail(message + QStringLiteral(": ") + windowsError(GetLastError()));
  };
  if (d_->file == INVALID_HANDLE_VALUE)
    return failWindows(QStringLiteral("candle 设备打开失败，请关闭 cangaroo 等占用程序"));
  if (!WinUsb_Initialize(d_->file, &d_->usb))
    return failWindows(QStringLiteral("WinUSB 初始化失败，请检查 candleLight 驱动"));
  USB_INTERFACE_DESCRIPTOR descriptor{};
  if (!WinUsb_QueryInterfaceSettings(d_->usb, 0, &descriptor))
    return failWindows(QStringLiteral("读取 candle USB 接口失败"));
  d_->interfaceNumber = descriptor.bInterfaceNumber;
  for (UCHAR index = 0; index < descriptor.bNumEndpoints; ++index) {
    WINUSB_PIPE_INFORMATION pipe{};
    if (!WinUsb_QueryPipe(d_->usb, 0, index, &pipe))
      return failWindows(QStringLiteral("读取 candle USB 端点失败"));
    if (pipe.PipeType != UsbdPipeTypeBulk) continue;
    if (pipe.PipeId & 0x80) d_->input = pipe.PipeId;
    else d_->output = pipe.PipeId;
  }
  if (!d_->input || !d_->output)
    return fail(QStringLiteral("candle 接口缺少 USB CAN 收发端点"));
  ULONG timeout = 100;
  if (!WinUsb_SetPipePolicy(d_->usb, d_->output, PIPE_TRANSFER_TIMEOUT,
                           sizeof(timeout), &timeout))
    return failWindows(QStringLiteral("设置 candle 发送超时失败"));
  QByteArray host(4, '\0');
  put32(host, 0, 0x0000BEEF);
  if (!d_->control(0, false, 1, d_->interfaceNumber, host))
    return failWindows(QStringLiteral("设置 candle 数据格式失败"));
  QByteArray config(12, '\0');
  if (!d_->control(5, true, 1, d_->interfaceNumber, config))
    return failWindows(QStringLiteral("读取 candle 设备信息失败"));
  QByteArray capabilities(40, '\0');
  if (!d_->control(4, true, 0, 0, capabilities))
    return failWindows(QStringLiteral("读取 candle CAN 时序能力失败"));
  QByteArray timing = bitTiming(capabilities, bitrate);
  if (timing.isEmpty())
    return fail(QStringLiteral("candle 设备不支持所选 CAN 波特率 %1").arg(bitrate));
  if (!d_->mode(0) || !d_->control(1, false, 0, 0, timing))
    return failWindows(QStringLiteral("设置 candle CAN 波特率失败"));
  d_->receive.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!d_->receive.hEvent) return failWindows(QStringLiteral("创建 candle 接收事件失败"));
  d_->running = true;
  d_->pollReceive(this);
  if (!d_->running)
    return fail(QStringLiteral("启动 candle USB 接收失败: ") + d_->receiveError);
  if (!d_->mode(1)) return failWindows(QStringLiteral("启动 candle CAN 通道失败"));
  d_->started = true;
  d_->pollTimer.start();
  return true;
#else
  Q_UNUSED(port);
  Q_UNUSED(bitrate);
  setError(error, QStringLiteral("原生 candleLight USB CAN 通道目前仅支持 Windows"));
  return false;
#endif
}

bool CandleCanAdapter::isOpen() const { return d_->running; }

bool CandleCanAdapter::sendFrame(std::uint32_t id, const QByteArray& data,
                                bool extended, QString* error) {
  if (!isOpen() || data.isEmpty() || data.size() > 8 ||
      id > (extended ? 0x1FFFFFFFU : 0x7FFU)) {
    setError(error, QStringLiteral("candle 通道未连接或 CAN 帧无效"));
    return false;
  }
#ifdef Q_OS_WIN
  // gs_usb classic host frame, timestamps and CAN FD disabled (20 bytes).
  QByteArray frame(20, '\0');
  put32(frame, 4, id | (extended ? 0x80000000U : 0U));
  frame[8] = char(data.size());
  std::copy(data.cbegin(), data.cend(), frame.begin() + 12);
  ULONG transferred = 0;
  if (!WinUsb_WritePipe(d_->usb, d_->output, reinterpret_cast<PUCHAR>(frame.data()),
                       static_cast<ULONG>(frame.size()), &transferred, nullptr) ||
      transferred != static_cast<ULONG>(frame.size())) {
    setError(error, QStringLiteral("candle USB CAN 发送失败: ") + windowsError(GetLastError()));
    return false;
  }
  return true;
#else
  Q_UNUSED(error);
  return false;
#endif
}

} // namespace crawling
