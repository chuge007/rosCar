#include "robot_app_logger.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "utf8_compat.h"

namespace crawling {
namespace {
// Raw-image correction diagnostics are intentionally detailed enough to
// replay the laser profile. Retain a complete correction run instead of
// rotating away its initial survey after only a few minutes.
constexpr qint64 kMaximumBytes = 16 * 1024 * 1024;
constexpr qint64 kRetainedBytes = 12 * 1024 * 1024;
constexpr qint64 kProfileDetectionMaximumBytes = 64 * 1024 * 1024;
constexpr qint64 kProfileDetectionRetainedBytes = 48 * 1024 * 1024;
constexpr qint64 kCameraConnectionMaximumBytes = 8 * 1024 * 1024;
constexpr qint64 kCameraConnectionRetainedBytes = 6 * 1024 * 1024;
constexpr std::size_t kMaximumNormalRecords = 32768;
constexpr std::size_t kFlushThreshold = 256;
constexpr std::size_t kMaximumDrainBatch = 4096;
constexpr auto kFlushInterval = std::chrono::milliseconds(50);

enum class LogChannel { Main, Profile };

struct QueuedLogRecord {
  QString level;
  QString category;
  QString message;
  qint64 epochMs = 0;
  quintptr sourceThreadId = 0;
  LogChannel channel = LogChannel::Main;
  bool cameraCopy = false;
  bool critical = false;
};

struct ResetBarrier {
  std::mutex mutex;
  std::condition_variable condition;
  bool completed = false;
  bool success = false;
};

struct LogJob {
  enum class Type { Record, ResetProfile, Stop };
  Type type = Type::Record;
  QueuedLogRecord record;
  std::shared_ptr<ResetBarrier> resetBarrier;
};

bool isCameraConnectionCategory(const QString& category, const QString& level) {
  static const QStringList categories = {
      QStringLiteral("CAMERA.CONNECTION"),
      QStringLiteral("CAMERA.DISCOVERY"),
      QStringLiteral("CAMERA.SDK"),
      QStringLiteral("CAMERA.CONFIG"),
      QStringLiteral("CAMERA.ACQUISITION"),
      QStringLiteral("CAMERA.PAYLOAD"),
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("USB_CAMERA.DISCOVERY"),
      QStringLiteral("USB_CAMERA.ACQUISITION"),
  };
  if (categories.contains(category))
    return true;
  return level != QLatin1String("INFO") &&
         (category.startsWith(QLatin1String("CAMERA.")) ||
          category.startsWith(QLatin1String("USB_CAMERA.")));
}

bool isCriticalLog(const QString& level, const QString& category,
                   const QString& message) {
  if (level == QLatin1String("WARN") ||
      level == QLatin1String("ERROR") ||
      level == QLatin1String("CRITICAL") ||
      level == QLatin1String("FATAL"))
    return true;
  if (category.startsWith(QLatin1String("SAFETY.")) ||
      category.startsWith(QLatin1String("ESTOP")) ||
      category.contains(QLatin1String("SAFETY")) ||
      category.contains(QLatin1String("FAULT")))
    return true;
  return message.contains(QLatin1String("急停")) ||
         message.contains(QLatin1String("安全停止")) ||
         message.contains(QLatin1String("emergency_stop"),
                          Qt::CaseInsensitive) ||
         message.contains(QLatin1String("estop"), Qt::CaseInsensitive);
}

qint64 nowMs() {
  return QDateTime::currentMSecsSinceEpoch();
}

quintptr currentThreadId() {
  return reinterpret_cast<quintptr>(QThread::currentThreadId());
}

class LoggerEngine final {
 public:
  LoggerEngine()
      : consolePath_(QDir(QCoreApplication::applicationDirPath())
                         .filePath(CRAWLING_TEXT("logs/robot_console.log"))),
        cameraPath_(QDir(QCoreApplication::applicationDirPath())
                        .filePath(CRAWLING_TEXT("logs/camera_connection.log"))),
        profilePath_(QDir(QCoreApplication::applicationDirPath())
                         .filePath(CRAWLING_TEXT("logs/profile_weld_detection.log"))),
        processId_(QCoreApplication::applicationPid()) {
    QDir().mkpath(QFileInfo(consolePath_).dir().absolutePath());
    worker_ = std::thread(&LoggerEngine::run, this);
  }

