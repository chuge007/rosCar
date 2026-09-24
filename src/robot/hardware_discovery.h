#pragma once

#include "drive_types.h"

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace crawling {

struct HardwareDetectionResult {
  bool imuDetected = false;
  QString imuPort;
  int imuBaudRate = 115200;

  bool laserDetected = false;
  QString laserDevice;
  QStringList laserDevices;

  bool wheelDetected = false;
  QString wheelCanPort;
  int wheelSerialBaudRate = 115200;
  int wheelCanBitrate = 1000000;
  QVector<int> wheelMotorIds;

  bool clampDetected = false;
  QString clampCanPort;
  int clampSerialBaudRate = 115200;
  int clampCanBitrate = 500000;
  int clampNodeId = 0;

  QStringList details;
};

class HardwareDiscovery final {
 public:
  static bool probeImuPort(const QString& portName, int baudRate = 115200,
                           int timeoutMs = 700);
  static HardwareDetectionResult probeCanPorts(const QString& excludedPort = {});
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::HardwareDetectionResult)
