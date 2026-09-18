#pragma once

#include <QString>

namespace crawling {

class AppLogger final {
 public:
  static void initialize();
  static void write(const QString& category, const QString& message);
  static QString filePath();
};

}  // namespace crawling
