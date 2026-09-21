#pragma once

#include <QString>

namespace crawling {

class AppLogger final {
 public:
  static void initialize();
  static void write(const QString& category, const QString& message);
  static void warning(const QString& category, const QString& message);
  static void error(const QString& category, const QString& message);
  static QString filePath();

 private:
  static void writeLine(const QString& level, const QString& category,
                        const QString& message);
};

}  // namespace crawling
