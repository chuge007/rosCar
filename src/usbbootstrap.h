#pragma once

#include <QString>

class QApplication;

namespace UsbBootstrap {

enum class DeviceMode {
    Missing,
    BootLoader,
    Streamer
};

DeviceMode detectDeviceMode();

// Load the vendor USB/FPGA firmware after a BootLoader device is inserted.
// This variant is intentionally callable after Client has been constructed:
// the SDK must already be alive to observe the resulting Streamer arrival.
bool loadFirmware(QApplication &app, QString *errorMessage = nullptr);

// Ask the Cypress cyusb3 driver to simulate a physical disconnect/reconnect.
// The PA SDK must already be alive when this happens so that it receives the
// board-arrival event which is otherwise missed on a cold application start.
bool cycleStreamerPort(QString *errorMessage = nullptr);

// Must run before Client::getInstance().  The vendor SDK snapshots the USB
// device list during construction and cannot recover if firmware appears later.
bool prepare(QApplication &app, QString *errorMessage = nullptr);

}
