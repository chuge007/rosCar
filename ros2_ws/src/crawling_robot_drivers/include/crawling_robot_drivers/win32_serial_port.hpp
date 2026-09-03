#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crawling_robot_drivers {

class Win32SerialPort {
public:
  Win32SerialPort() = default;
  ~Win32SerialPort();

  Win32SerialPort(const Win32SerialPort&) = delete;
  Win32SerialPort& operator=(const Win32SerialPort&) = delete;

  void open(const std::string& port_name, std::uint32_t baud_rate);
  void close() noexcept;
  bool isOpen() const noexcept;
  std::size_t read(std::uint8_t* destination, std::size_t capacity);
  void writeAll(const std::vector<std::uint8_t>& bytes);

private:
  void* handle_ = nullptr;
};

}  // namespace crawling_robot_drivers
