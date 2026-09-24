#include "usbbootstrap.h"

namespace UsbBootstrap {

DeviceMode detectDeviceMode() { return DeviceMode::Streamer; }

bool loadFirmware(QApplication &, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    return true;
}

bool cycleStreamerPort(QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    return true;
}

bool prepare(QApplication &, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    return true;
}

}