  ~LoggerEngine() { shutdown(); }

  void enqueue(const QString& level, const QString& category,
               const QString& message, LogChannel channel = LogChannel::Main) {
    const qint64 timestamp = nowMs();
    const quintptr threadId = currentThreadId();
    const bool critical = isCriticalLog(level, category, message);
    const bool cameraCopy = channel == LogChannel::Main &&
                            isCameraConnectionCategory(category, level);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (!critical && normalQueued_ >= kMaximumNormalRecords) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    LogJob job;
    job.type = LogJob::Type::Record;
    job.record = {level, category, message, timestamp, threadId, channel,
                  cameraCopy, critical};
    jobs_.push_back(std::move(job));
    if (critical) {
      immediateWake_ = true;
      condition_.notify_one();
    } else {
      ++normalQueued_;
      if (normalQueued_ >= kFlushThreshold) {
        immediateWake_ = true;
        condition_.notify_one();
      }
    }
  }

  void enqueueBatch(const QString& level, const QString& category,
                    const QStringList& messages,
                    LogChannel channel = LogChannel::Main) {
    if (messages.isEmpty())
      return;
    const qint64 timestamp = nowMs();
    const quintptr threadId = currentThreadId();
    const bool baseCritical = isCriticalLog(level, category, QString());
    const bool cameraCopy = channel == LogChannel::Main &&
                            isCameraConnectionCategory(category, level);
    bool wake = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) {
      dropped_.fetch_add(quint64(messages.size()), std::memory_order_relaxed);
      return;
    }
    for (const QString& message : messages) {
      const bool critical =
          baseCritical || isCriticalLog(level, category, message);
      if (!critical && normalQueued_ >= kMaximumNormalRecords) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      LogJob job;
      job.type = LogJob::Type::Record;
      job.record = {level, category, message, timestamp, threadId, channel,
                    cameraCopy, critical};
      jobs_.push_back(std::move(job));
      if (critical) {
        wake = true;
      } else {
        ++normalQueued_;
      }
    }
    if (wake || normalQueued_ >= kFlushThreshold) {
      immediateWake_ = true;
      condition_.notify_one();
    }
  }

