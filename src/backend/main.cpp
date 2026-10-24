#include "host_application.h"
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
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

QMutex g_logMutex;
QFile g_logFile;

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
      g_logFile.write(line.toUtf8());
      g_logFile.flush();
    }
  }
  if (type == QtFatalMsg) std::abort();
}

bool openHostLog(QString* error) {
  const QDir logDir(QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("logs")));
  if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
    if (error) *error = QStringLiteral("无法创建日志目录");
    return false;
  }
  g_logFile.setFileName(logDir.filePath(QStringLiteral("robot_host.log")));
  if (!g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    if (error) *error = QStringLiteral("无法打开主机日志文件");
    return false;
  }
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
  if (!instanceLock.tryLock(100)) {
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
