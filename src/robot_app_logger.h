#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace crawling {

class AppLogger final {
 public:
  static void initialize();
  static void shutdown();
  static void write(const QString& category, const QString& message);
  static void writeBatch(const QString& category, const QStringList& messages);
  static bool resetProfileDetection();
  static void writeProfileDetection(const QString& message);
  static void writeProfileDetectionBatch(const QStringList& messages);
  static void warning(const QString& category, const QString& message);
  static void error(const QString& category, const QString& message);
  static QString filePath();
  static QString cameraConnectionFilePath();
  static QString profileDetectionFilePath();

 private:
  static void writeLine(const QString& level, const QString& category,
                        const QString& message);
};

class ProfileDetectionLogWriter final : public QObject {
 public:
  bool beginSession();
  void endSession(const QString& reason);
  void append(const QString& message);
  void appendBatch(const QStringList& messages);
  void appendDiagnosticBatch(const QStringList& messages);

 private:
  bool sessionActive_ = false;
};

}  // namespace crawling