  bool resetProfileDetection() {
    auto barrier = std::make_shared<ResetBarrier>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!accepting_)
        return false;
      LogJob job;
      job.type = LogJob::Type::ResetProfile;
      job.resetBarrier = barrier;
      jobs_.push_back(std::move(job));
      immediateWake_ = true;
    }
    condition_.notify_one();
    std::unique_lock<std::mutex> lock(barrier->mutex);
    barrier->condition.wait(lock, [&barrier] { return barrier->completed; });
    return barrier->success;
  }

  void shutdown() {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    if (stopped_)
      return;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (accepting_) {
        accepting_ = false;
        LogJob job;
        job.type = LogJob::Type::Stop;
        jobs_.push_back(std::move(job));
        immediateWake_ = true;
      }
    }
    condition_.notify_one();
    if (worker_.joinable())
      worker_.join();
    stopped_ = true;
  }

 private:
  void run() {
    for (;;) {
      QVector<LogJob> batch;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, kFlushInterval, [this] {
          return immediateWake_ || stoppingEnqueued_;
        });
        immediateWake_ = false;
        batch.reserve(int(std::min(kMaximumDrainBatch, jobs_.size())));
        bool stopEnqueued = false;
        while (!jobs_.empty() && batch.size() < int(kMaximumDrainBatch)) {
          LogJob job = std::move(jobs_.front());
          jobs_.pop_front();
          if (job.type == LogJob::Type::Record && !job.record.critical &&
              normalQueued_ > 0) {
            --normalQueued_;
          }
          if (job.type == LogJob::Type::Stop)
            stopEnqueued = true;
          batch.push_back(std::move(job));
        }
        if (stopEnqueued)
          stoppingEnqueued_ = true;
      }

      bool stop = false;
      processBatch(batch, &stop);
      if (stop)
        return;
    }
  }

  void processBatch(const QVector<LogJob>& batch, bool* stop) {
    QVector<QueuedLogRecord> records;
    for (const LogJob& job : batch) {
      switch (job.type) {
        case LogJob::Type::Record:
          records.push_back(job.record);
          break;
        case LogJob::Type::ResetProfile: {
          flush(records);
          records.clear();
          const bool success = truncateProfileFile();
          if (job.resetBarrier) {
            {
              std::lock_guard<std::mutex> lock(job.resetBarrier->mutex);
              job.resetBarrier->success = success;
              job.resetBarrier->completed = true;
            }
            job.resetBarrier->condition.notify_all();
          }
          break;
        }
        case LogJob::Type::Stop:
          flush(records);
          records.clear();
          *stop = true;
          return;
      }
    }
    flush(records);
  }

  bool truncateProfileFile() {
    QDir().mkpath(QFileInfo(profilePath_).dir().absolutePath());
    QFile file(profilePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      return false;
    profileSequence_ = 0;
    return true;
  }

  void flush(QVector<QueuedLogRecord>& records) {
    const quint64 dropped = dropped_.exchange(0, std::memory_order_relaxed);
    if (dropped > 0) {
      QueuedLogRecord notice;
      notice.level = QStringLiteral("WARN");
      notice.category = QStringLiteral("LOGGER.OVERFLOW");
      notice.message = QStringLiteral("event=queue_overflow dropped=%1").arg(dropped);
      notice.epochMs = nowMs();
      notice.sourceThreadId = currentThreadId();
      notice.critical = true;
      records.prepend(std::move(notice));
    }
    if (records.isEmpty())
      return;

    QVector<const QueuedLogRecord*> mainRecords;
    QVector<const QueuedLogRecord*> cameraRecords;
    QVector<const QueuedLogRecord*> profileRecords;
    for (const QueuedLogRecord& record : records) {
      if (record.channel == LogChannel::Profile) {
        profileRecords.push_back(&record);
      } else {
        mainRecords.push_back(&record);
        if (record.cameraCopy)
          cameraRecords.push_back(&record);
      }
    }
    writeRecords(consolePath_, mainRecords, kMaximumBytes, kRetainedBytes,
                 mainSequence_);
    writeRecords(cameraPath_, cameraRecords, kCameraConnectionMaximumBytes,
                 kCameraConnectionRetainedBytes, cameraSequence_);
    writeRecords(profilePath_, profileRecords, kProfileDetectionMaximumBytes,
                 kProfileDetectionRetainedBytes, profileSequence_);
  }

  void writeRecords(const QString& path,
                    const QVector<const QueuedLogRecord*>& records,
                    qint64 maximumBytes, qint64 retainedBytes,
                    quint64& sequence) {
    if (records.isEmpty())
      return;
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text))
      return;
    if (file.size() >= maximumBytes) {
      file.seek(qMax<qint64>(0, file.size() - retainedBytes));
      QByteArray retained = file.readAll();
      const int firstLine = retained.indexOf('\n');
      if (firstLine >= 0)
        retained.remove(0, firstLine + 1);
      file.resize(0);
      file.write(retained);
    }
    file.seek(file.size());
    QByteArray lines;
    lines.reserve(int(std::min<std::size_t>(records.size() * 160,
                                            4 * 1024 * 1024)));
    for (const QueuedLogRecord* record : records) {
      const QString line = CRAWLING_TEXT("%1 #%2 [T%3] [P%4] [%5] [%6] %7\n")
          .arg(QDateTime::fromMSecsSinceEpoch(record->epochMs)
                   .toString(CRAWLING_TEXT("yyyy-MM-dd HH:mm:ss.zzz")))
          .arg(++sequence, 8, 10, QLatin1Char('0'))
          .arg(record->sourceThreadId, 0, 16)
          .arg(processId_)
          .arg(record->level, record->category, record->message);
      lines.append(line.toUtf8());
    }
    file.write(lines);
    file.flush();
  }

  const QString consolePath_;
  const QString cameraPath_;
  const QString profilePath_;
  const qint64 processId_;

  std::mutex lifecycleMutex_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<LogJob> jobs_;
  std::thread worker_;
  std::atomic<quint64> dropped_{0};
  std::size_t normalQueued_ = 0;
  bool accepting_ = true;
  bool immediateWake_ = false;
  bool stoppingEnqueued_ = false;
  bool stopped_ = false;
  quint64 mainSequence_ = 0;
  quint64 cameraSequence_ = 0;
  quint64 profileSequence_ = 0;
};

