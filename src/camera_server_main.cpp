// Camera Server - Standalone process for camera capture
// Isolates camera I/O from robot control process
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>
#include "camera_server.h"

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("PA1664CameraServer");
  app.setApplicationVersion("1.0");

  QCommandLineParser parser;
  parser.setApplicationDescription("Camera capture server for PA1664 Robot");
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption ipcKeyOption(QStringList() << "k" << "ipc-key",
                                   "IPC shared memory key base",
                                   "key", "PA1664Camera");
  parser.addOption(ipcKeyOption);

  parser.process(app);

  const QString ipcKey = parser.value(ipcKeyOption);

  crawling::CameraServer server(ipcKey);
  if (!server.start()) {
    qCritical() << "Failed to start camera server";
    return 1;
  }

  qInfo() << "Camera server started with IPC key:" << ipcKey;
  qInfo() << "Ready";  // Signal to parent process

  return app.exec();
}
