#include "usbbootstrap.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QStringList>
#include <QThread>

namespace UsbBootstrap {

DeviceMode detectDeviceMode() { return DeviceMode::Streamer; }

bool loadFirmware(QApplication &, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    return true;
}

bool cycleStreamerPort(QString *errorMessage) {
    if (errorMessage)
        errorMessage->clear();
    // Request a non-destructive Windows PnP rescan.  This is intentionally
    // detached: the GUI must remain responsive while Windows and cyusb3 finish
    // enumerating the board. DeviceController performs the delayed SDK retry.
    const bool started = QProcess::startDetached(
        QStringLiteral("pnputil.exe"),
        QStringList{QStringLiteral("/scan-devices")});
    if (!started && errorMessage) {
        *errorMessage = QStringLiteral(
            "无法启动 Windows 设备重新枚举；将继续使用 SDK 内部恢复流程。");
    }
    return started;
}

bool prepare(QApplication &app, QString *errorMessage) {
    if (errorMessage)
        errorMessage->clear();
    QString scanError;
    cycleStreamerPort(&scanError);

    // A power-cycled board can still be moving from USB enumeration into its
    // streamer state. Delay construction of Client, which snapshots physical
    // devices only once, while keeping the Windows event queue responsive.
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 2500) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(25);
    }
    if (!scanError.isEmpty() && errorMessage)
        *errorMessage = scanError;
    return true;
}

}
