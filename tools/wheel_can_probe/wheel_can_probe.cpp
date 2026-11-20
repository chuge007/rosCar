// 独立驱动轮 CAN 调试工具：不依赖 PA1664RobotHost，直连 SLCAN 适配器读写 MWD 伺服。
//
// 用法：
//   wheel_can_probe.exe --port COM8 [--init] [--scan]
//                       [--status <id>] [--watch <id> <seconds>]
//                       [--cmd <id> <hex>] [--speed <id> <dps> <seconds>]
//                       [--stop <id>]
//
// 协议依据 tmp/mwd_protocol_v3_8.txt：
//   命令帧 ID = 0x140 + 电机ID，反馈 ID = 0x240 + 电机ID
//   0x9A 状态1：data[1] 温度, data[3] RlyCtrlRslt, data[4..5] 母线电压 0.1V,
//                data[6..7] System_errorState
//   0x9C 状态2：data[1] 温度, data[2..3] iq 0.01A, data[4..5] speed 1dps,
//                data[6..7] 角度 1 度
//   0x81 停止, 0x88 运行, 0xA2 速度闭环（0.01 dps/LSB, int32 LE at data[4]）
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kCmdBase = 0x140;
constexpr std::uint32_t kFbBase = 0x240;

struct Frame {
  std::uint32_t id = 0;
  std::uint8_t data[8]{};
};

long long nowMs() {
  return static_cast<long long>(GetTickCount64());
}

HANDLE openPort(const char* name, int baud) {
  char path[64];
  std::snprintf(path, sizeof path, "\\\\.\\%s", name);
  HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    std::printf("OPEN_FAILED port=%s err=%lu\n", name, GetLastError());
    return handle;
  }
  DCB dcb{};
  dcb.DCBlength = sizeof dcb;
  if (!GetCommState(handle, &dcb)) {
    std::printf("GET_COMM_STATE_FAILED err=%lu\n", GetLastError());
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
  }
  dcb.BaudRate = static_cast<DWORD>(baud);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  if (!SetCommState(handle, &dcb)) {
    std::printf("SET_COMM_STATE_FAILED err=%lu\n", GetLastError());
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
  }
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 200;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  SetCommTimeouts(handle, &timeouts);
  PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
  std::printf("PORT_OPEN port=%s baud=%d\n", name, baud);
  return handle;
}

bool writeRaw(HANDLE handle, const char* text) {
  DWORD written = 0;
  const DWORD length = static_cast<DWORD>(std::strlen(text));
  if (!WriteFile(handle, text, length, &written, nullptr) || written != length) {
    std::printf("WRITE_FAILED err=%lu len=%lu\n", GetLastError(),
                static_cast<unsigned long>(length));
    return false;
  }
  return true;
}

std::string frameText(const Frame& frame) {
  char text[64];
  std::snprintf(text, sizeof text, "t%03X8%02X%02X%02X%02X%02X%02X%02X%02X\r",
                static_cast<unsigned>(frame.id), frame.data[0], frame.data[1],
                frame.data[2], frame.data[3], frame.data[4], frame.data[5],
                frame.data[6], frame.data[7]);
  return text;
}

