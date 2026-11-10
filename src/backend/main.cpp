#include "host_application.h"
#include "robot_app_logger.h"
#include "usbbootstrap.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QHostAddress>
#include <QLockFile>
#include <QMutex>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

QMutex g_logMutex;
QFile g_logFile;
QString g_logPath;
int g_logLineCount = 0;
// The host runs unattended for days. Keep only the newest lines so
// robot_host.log cannot grow without bound; AppLogger applies the same policy to
// the other log files.
constexpr int kMaximumHostLogLines = crawling::AppLogger::Retention::HostLog;

void hostMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
  const char* level = "INFO";
  switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
  }
  const QString line = QStringLiteral("%1 [%2] %3\n")
                           .arg(QDateTime::currentDateTime().toString(
                                    QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                QString::fromLatin1(level), message);
  {
    QMutexLocker locker(&g_logMutex);
    std::fwrite(line.toUtf8().constData(), 1,
                static_cast<size_t>(line.toUtf8().size()), stdout);
    std::fflush(stdout);
    if (g_logFile.isOpen()) {
      if (g_logLineCount >= kMaximumHostLogLines) {
        // Trim one line below the limit so the line about to be written lands at
        // exactly kMaximumHostLogLines instead of one over it. Closing first
        // keeps the trim from fighting the append handle on Windows.
        g_logPath = g_logFile.fileName();
        g_logFile.close();
        crawling::AppLogger::trimToLastLines(g_logPath,
                                             kMaximumHostLogLines - 1);
        g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        g_logLineCount = kMaximumHostLogLines - 1;
      }
      if (g_logFile.isOpen()) {
        g_logFile.write(line.toUtf8());
        g_logFile.flush();
        ++g_logLineCount;
      }
    }
  }
  // Warnings and errors also go to logs/errors.log through AppLogger, which
  // applies its own retention. INFO/DEBUG stay out of that file on purpose.
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
    crawling::AppLogger::error(QStringLiteral("HOST"), message);
  if (type == QtFatalMsg) std::abort();
}

