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

namespace crawling_robot_logging {
namespace detail {

inline constexpr std::size_t kMaxLogLines = 10000;

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
    file_path_ = log_root_ / (node_name_ + ".log");
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
    lines_.clear();
    std::ifstream input(file_path_);
    std::string line;
    while (std::getline(input, line)) {
      lines_.push_back(line);
      if (lines_.size() > detail::kMaxLogLines) {
        lines_.pop_front();
      }
    }
    flushLocked();
  }

  void appendLineLocked(const std::string& line) {
    lines_.push_back(line);
    if (lines_.size() > detail::kMaxLogLines) {
      while (lines_.size() > detail::kMaxLogLines) {
        lines_.pop_front();
      }
      flushLocked();
      return;
    }

    std::ofstream output(file_path_, std::ios::binary | std::ios::app);
    if (output) {
      output << line << '\n';
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