LoggerEngine& loggerEngine() {
  static LoggerEngine engine;
  return engine;
}
}  // namespace

QString AppLogger::filePath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(CRAWLING_TEXT("logs/robot_console.log"));
}

QString AppLogger::cameraConnectionFilePath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(CRAWLING_TEXT("logs/camera_connection.log"));
}

QString AppLogger::profileDetectionFilePath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(CRAWLING_TEXT("logs/profile_weld_detection.log"));
}

void AppLogger::initialize() {
  QDir().mkpath(QFileInfo(filePath()).dir().absolutePath());
  loggerEngine();
}

void AppLogger::shutdown() {
  loggerEngine().shutdown();
}

void AppLogger::write(const QString& category, const QString& message) {
  writeLine(QStringLiteral("INFO"), category, message);
}

void AppLogger::writeBatch(const QString& category, const QStringList& messages) {
  if (messages.isEmpty())
    return;
  loggerEngine().enqueueBatch(QStringLiteral("INFO"), category, messages);
}

bool AppLogger::resetProfileDetection() {
  return loggerEngine().resetProfileDetection();
}

void AppLogger::writeProfileDetection(const QString& message) {
  writeProfileDetectionBatch(QStringList{message});
}

void AppLogger::writeProfileDetectionBatch(const QStringList& messages) {
  if (messages.isEmpty())
    return;
  loggerEngine().enqueueBatch(QStringLiteral("TRACE"),
                              QStringLiteral("LASER.PROFILE"), messages,
                              LogChannel::Profile);
}

void AppLogger::warning(const QString& category, const QString& message) {
  writeLine(QStringLiteral("WARN"), category, message);
}

void AppLogger::error(const QString& category, const QString& message) {
  writeLine(QStringLiteral("ERROR"), category, message);
}

void AppLogger::writeLine(const QString& level, const QString& category,
                          const QString& message) {
  loggerEngine().enqueue(level, category, message);
}

bool ProfileDetectionLogWriter::beginSession() {
  sessionActive_ = AppLogger::resetProfileDetection();
  if (sessionActive_)
    AppLogger::writeProfileDetection(QStringLiteral("event=session_begin"));
  return sessionActive_;
}

void ProfileDetectionLogWriter::endSession(const QString& reason) {
  if (!sessionActive_)
    return;
  QString singleLineReason = reason;
  singleLineReason.replace(QLatin1Char('\r'), QLatin1Char(' '));
  singleLineReason.replace(QLatin1Char('\n'), QLatin1Char(' '));
  AppLogger::writeProfileDetection(
      QStringLiteral("event=session_end reason=\"%1\"")
          .arg(singleLineReason));
  sessionActive_ = false;
}

void ProfileDetectionLogWriter::append(const QString& message) {
  if (!sessionActive_)
    return;
  AppLogger::writeProfileDetection(message);
}

void ProfileDetectionLogWriter::appendBatch(const QStringList& messages) {
  if (!sessionActive_)
    return;
  AppLogger::writeProfileDetectionBatch(messages);
}

void ProfileDetectionLogWriter::appendDiagnosticBatch(const QStringList& messages) {
  // Compact localization diagnostics are independent of the detailed-log checkbox.
  AppLogger::writeBatch(QStringLiteral("LASER.LOCATE"), messages);
}
}  // namespace crawling
