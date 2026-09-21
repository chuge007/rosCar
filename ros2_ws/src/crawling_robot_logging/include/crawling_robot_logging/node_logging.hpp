#pragma once

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <system_error>
#include <vector>

#include "rcutils/logging.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace crawling_robot_logging {
namespace detail {

inline constexpr std::size_t kMaxLogLines = 10000;
inline constexpr std::uintmax_t kMaxLogBytes = 2U * 1024U * 1024U;

class ProcessLogFileLock final {
public:
  ProcessLogFileLock() {
#if defined(_WIN32)
    handle_ = ::CreateMutexA(nullptr, FALSE, "Local\\CrawlingRobotRobotLog");
    if (handle_ != nullptr) {
      const DWORD result = ::WaitForSingleObject(handle_, 5000U);
      locked_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
#endif
  }

  ~ProcessLogFileLock() {
#if defined(_WIN32)
    if (locked_) {
      ::ReleaseMutex(handle_);
    }
    if (handle_ != nullptr) {
      ::CloseHandle(handle_);
    }
#endif
  }

  ProcessLogFileLock(const ProcessLogFileLock&) = delete;
  ProcessLogFileLock& operator=(const ProcessLogFileLock&) = delete;

private:
#if defined(_WIN32)
  HANDLE handle_ = nullptr;
  bool locked_ = false;
#endif
};

inline std::string sanitizeFileStem(std::string value) {
  for (char& ch : value) {
    const bool allowed = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') || ch == '_' || ch == '-';
    if (!allowed) {
      ch = '_';
    }
  }
  if (value.empty()) {
    value = "node";
  }
  return value;
}

inline std::string severityLabel(int severity) {
  switch (severity) {
    case RCUTILS_LOG_SEVERITY_DEBUG:
      return "DEBUG";
    case RCUTILS_LOG_SEVERITY_INFO:
      return "INFO";
    case RCUTILS_LOG_SEVERITY_WARN:
      return "WARN";
    case RCUTILS_LOG_SEVERITY_ERROR:
      return "ERROR";
    case RCUTILS_LOG_SEVERITY_FATAL:
      return "FATAL";
    default:
      return "UNKNOWN";
  }
}

inline std::string formatTimestamp(rcutils_time_point_value_t timestamp) {
  const auto time_point = std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::nanoseconds(timestamp)));
  const auto time_value = std::chrono::system_clock::to_time_t(time_point);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time_value);
#else
  localtime_r(&time_value, &local_time);
#endif
  const auto millisecond_part =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()) %
      std::chrono::seconds(1);
  std::ostringstream output;
  output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3)
         << std::setfill('0') << millisecond_part.count();
  return output.str();
}

inline std::string formatMessage(const char* format, va_list* args) {
  if (format == nullptr) {
    return {};
  }

  va_list probe;
  va_copy(probe, *args);
  const int required = std::vsnprintf(nullptr, 0, format, probe);
  va_end(probe);
  if (required < 0) {
    return "<log formatting failed>";
  }

  std::vector<char> buffer(static_cast<std::size_t>(required) + 1U, '\0');
  va_list copy;
  va_copy(copy, *args);
  std::vsnprintf(buffer.data(), buffer.size(), format, copy);
  va_end(copy);
  return std::string(buffer.data(), static_cast<std::size_t>(required));
}

inline std::string buildLine(int severity, const char* name, rcutils_time_point_value_t timestamp,
                             const char* format, va_list* args) {
  std::ostringstream output;
  output << '[' << formatTimestamp(timestamp) << "]"
         << '[' << severityLabel(severity) << ']';
  if (name != nullptr && *name != '\0') {
    output << '[' << name << ']';
  }
  output << ' ' << formatMessage(format, args);
  return output.str();
}

}  // namespace detail

class NodeFileLogger final {
public:
  static NodeFileLogger& instance() {
    static NodeFileLogger logger;
    return logger;
  }

  void initialize(std::string node_name, std::filesystem::path log_root = defaultLogRoot()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
      return;
    }

    node_name_ = detail::sanitizeFileStem(std::move(node_name));
    log_root_ = std::move(log_root);
    std::error_code error;
    std::filesystem::create_directories(log_root_, error);
    // All ROS processes append to one operator-facing log. The node name is
    // retained in each formatted line, so diagnosis remains possible.
    file_path_ = log_root_ / "robot.log";
    loadExistingLog();
    rcutils_logging_set_output_handler(&NodeFileLogger::outputHandler);
    initialized_ = true;
  }

private:
  static std::filesystem::path defaultLogRoot() {
    return std::filesystem::current_path() / "logs";
  }

  static void outputHandler(const rcutils_log_location_t* location, int severity, const char* name,
                            rcutils_time_point_value_t timestamp, const char* format, va_list* args) {
    try {
      auto& logger = instance();
      const std::string line = detail::buildLine(severity, name, timestamp, format, args);
      {
        std::lock_guard<std::mutex> lock(logger.mutex_);
        if (logger.initialized_) {
          logger.appendLineLocked(line);
        }
      }
    } catch (...) {
    }
    rcutils_logging_console_output_handler(location, severity, name, timestamp, format, args);
  }

  void loadExistingLog() {
    detail::ProcessLogFileLock file_lock;
    trimFileLocked();
  }

  void trimFileLocked() {
    lines_.clear();
    std::ifstream input(file_path_);
    std::string line;
    while (std::getline(input, line)) {
      lines_.push_back(line);
      if (lines_.size() > detail::kMaxLogLines) {
        lines_.pop_front();
      }
    }
    if (lines_.size() >= detail::kMaxLogLines) {
      flushLocked();
    }
  }

  void appendLineLocked(const std::string& line) {
    detail::ProcessLogFileLock file_lock;
    std::ofstream output(file_path_, std::ios::binary | std::ios::app);
    if (output) {
      output << line << '\n';
    }
    std::error_code error;
    const auto file_size = std::filesystem::file_size(file_path_, error);
    if (!error && file_size > detail::kMaxLogBytes) {
      trimFileLocked();
    }
  }

  void flushLocked() {
    std::ofstream output(file_path_, std::ios::binary | std::ios::trunc);
    if (!output) {
      return;
    }
    for (const auto& line : lines_) {
      output << line << '\n';
    }
  }

  bool initialized_ = false;
  std::string node_name_;
  std::filesystem::path log_root_;
  std::filesystem::path file_path_;
  std::deque<std::string> lines_;
  std::mutex mutex_;
};

inline void installNodeFileLogger(const std::string& node_name,
                                  const std::filesystem::path& log_root = std::filesystem::path()) {
  if (log_root.empty()) {
    NodeFileLogger::instance().initialize(node_name);
  } else {
    NodeFileLogger::instance().initialize(node_name, log_root);
  }
}

}  // namespace crawling_robot_logging
