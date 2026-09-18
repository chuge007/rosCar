#include "app_logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include "utf8_compat.h"

namespace crawling {
namespace {
constexpr qint64 kMaximumBytes = 2 * 1024 * 1024;
constexpr qint64 kRetainedBytes = 1536 * 1024;
QMutex& loggerMutex() { static QMutex mutex; return mutex; }
quint64& logSequence() { static quint64 sequence = 0; return sequence; }
}

QString AppLogger::filePath() {
  return QDir(QCoreApplication::applicationDirPath()).filePath(CRAWLING_TEXT("logs/robot_console.log"));
}

void AppLogger::initialize() {
  QMutexLocker lock(&loggerMutex());
  QDir().mkpath(QFileInfo(filePath()).dir().absolutePath());
}

void AppLogger::write(const QString& category, const QString& message) {
  writeLine(QStringLiteral("INFO"), category, message);
}

void AppLogger::warning(const QString& category, const QString& message) {
  writeLine(QStringLiteral("WARN"), category, message);
}

void AppLogger::error(const QString& category, const QString& message) {
  writeLine(QStringLiteral("ERROR"), category, message);
}

void AppLogger::writeLine(const QString& level, const QString& category,
                          const QString& message) {
  QMutexLocker lock(&loggerMutex());
  QDir().mkpath(QFileInfo(filePath()).dir().absolutePath());
  QFile file(filePath());
  if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) return;
  if (file.size() >= kMaximumBytes) {
    file.seek(qMax<qint64>(0, file.size() - kRetainedBytes));
    QByteArray retained = file.readAll();
    const int firstLine = retained.indexOf('\n');
    if (firstLine >= 0) retained.remove(0, firstLine + 1);
    file.resize(0);
    file.write(retained);
  }
  file.seek(file.size());
  const QString line = CRAWLING_TEXT("%1 #%2 [T%3] [%4] [%5] %6\n")
      .arg(QDateTime::currentDateTime().toString(CRAWLING_TEXT("yyyy-MM-dd HH:mm:ss.zzz")))
      .arg(++logSequence(), 8, 10, QLatin1Char('0'))
      .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
      .arg(level, category, message);
  file.write(line.toUtf8());
  file.flush();
}
}  // namespace crawling