bool openHostLog(QString* error) {
  const QDir logDir(QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("logs")));
  if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
    if (error) *error = QStringLiteral("无法创建日志目录");
    return false;
  }
  g_logPath = logDir.filePath(QStringLiteral("robot_host.log"));
  // Whatever the previous run left behind is trimmed before the first write, so
  // a restarted host does not keep appending to an oversized file. Trim while no
  // append handle is open to avoid two handles racing on the same file.
  crawling::AppLogger::trimToLastLines(g_logPath, kMaximumHostLogLines);
  g_logFile.setFileName(g_logPath);
  if (!g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    if (error) *error = QStringLiteral("无法打开主机日志文件");
    return false;
  }
  g_logLineCount = 0;
  QFile existing(g_logPath);
  if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const QByteArray tail = existing.readAll();
    for (char ch : tail) {
      if (ch == '\n') ++g_logLineCount;
    }
  }
  g_logLineCount = qMin(g_logLineCount, kMaximumHostLogLines);
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PA1664"));
  QCoreApplication::setApplicationName(QStringLiteral("PA1664RobotHost"));
  QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
  QDir::setCurrent(QCoreApplication::applicationDirPath());

  QString usbPreparationWarning;
  UsbBootstrap::prepare(app, &usbPreparationWarning);

  QString logError;
  openHostLog(&logError);
  qInstallMessageHandler(hostMessageHandler);
  if (!logError.isEmpty()) qWarning().noquote() << logError;
  if (!usbPreparationWarning.isEmpty())
    qWarning().noquote() << "USB_PREPARE:" << usbPreparationWarning;

  // The ultrasound SDK writes one log_YYYYMMDD_HHMMSS.txt per run and never
  // removes old ones; after a few dozen restarts the directory is full of them.
  // Drop the previous runs' files here (the current run has not created its own
  // yet, because the ultrasound service starts later). The SDK keeps writing its
  // own file for this session, so exactly one remains on disk at any time.
  {
    const QDir sdkLogDir(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("logs")));
    const int removed = crawling::AppLogger::removeOldSessionLogs(
        sdkLogDir.absolutePath(),
        QStringLiteral("^log_\\d{8}_\\d{6}\\.txt$"), 0);
    if (removed > 0)
      qInfo().noquote() << "SESSION_LOG_CLEANUP: removed" << removed
                        << "old ultrasound log files";
  }

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("PA1664 小车无界面控制主机（下位机）"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption listenOption(
      {QStringLiteral("l"), QStringLiteral("listen")},
      QStringLiteral("监听地址，默认 0.0.0.0。"), QStringLiteral("address"),
      QStringLiteral("0.0.0.0"));
  const QCommandLineOption controlPortOption(
      QStringLiteral("control-port"), QStringLiteral("可靠控制 TCP 端口。"),
      QStringLiteral("port"), QString::number(pa1664::remote::kDefaultControlPort));
  const QCommandLineOption realtimePortOption(
      QStringLiteral("realtime-port"), QStringLiteral("实时 UDP 端口。"),
      QStringLiteral("port"), QString::number(pa1664::remote::kDefaultRealtimePort));
  const QCommandLineOption mediaPortOption(
      QStringLiteral("media-port"), QStringLiteral("低延迟媒体 UDP 端口。"),
      QStringLiteral("port"), QString::number(pa1664::remote::kDefaultMediaPort));
  const QCommandLineOption bulkPortOption(
      QStringLiteral("bulk-port"), QStringLiteral("可靠批量 TCP 端口。"),
      QStringLiteral("port"), QString::number(pa1664::remote::kDefaultBulkPort));
  parser.addOption(listenOption);
  parser.addOption(controlPortOption);
  parser.addOption(realtimePortOption);
  parser.addOption(mediaPortOption);
  parser.addOption(bulkPortOption);
  parser.process(app);

  bool portOk = false;
  const uint controlPort = parser.value(controlPortOption).toUInt(&portOk);
  if (!portOk || controlPort == 0 || controlPort > 65535) {
    qCritical() << "控制端口无效";
    return 2;
  }
  const uint realtimePort = parser.value(realtimePortOption).toUInt(&portOk);
  if (!portOk || realtimePort == 0 || realtimePort > 65535) {
    qCritical() << "实时端口无效";
    return 2;
  }
  const uint mediaPort = parser.value(mediaPortOption).toUInt(&portOk);
  if (!portOk || mediaPort == 0 || mediaPort > 65535) {
    qCritical() << "媒体端口无效";
    return 2;
  }
  const uint bulkPort = parser.value(bulkPortOption).toUInt(&portOk);
  if (!portOk || bulkPort == 0 || bulkPort > 65535) {
    qCritical() << "批量端口无效";
    return 2;
  }
  const QSet<uint> ports{controlPort, realtimePort, mediaPort, bulkPort};
  if (ports.size() != 4) {
    qCritical() << "控制、实时、媒体、批量端口必须互不相同";
    return 2;
  }

  QHostAddress address;
  if (!address.setAddress(parser.value(listenOption).trimmed())) {
    qCritical() << "监听地址无效";
    return 2;
  }

  const QString lockPath = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("robot_host.lock"));
  QLockFile instanceLock(lockPath);
  instanceLock.setStaleLockTime(15000);
  bool haveInstanceLock = instanceLock.tryLock(100);
  if (!haveInstanceLock) {
    // A killed instance leaves a lock file behind. QLockFile decides whether it
    // is stale by checking if the recorded PID still exists, and Windows reuses
    // PIDs quickly: once that number belongs to an unrelated process the lock
    // looks alive forever, so every double-click exits with "已有实例运行" and
    // the console window closes before anyone can read it. Confirm against the
    // real process list before giving up.
    QProcess probe;
    probe.start(QStringLiteral("tasklist"),
                {QStringLiteral("/FI"),
                 QStringLiteral("IMAGENAME eq PA1664RobotHost.exe"),
                 QStringLiteral("/NH")});
    bool known = false;
    if (probe.waitForStarted(2000) && probe.waitForFinished(3000)) {
      const QString output =
          QString::fromLocal8Bit(probe.readAllStandardOutput());
      known = output.contains(QStringLiteral("PA1664RobotHost.exe"),
                              Qt::CaseInsensitive);
    } else {
      // Cannot verify: keep behaving like before rather than stealing a lock
      // that may be held by a live instance.
      known = true;
    }
    if (!known && QFile::remove(lockPath)) {
      qInfo() << "清理残留的单实例锁:" << lockPath;
      haveInstanceLock = instanceLock.tryLock(100);
    }
  }
  if (!haveInstanceLock) {
    qCritical() << "已有 PA1664RobotHost 实例运行";
    return 3;
  }

  pa1664::remote::HostApplication host;
  QObject::connect(&host, &pa1664::remote::HostApplication::fatalError,
                   [](const QString& detail) { qCritical().noquote() << detail; });
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &host,
                   &pa1664::remote::HostApplication::stop);

  QString startError;
  if (!host.start(address, static_cast<quint16>(controlPort),
                  static_cast<quint16>(realtimePort),
                  static_cast<quint16>(mediaPort),
                  static_cast<quint16>(bulkPort), &startError)) {
    qCritical().noquote() << startError;
    return 4;
  }

  qInfo().noquote()
      << QStringLiteral("PA1664RobotHost 已启动：control/tcp=%1:%2 realtime/udp=%1:%3 media/udp=%1:%4 bulk/tcp=%1:%5")
             .arg(address.toString())
             .arg(controlPort)
             .arg(realtimePort)
             .arg(mediaPort)
             .arg(bulkPort);
  qInfo().noquote()
      << "安全要求：软件急停不能替代硬线急停；现场必须保留独立硬线急停回路。";
  return app.exec();
}