std::string hexOf(const Frame& frame) {
  char text[32];
  std::snprintf(text, sizeof text, "%02X%02X%02X%02X%02X%02X%02X%02X",
                frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
  return text;
}

bool sendFrame(HANDLE handle, const Frame& frame) {
  return writeRaw(handle, frameText(frame).c_str());
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

std::vector<Frame> drainFrames(HANDLE handle) {
  std::vector<Frame> frames;
  char chunk[512];
  DWORD read = 0;
  static std::string pending;
  while (ReadFile(handle, chunk, sizeof chunk, &read, nullptr) && read > 0) {
    pending.append(chunk, read);
  }
  size_t start = 0;
  while (true) {
    const size_t end = pending.find('\r', start);
    if (end == std::string::npos) break;
    const std::string line = pending.substr(start, end - start);
    start = end + 1;
    if (line.size() != 21 || (line[0] != 't' && line[0] != 'T')) continue;
    Frame frame;
    int id = 0;
    for (int i = 1; i <= 3; ++i) id = (id << 4) | hexValue(line[static_cast<size_t>(i)]);
    frame.id = static_cast<std::uint32_t>(id);
    const int length = hexValue(line[4]);
    if (length != 8) continue;
    if (line[0] == 't' && frame.id > 0x7FF) continue;
    for (int i = 0; i < 8; ++i) {
      const int high = hexValue(line[static_cast<size_t>(5 + i * 2)]);
      const int low = hexValue(line[static_cast<size_t>(6 + i * 2)]);
      if (high < 0 || low < 0) { frame.id = 0; break; }
      frame.data[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    if (frame.id != 0) frames.push_back(frame);
  }
  if (start > 0) pending.erase(0, start);
  if (pending.size() > 4096) pending.clear();
  return frames;
}

void printFrame(unsigned long originId, const Frame& frame) {
  const std::string hex = hexOf(frame);
  std::printf("RX_MATCH origin=%lX id=0x%03X data=%s", originId, frame.id,
              hex.c_str());
  if (frame.id >= kFbBase + 1 && frame.id <= kFbBase + 32) {
    const int motorId = static_cast<int>(frame.id - kFbBase);
    if (frame.data[0] == 0x9C) {
      const auto iq = static_cast<std::int16_t>(
          static_cast<std::uint16_t>(frame.data[2]) |
          (static_cast<std::uint16_t>(frame.data[3]) << 8));
      const auto speed = static_cast<std::int16_t>(
          static_cast<std::uint16_t>(frame.data[4]) |
          (static_cast<std::uint16_t>(frame.data[5]) << 8));
      const auto angle = static_cast<std::int16_t>(
          static_cast<std::uint16_t>(frame.data[6]) |
          (static_cast<std::uint16_t>(frame.data[7]) << 8));
      std::printf(
          " [9C motor=%d temp=%dC iq=%.2fA speed=%ddps angle=%d]",
          motorId, static_cast<std::int8_t>(frame.data[1]), iq * 0.01, speed,
          angle);
    } else if (frame.data[0] == 0x9A) {
      const int voltage =
          static_cast<int>(frame.data[4]) |
          (static_cast<int>(frame.data[5]) << 8);
      const int error = static_cast<int>(frame.data[6]) |
                        (static_cast<int>(frame.data[7]) << 8);
      std::printf(
          " [9A motor=%d temp=%dC rlyCtrlRslt=%d busVolt=%.1fV error=0x%04X]",
          motorId, static_cast<std::int8_t>(frame.data[1]), int(frame.data[3]),
          voltage * 0.1, error);
    }
  }
  std::printf("\n");
  std::fflush(stdout);
}

// 发一帧
// originId，等待最多 waitMs，打印所有反馈 ID 匹配的应答，返回匹配数量。
int queryAndPrint(HANDLE handle, std::uint8_t command, std::uint8_t motorId,
                  int waitMs) {
  Frame frame;
  frame.id = kCmdBase + motorId;
  frame.data[0] = command;
  sendFrame(handle, frame);
  const long long deadline = nowMs() + waitMs;
  int matched = 0;
  while (nowMs() < deadline) {
    for (const Frame& reply : drainFrames(handle)) {
      if (reply.id == kFbBase + motorId) {
        printFrame(frame.id, reply);
        ++matched;
      }
    }
    Sleep(2);
  }
  return matched;
}

void runScan(HANDLE handle) {
  std::printf("SCAN_BEGIN ids=1..8 command=0x9A\n");
  for (int id = 1; id <= 8; ++id) {
    const int matched =
        queryAndPrint(handle, 0x9A, static_cast<std::uint8_t>(id), 60);
    if (matched == 0) {
      std::printf("SCAN id=%d NO_REPLY\n", id);
    }
    std::fflush(stdout);
  }
  std::printf("SCAN_END\n");
}

void runStatus(HANDLE handle, int motorId) {
  for (int i = 0; i < 3; ++i) {
    queryAndPrint(handle, 0x9A, static_cast<std::uint8_t>(motorId), 120);
    Sleep(80);
  }
}

void runWatch(HANDLE handle, int motorId, int seconds) {
  std::printf("WATCH_BEGIN id=%d seconds=%d command=0x9C\n", motorId, seconds);
  const long long deadline = nowMs() + static_cast<long long>(seconds) * 1000;
  while (nowMs() < deadline) {
    queryAndPrint(handle, 0x9C, static_cast<std::uint8_t>(motorId), 40);
    Sleep(60);
  }
  std::printf("WATCH_END\n");
}

void runSpeed(HANDLE handle, int motorId, int dps, int seconds) {
  std::printf("SPEED_BEGIN id=%d target=%ddps seconds=%d\n", motorId, dps,
              seconds);
  Frame run;
  run.id = kCmdBase + static_cast<std::uint32_t>(motorId);
  run.data[0] = 0x88;
  sendFrame(handle, run);
  Sleep(60);
  Frame brake;
  brake.id = run.id;
  brake.data[0] = 0x8C;
  brake.data[1] = 0x01;
  sendFrame(handle, brake);
  Sleep(60);

  Frame speed;
  speed.id = run.id;
  speed.data[0] = 0xA2;
  const auto raw = static_cast<std::int32_t>(dps * 100);
  speed.data[4] = static_cast<std::uint8_t>(raw & 0xFF);
  speed.data[5] = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
  speed.data[6] = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
  speed.data[7] = static_cast<std::uint8_t>((raw >> 24) & 0xFF);

  const long long deadline = nowMs() + static_cast<long long>(seconds) * 1000;
  long long nextSpeed = 0;
  long long nextStatus = 0;
  int statusPhase = 0;
  while (nowMs() < deadline) {
    const long long now = nowMs();
    if (now >= nextSpeed) {
      sendFrame(handle, speed);
      nextSpeed = now + 100;
    }
    if (now >= nextStatus) {
      // 交替读状态2（速度/角度）与状态1（抱闸继电器结果/故障码），
      // 这样能看到"命令已下发"与"抱闸是否真的松开"的对应关系。
      Frame fb;
      fb.id = kCmdBase + static_cast<std::uint32_t>(motorId);
      fb.data[0] = (statusPhase++ % 2 == 0) ? 0x9C : 0x9A;
      sendFrame(handle, fb);
      nextStatus = now + 500;
    }
    for (const Frame& reply : drainFrames(handle)) {
      if (reply.id == kFbBase + static_cast<std::uint32_t>(motorId)) {
        printFrame(speed.id, reply);
      }
    }
    Sleep(10);
  }

  Frame stop;
  stop.id = run.id;
  stop.data[0] = 0x81;
  sendFrame(handle, stop);
  std::printf("SPEED_END stop_sent\n");
}

// 力矩/电流环模式（0xA1，iqControl 0.01A/LSB）：用小电流探测功率级与抱闸。
// 三相电流（0x9D）会同步打印，用来判断电机是否真的出力。
void runTorque(HANDLE handle, int motorId, double amps, int seconds) {
  std::printf("TORQUE_BEGIN id=%d target=%.2fA seconds=%d\n", motorId, amps,
              seconds);
  const std::uint32_t id = kCmdBase + static_cast<std::uint32_t>(motorId);
  Frame run;
  run.id = id;
  run.data[0] = 0x88;
  sendFrame(handle, run);
  Sleep(60);
  Frame brake;
  brake.id = id;
  brake.data[0] = 0x8C;
  brake.data[1] = 0x01;
  sendFrame(handle, brake);
  Sleep(60);

  Frame torque;
  torque.id = id;
  torque.data[0] = 0xA1;
  const auto raw = static_cast<std::int16_t>(amps * 100.0);
  torque.data[4] = static_cast<std::uint8_t>(raw & 0xFF);
  torque.data[5] = static_cast<std::uint8_t>((raw >> 8) & 0xFF);

  const long long deadline = nowMs() + static_cast<long long>(seconds) * 1000;
  long long nextTorque = 0;
  long long nextProbe = 0;
  int phase = 0;
  while (nowMs() < deadline) {
    const long long now = nowMs();
    if (now >= nextTorque) {
      sendFrame(handle, torque);
      nextTorque = now + 100;
    }
    if (now >= nextProbe) {
      Frame probe;
      probe.id = id;
      probe.data[0] = (phase % 2 == 0) ? 0x9D : 0x9C;
      sendFrame(handle, probe);
      nextProbe = now + 300;
      ++phase;
    }
    for (const Frame& reply : drainFrames(handle)) {
      if (reply.id == kFbBase + static_cast<std::uint32_t>(motorId)) {
        printFrame(id, reply);
        if (reply.data[0] == 0x9D) {
          const auto ia = static_cast<std::int16_t>(
              static_cast<std::uint16_t>(reply.data[2]) |
              (static_cast<std::uint16_t>(reply.data[3]) << 8));
          const auto ib = static_cast<std::int16_t>(
              static_cast<std::uint16_t>(reply.data[4]) |
              (static_cast<std::uint16_t>(reply.data[5]) << 8));
          const auto ic = static_cast<std::int16_t>(
              static_cast<std::uint16_t>(reply.data[6]) |
              (static_cast<std::uint16_t>(reply.data[7]) << 8));
          std::printf(" [9D iA=%.2fA iB=%.2fA iC=%.2fA]\n", ia * 0.01,
                      ib * 0.01, ic * 0.01);
          std::fflush(stdout);
        }
      }
    }
    Sleep(10);
  }

  Frame zero;
  zero.id = id;
  zero.data[0] = 0xA1;
  sendFrame(handle, zero);
  Sleep(60);
  Frame stop;
  stop.id = id;
  stop.data[0] = 0x81;
  sendFrame(handle, stop);
  std::printf("TORQUE_END zero_and_stop_sent\n");
}

void runRawCommand(HANDLE handle, int motorId, const char* hex) {
  Frame frame;
  frame.id = kCmdBase + static_cast<std::uint32_t>(motorId);
  const size_t length = std::strlen(hex);
  const size_t bytes = length / 2;
  for (size_t i = 0; i < bytes && i < 8; ++i) {
    const int high = hexValue(hex[i * 2]);
    const int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) {
      std::printf("RAW_BAD_HEX\n");
      return;
    }
    frame.data[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  std::printf("RAW_TX id=0x%03X data=%s\n", frame.id, hexOf(frame).c_str());
  sendFrame(handle, frame);
  const long long deadline = nowMs() + 300;
  while (nowMs() < deadline) {
    for (const Frame& reply : drainFrames(handle)) {
      if (reply.id == kFbBase + static_cast<std::uint32_t>(motorId)) {
        printFrame(frame.id, reply);
      }
    }
    Sleep(2);
  }
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  const char* port = "COM8";
  int baud = 115200;
  bool init = false;
  bool doScan = false;
  int watchId = 0;
  int watchSeconds = 0;
  int statusId = 0;
  int stopId = 0;
  int speedId = 0;
  int speedDps = 0;
  int speedSeconds = 0;
  int torqueId = 0;
  double torqueAmps = 0.0;
  int torqueSeconds = 0;
  const char* rawHex = nullptr;
  int rawId = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](int offset) -> const char* {
      return (i + offset < argc) ? argv[i + offset] : nullptr;
    };
    if (arg == "--port") { if (const char* v = next(1)) { port = v; ++i; } }
    else if (arg == "--baud") { if (const char* v = next(1)) { baud = std::atoi(v); ++i; } }
    else if (arg == "--init") { init = true; }
    else if (arg == "--scan") { doScan = true; }
    else if (arg == "--status") { if (const char* v = next(1)) { statusId = std::atoi(v); ++i; } }
    else if (arg == "--watch") {
      if (const char* a = next(1)) { watchId = std::atoi(a); ++i; }
      if (const char* b = next(1)) { watchSeconds = std::atoi(b); ++i; }
    } else if (arg == "--stop") {
      if (const char* v = next(1)) { stopId = std::atoi(v); ++i; }
    } else if (arg == "--speed") {
      if (const char* a = next(1)) { speedId = std::atoi(a); ++i; }
      if (const char* b = next(1)) { speedDps = std::atoi(b); ++i; }
      if (const char* c = next(1)) { speedSeconds = std::atoi(c); ++i; }
    } else if (arg == "--cmd") {
      if (const char* a = next(1)) { rawId = std::atoi(a); ++i; }
      if (const char* b = next(1)) { rawHex = b; ++i; }
    } else if (arg == "--torque") {
      if (const char* a = next(1)) { torqueId = std::atoi(a); ++i; }
      if (const char* b = next(1)) { torqueAmps = std::atof(b); ++i; }
      if (const char* c = next(1)) { torqueSeconds = std::atoi(c); ++i; }
    } else if (arg == "--help" || arg == "-h") {
      std::printf("usage: wheel_can_probe --port COM8 [--init] [--scan] "
                  "[--status id] [--watch id sec] [--cmd id hex] "
                  "[--speed id dps sec] [--torque id amps sec] [--stop id]\n");
      return 0;
    }
  }

  HANDLE handle = openPort(port, baud);
  if (handle == INVALID_HANDLE_VALUE) return 2;

  if (init) {
    std::printf("INIT_SLCAN S8 + O\n");
    writeRaw(handle, "C\r");
    Sleep(80);
    writeRaw(handle, "S8\r");
    Sleep(80);
    writeRaw(handle, "O\r");
    Sleep(150);
  }

  if (doScan) runScan(handle);
  if (statusId > 0) runStatus(handle, statusId);
  if (watchId > 0 && watchSeconds > 0) runWatch(handle, watchId, watchSeconds);
  if (rawHex != nullptr && rawId > 0) runRawCommand(handle, rawId, rawHex);
  if (stopId > 0) {
    Frame stop;
    stop.id = kCmdBase + static_cast<std::uint32_t>(stopId);
    stop.data[0] = 0x81;
    sendFrame(handle, stop);
    std::printf("STOP_SENT id=%d\n", stopId);
  }
  if (speedId > 0 && speedSeconds > 0) {
    runSpeed(handle, speedId, speedDps, speedSeconds);
  }
  if (torqueId > 0 && torqueSeconds > 0) {
    runTorque(handle, torqueId, torqueAmps, torqueSeconds);
  }

  CloseHandle(handle);
  std::printf("DONE\n");
  return 0;
}
