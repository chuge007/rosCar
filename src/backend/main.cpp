#include "host_application.h"
#include "host_autostart.h"
#include "robot_app_logger.h"
#include "usbbootstrap.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QHostAddress>
#include <QLockFile>
#include <QMutex>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// 进程内重启（操作端“重启小车端程序”按钮触发）的第二半：替代进程必须先等
// 旧进程放掉单实例锁、四个监听端口与 USB/CAN 设备句柄再初始化，否则会以
// “已有实例运行”或端口占用直接退出。旧进程可能被主线程卡死拖住，这里给出
// 等待预算后强制结束它，保证主机的自愈闭环一定能走完。
void waitForPreviousHost(qint64 previousPid) {
  using pa1664::remote::kAdminDeviceReleaseGraceMs;
  using pa1664::remote::kAdminForcedKillMs;
  using pa1664::remote::kAdminPreviousExitMs;
  if (previousPid <= 0) return;
  qInfo().noquote() << "HOST_RESTART_WAIT: previous pid" << previousPid;
#ifdef Q_OS_WIN
  HANDLE handle = OpenProcess(
      SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
      static_cast<DWORD>(previousPid));
  if (!handle) {
    qInfo().noquote() << "HOST_RESTART_WAIT: previous pid already gone";
  } else {
    // PID 会被系统快速复用：确认它确实是 RobotHost 才等待/结束，避免误杀
    // 一个恰好拿到同号 PID 的无关进程。
    bool sameImage = true;
    wchar_t imagePath[4096] = {};
    DWORD imageChars = 4096;
    if (QueryFullProcessImageNameW(handle, 0, imagePath, &imageChars)) {
      sameImage = QFileInfo(QString::fromWCharArray(imagePath,
                                                   static_cast<int>(imageChars)))
                      .fileName()
                      .compare(QStringLiteral("PA1664RobotHost.exe"),
                               Qt::CaseInsensitive) == 0;
    }
    if (!sameImage) {
      qWarning().noquote()
          << "HOST_RESTART_WAIT: pid reused by another process, skip waiting";
      CloseHandle(handle);
      return;
    }
    if (WaitForSingleObject(handle, kAdminPreviousExitMs) != WAIT_OBJECT_0) {
      qWarning().noquote()
          << "HOST_RESTART_WAIT: timeout, terminating previous instance";
      TerminateProcess(handle, 1);
      WaitForSingleObject(handle, kAdminForcedKillMs);
    }
    CloseHandle(handle);
  }
#else
  // 当前部署目标只有 Windows（MSVC + Qt）；其它平台保留原有单实例逻辑，
  // 不做跨进程等待。
  Q_UNUSED(previousPid);
#endif
  // 设备句柄不是随进程退出同步回收的（USB 相机/超声设备驱动尤甚），
  // 留一点余量再初始化，避免新进程第一次打开设备就失败。
  QThread::msleep(kAdminDeviceReleaseGraceMs);
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
  // 内部参数：由上一实例在“重启小车端程序”流程中带入。不放进任何 UI，
  // 手工启动主机时不需要它。
  const QCommandLineOption restartAfterPidOption(
      QStringLiteral("restart-after-pid"),
      QStringLiteral("内部使用：等待指定 PID 的旧主机进程退出后再接管。"),
      QStringLiteral("pid"));
  const QCommandLineOption autostartOption(
      QStringLiteral("autostart"),
      QStringLiteral("强制登记开机自启动（忽略关闭标记与部署形态判断）。"));
  const QCommandLineOption noAutostartOption(
      QStringLiteral("no-autostart"), QStringLiteral("本次不处理开机自启动项。"));
  const QCommandLineOption removeAutostartOption(
      QStringLiteral("remove-autostart"),
      QStringLiteral("删除开机自启动项并写入关闭标记。"));
  parser.addOption(autostartOption);
  parser.addOption(noAutostartOption);
  parser.addOption(removeAutostartOption);
  parser.addOption(listenOption);
  parser.addOption(controlPortOption);
  parser.addOption(realtimePortOption);
  parser.addOption(mediaPortOption);
  parser.addOption(bulkPortOption);
  parser.addOption(restartAfterPidOption);
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

  // 纯运维命令：只卸载自启动，不启动服务。放在单实例锁之前，
  // 这样主机正在运行时执行也能生效（否则会被“已有实例运行”挡在前面）。
  if (parser.isSet(removeAutostartOption)) {
    QString removeDetail;
    const bool removed = pa1664::remote::HostAutostart::apply(
        pa1664::remote::HostAutostartMode::Remove, &removeDetail);
    if (removed)
      qInfo().noquote() << "HOST_AUTOSTART REMOVED" << removeDetail;
    else
      qCritical().noquote() << "HOST_AUTOSTART REMOVE_FAILED" << removeDetail;
    return removed ? 0 : 1;
  }

  // --autostart 同样在锁之前处理：现场点 InstallAutoStart 时主机往往已经在跑，
  // 若把登记放在锁之后会被“已有实例运行”挡掉，脚本看起来什么都没做。
  const bool autostartForced =
      parser.isSet(autostartOption) && !parser.isSet(noAutostartOption);
  if (autostartForced) {
    QString forceDetail;
    if (pa1664::remote::HostAutostart::apply(
            pa1664::remote::HostAutostartMode::Force, &forceDetail)) {
      qInfo().noquote() << "HOST_AUTOSTART OK" << forceDetail;
    } else {
      qWarning().noquote() << "HOST_AUTOSTART NOT_INSTALLED" << forceDetail;
    }
  }

  const QString lockPath = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("robot_host.lock"));
  if (parser.isSet(restartAfterPidOption)) {
    waitForPreviousHost(parser.value(restartAfterPidOption).toLongLong());
    // 被强制结束的旧进程不会执行 QLockFile 的析构，锁文件会留在盘上。
    // 此时旧进程已确认退出，直接清掉比让 tryLock 的 stale 判定去猜更可靠。
    if (QFile::exists(lockPath) && QFile::remove(lockPath))
      qInfo().noquote() << "清理进程内重启留下的单实例锁:" << lockPath;
  }
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

  // 自启动自愈：第一次运行即登记，之后覆盖 exe 也不需要重装。
  // 放在单实例锁之后，避免多个实例同时改同一个快捷方式。
  // 任何失败只记录原因，绝不阻塞主机主服务。
  // --remove-autostart 与 --autostart 都已在单实例锁之前处理，
  // 这里只剩默认的 Ensure 与 --no-autostart。
  if (!autostartForced) {
    using pa1664::remote::HostAutostartMode;
    const bool skipped = parser.isSet(noAutostartOption);
    QString autostartDetail;
    const bool installed = pa1664::remote::HostAutostart::apply(
        skipped ? HostAutostartMode::Skip : HostAutostartMode::Ensure,
        &autostartDetail);
    if (skipped)
      qInfo().noquote() << "HOST_AUTOSTART SKIPPED" << autostartDetail;
    else if (installed)
      qInfo().noquote() << "HOST_AUTOSTART OK" << autostartDetail;
    else
      qWarning().noquote() << "HOST_AUTOSTART NOT_INSTALLED" << autostartDetail;
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
