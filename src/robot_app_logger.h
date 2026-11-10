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
  // WARN/ERROR/CRITICAL only. The first file to read when something failed: the
  // main console log rotates away the first error long before the tail.
  static QString errorFilePath();

  // Per-file retention, shared by the host and the workbench. The numbers are
  // chosen so that a single full read still fits in an assistant's context
  // (~45 tokens per line), while keeping enough history to grep a real failure.
  // Anything that needs more history should be located by grep instead of by
  // raising these limits.
  struct Retention {
    static constexpr int HostLog = 800;   // robot_host.log
    static constexpr int Console = 500;   // robot_console.log
    static constexpr int Camera = 300;    // camera_connection.log
    static constexpr int Comm = 200;      // logs/comm/*.log
    static constexpr int Profile = 3000;  // profile_weld_detection.log
    static constexpr int Errors = 200;    // errors.log (WARN/ERROR only)
  };
  // Trims `path` in place to its last `maxLines` lines. Returns false when the
  // file cannot be opened (missing or locked).
  static bool trimToLastLines(const QString& path, int maxLines);
  // Deletes files in `directory` whose name matches `namePattern`, keeping only
  // the `keepNewest` most recently modified ones. Pass 0 to remove all matches.
  // Returns the number of files removed.
  static int removeOldSessionLogs(const QString& directory,
                                  const QString& namePattern, int keepNewest);

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

