#include "crawling_robot_drivers/win32_serial_port.hpp"

#include <windows.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace crawling_robot_drivers {
namespace {

std::string lastError(const std::string& action) {
  return action + " failed with Win32 error " + std::to_string(GetLastError()) + ".";
}

std::string normalizePortName(const std::string& port_name) {
  if (port_name.rfind("\\\\.\\", 0) == 0) {
    return port_name;
  }
  return "\\\\.\\" + port_name;
}

HANDLE asHandle(void* handle) {
  return static_cast<HANDLE>(handle);
}

}  // namespace

Win32SerialPort::~Win32SerialPort() {
  close();
}

void Win32SerialPort::open(const std::string& port_name, std::uint32_t baud_rate) {
  close();

  const auto port = normalizePortName(port_name);
  HANDLE handle = CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    throw std::runtime_error(lastError("Opening serial port " + port_name));
  }

  DCB settings{};
  settings.DCBlength = sizeof(settings);
  if (!GetCommState(handle, &settings)) {
    CloseHandle(handle);
    throw std::runtime_error(lastError("Reading serial settings"));
  }
  settings.BaudRate = baud_rate;
  settings.ByteSize = 8;
  settings.Parity = NOPARITY;
  settings.StopBits = ONESTOPBIT;
  settings.fBinary = TRUE;
  settings.fParity = FALSE;
  settings.fOutxCtsFlow = FALSE;
  settings.fOutxDsrFlow = FALSE;
  settings.fDtrControl = DTR_CONTROL_DISABLE;
  settings.fRtsControl = RTS_CONTROL_DISABLE;
  if (!SetCommState(handle, &settings)) {
    CloseHandle(handle);
    throw std::runtime_error(lastError("Writing serial settings"));
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 1000;
  if (!SetCommTimeouts(handle, &timeouts)) {
    CloseHandle(handle);
    throw std::runtime_error(lastError("Writing serial timeouts"));
  }

  PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
  handle_ = handle;
}

void Win32SerialPort::close() noexcept {
  if (handle_ != nullptr) {
    CloseHandle(asHandle(handle_));
    handle_ = nullptr;
  }
}

bool Win32SerialPort::isOpen() const noexcept {
  return handle_ != nullptr;
}

std::size_t Win32SerialPort::read(std::uint8_t* destination, std::size_t capacity) {
  if (!isOpen()) {
    throw std::runtime_error("Serial port is not open.");
  }
  const DWORD request = static_cast<DWORD>(std::min<std::size_t>(capacity, MAXDWORD));
  DWORD read_count = 0;
  if (!ReadFile(asHandle(handle_), destination, request, &read_count, nullptr)) {
    throw std::runtime_error(lastError("Reading serial port"));
  }
  return static_cast<std::size_t>(read_count);
}

void Win32SerialPort::writeAll(const std::vector<std::uint8_t>& bytes) {
  if (!isOpen()) {
    throw std::runtime_error("Serial port is not open.");
  }
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD request = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size() - offset, static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    if (!WriteFile(asHandle(handle_), bytes.data() + offset, request, &written, nullptr)) {
      throw std::runtime_error(lastError("Writing serial port"));
    }
    if (written == 0) {
      throw std::runtime_error("Serial port write returned zero bytes.");
    }
    offset += written;
  }
}

}  // namespace crawling_robot_drivers
