// 最小示例：连接小车主机 → 使能 → 前进 2 秒 → 停车 → 断开。
//
// 运行前确认：
//   1. 小车端 PA1664RobotHost.exe 已启动（C:\Users\a\Desktop\robot_host）；
//   2. 小车端底盘串口/CAN 配置已完成过一次（API 连接时沿用主机已保存的配置）；
//   3. 本机与小车在同一网段，能 ping 通 192.168.66.10。
//
// 命令行：forward_demo [host]
// 退出码：0 正常流程结束，2 连接失败，3 前置请求失败。

#include "pa1664_robot_api.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <exception>

namespace {

void print(const char* tag, const std::string& text) {
  std::printf("[%-8s] %s\n", tag, text.c_str());
}

// 离线自检：不建链、不动电机，逐个调用全部接口，验证符号链接与参数校验。
// 所有调用在未 start() 时都应返回 NotOnline / InvalidArgument。
void expectOffline(const char* name, const pa1664::Result& result, int* failures) {
  const bool expected = result.code == pa1664::ErrorCode::NotOnline ||
                        result.code == pa1664::ErrorCode::InvalidArgument;
  std::printf("[%-8s] %-28s %s\n", expected ? "OK" : "FAIL", name,
              result.message.c_str());
  if (!expected) ++*failures;
}

int runSelfTest(const std::string& iniPath) {
  std::printf("=== PA1664 API 离线自检（不连网络）===\n");

  const pa1664::DriveConfig drive = pa1664::loadDriveConfig(iniPath);
  std::printf("[CONFIG ] path=%s loaded=%d\n", drive.path.c_str(),
              drive.loaded ? 1 : 0);
  if (!drive.loaded) std::printf("[CONFIG ] %s\n", drive.error.c_str());
  if (drive.loaded) {
    std::printf("[CONFIG ] schema=%d maxLinear=%.3f maxAngular=%.3f "
                "clampMotors=%d cameras=%d\n",
                drive.schemaVersion, drive.maximumLinearSpeedMps,
                drive.maximumAngularSpeedRadps,
                int(drive.clampMotors.size()), int(drive.cameras.size()));
  }

  pa1664::RobotApi robot;
  robot.applyDriveConfig(drive);
  robot.setConnectionHandler([](bool, const std::string&) {});
  robot.setStatusHandler([](const pa1664::RobotStatus&) {});
  robot.setResponseHandler([](const std::string&, bool, const std::string&,
                              const std::string&, const std::string&) {});
  robot.setEventHandler([](const std::string&, const std::string&) {});
  robot.setDriveConfigHandler([](const pa1664::DriveConfig&) {});
  robot.setCameraFrameHandler([](const pa1664::CameraFrame&) {});
  robot.setUltrasoundFrameHandler([](const pa1664::UltrasoundAScanFrame&) {});
  robot.setCorrectionStatusHandler([](const pa1664::CorrectionStatus&) {});
  robot.setUltrasoundStatusHandler([](const pa1664::UltrasoundStatus&) {});
  robot.setLogHandler([](pa1664::LogLevel, const std::string&) {});

  int failures = 0;
  expectOffline("connectChassis", robot.connectChassis(), &failures);
  expectOffline("disconnectChassis", robot.disconnectChassis(), &failures);
  expectOffline("enable", robot.enable(true), &failures);
  expectOffline("takeControl", robot.takeControl(), &failures);
  expectOffline("emergencyStop", robot.emergencyStop(), &failures);
  expectOffline("reset", robot.reset(), &failures);
  expectOffline("clearAlarm", robot.clearAlarm(), &failures);
  expectOffline("requestSnapshot", robot.requestSnapshot(), &failures);
  expectOffline("requestDriveConfig", robot.requestDriveConfig(), &failures);
  expectOffline("stopMotionOnHost", robot.stopMotionOnHost(), &failures);
  expectOffline("clampConnect", robot.clampConnect(), &failures);
  expectOffline("clampDisconnect", robot.clampDisconnect(), &failures);
  expectOffline("requestClampStatus", robot.requestClampStatus(), &failures);
  expectOffline("requestClampSettings", robot.requestClampSettings(), &failures);
  expectOffline("scanCameras", robot.scanCameras(), &failures);
  expectOffline("setCameraDisplaySize",
                robot.setCameraDisplaySize(0, 640, 480), &failures);
  expectOffline("setCameraDisplaySize(bad)",
                robot.setCameraDisplaySize(9, 640, 480), &failures);
  expectOffline("requestUltrasoundPresets", robot.requestUltrasoundPresets(),
                &failures);
  expectOffline("switchUltrasoundPreset", robot.switchUltrasoundPreset(1),
                &failures);
  expectOffline("switchUltrasoundPreset(bad)",
                robot.switchUltrasoundPreset(-1), &failures);
  expectOffline("captureUltrasoundPreset", robot.captureUltrasoundPreset(1),
                &failures);
  expectOffline("requestUltrasoundScanPrecision",
                robot.requestUltrasoundScanPrecision(), &failures);
  expectOffline("setUltrasoundScanPrecision",
                robot.setUltrasoundScanPrecision(1, 0.5), &failures);
  expectOffline("requestUltrasoundEncoderCalibration",
                robot.requestUltrasoundEncoderCalibration(), &failures);
  expectOffline("setUltrasoundEncoderCalibration",
                robot.setUltrasoundEncoderCalibration("{}"), &failures);
  expectOffline("ultrasoundResetEncoder", robot.ultrasoundResetEncoder(),
                &failures);
  expectOffline("ultrasoundRefreshGeometry",
                robot.ultrasoundRefreshGeometry(), &failures);
  expectOffline("scanLaserDevices", robot.scanLaserDevices(), &failures);
  expectOffline("connectLaser", robot.connectLaser("DB2666828"), &failures);
  expectOffline("connectLaser(bad)", robot.connectLaser(""), &failures);
  expectOffline("disconnectLaser", robot.disconnectLaser(), &failures);
  expectOffline("requestLaserStatus", robot.requestLaserStatus(), &failures);
  expectOffline("connectImu", robot.connectImu("COM3", 115200), &failures);
  expectOffline("connectImu(bad)", robot.connectImu("", 115200), &failures);
  expectOffline("disconnectImu", robot.disconnectImu(), &failures);
  expectOffline("requestImuStatus", robot.requestImuStatus(), &failures);
  expectOffline("requestPeripheralsSnapshot",
                robot.requestPeripheralsSnapshot(), &failures);
  expectOffline("requestSerialPorts", robot.requestSerialPorts(), &failures);
  expectOffline("forward", robot.forward(0.5), &failures);
  expectOffline("backward", robot.backward(0.5), &failures);
  expectOffline("turnLeft", robot.turnLeft(0.5), &failures);
  expectOffline("turnRight", robot.turnRight(0.5), &failures);
  expectOffline("moveVelocity", robot.moveVelocity(0.05, 0.1), &failures);
  // stopMotion 在未连接时也返回 ok（本机清零不算失败）。
  const pa1664::Result stopResult = robot.stopMotion();
  std::printf("[%-8s] %-28s %s\n", stopResult.ok ? "OK" : "FAIL", "stopMotion",
              stopResult.message.c_str());
  if (!stopResult.ok) ++failures;
  expectOffline("startAutoCorrection", robot.startAutoCorrection("{}"), &failures);
  expectOffline("stopAutoCorrection", robot.stopAutoCorrection(), &failures);
  expectOffline("pauseAutoCorrection", robot.pauseAutoCorrection(true), &failures);
  expectOffline("requestAutoCorrectionStatus",
                robot.requestAutoCorrectionStatus(), &failures);
  expectOffline("clampSetSpeed", robot.clampSetSpeed(0, 0, 40), &failures);
  expectOffline("clampSetSpeed(bad)", robot.clampSetSpeed(9, 0, 40), &failures);
  expectOffline("clampStopMotor", robot.clampStopMotor(0, 0), &failures);
  expectOffline("clampStop", robot.clampStop(), &failures);
  expectOffline("clampMoveToPosition", robot.clampMoveToPosition(2, 0, 90.0),
                &failures);
  expectOffline("clampRequestPosition", robot.clampRequestPosition(2, 0),
                &failures);
  expectOffline("clampRequestPositions", robot.clampRequestPositions(), &failures);
  expectOffline("x1Motor", robot.x1Motor(40), &failures);
  expectOffline("x2Motor", robot.x2Motor(-40), &failures);
  expectOffline("z1Motor", robot.z1Motor(25), &failures);
  expectOffline("z2Motor", robot.z2Motor(-25), &failures);
  expectOffline("connectCamera", robot.connectCamera(0, 0, 15), &failures);
  expectOffline("connectNetworkCamera",
                robot.connectNetworkCamera(1, "rtsp://127.0.0.1/live"),
                &failures);
  expectOffline("disconnectCamera", robot.disconnectCamera(0), &failures);
  expectOffline("disconnectAllCameras", robot.disconnectAllCameras(), &failures);
  expectOffline("requestCameraStatus", robot.requestCameraStatus(), &failures);
  expectOffline("ultrasoundConnect", robot.ultrasoundConnect(), &failures);
  expectOffline("ultrasoundDisconnect", robot.ultrasoundDisconnect(), &failures);
  expectOffline("ultrasoundStartAcquisition",
                robot.ultrasoundStartAcquisition(), &failures);
  expectOffline("ultrasoundStopAcquisition",
                robot.ultrasoundStopAcquisition(), &failures);
  expectOffline("requestUltrasoundStatus", robot.requestUltrasoundStatus(),
                &failures);
  expectOffline("ultrasoundParametersSettled",
                robot.ultrasoundParametersSettled(true), &failures);
  expectOffline("ultrasoundParameterSnapshot",
                robot.ultrasoundParameterSnapshot(), &failures);
  expectOffline("ultrasoundGetParameter", robot.ultrasoundGetParameter("getGain"),
                &failures);
  expectOffline("ultrasoundSetParameter",
                robot.ultrasoundSetParameter("setGain", "[40,0]"), &failures);
  expectOffline("ultrasoundApplyParameters",
                robot.ultrasoundApplyParameters(
                    R"([{"method":"setGain","args":[40,0]}])"),
                &failures);
  pa1664::UltrasoundDecoderConfig decoder;
  decoder.pointCount = 1024;
  decoder.beamCount = 1;
  expectOffline("configureUltrasoundDecoder",
                robot.configureUltrasoundDecoder(decoder), &failures);
  if (robot.request("robot.snapshot") != 0) {
    std::printf("[FAIL    ] request 在未连接时返回了非 0\n");
    ++failures;
  } else {
    std::printf("[OK      ] %-28s 未连接时返回 0\n", "request");
  }

  std::printf("=== 自检结束：%d 项不符合预期 ===\n", failures);
  return failures == 0 ? 0 : 4;
}

}  // namespace

// 通讯验证：只建链、收遥测、发一次 robot.snapshot，并下发一帧零速
// MotionSetpoint 证明实时通道可用。全程不发任何非零速度，车不会动。
int runLinkTest(const std::string& host, bool connectChassis) {
  pa1664::Config config;
  config.host = host;
  pa1664::RobotApi robot(config);

  int telemetryCount = 0;
  bool everOnline = false;
  bool snapshotOk = false;
  bool sawState = false;

  robot.setLogHandler([](pa1664::LogLevel level, const std::string& message) {
    print(level == pa1664::LogLevel::Error ? "ERROR" : "LOG", message);
  });

  robot.setConnectionHandler([&](bool isOnline, const std::string& detail) {
    print(isOnline ? "ONLINE" : "OFFLINE", detail);
    if (isOnline) everOnline = true;
    if (!isOnline) return;
    // 快照：证明 TCP 请求/应答通路
    robot.requestSnapshot();
    // 零速包：证明 UDP 实时通道（不会让车动）
    robot.stopMotion();
    // 可选：连接底盘 + 使能，用来验证从会话到驱动层的整条链路。
    // 不发任何非零速度，使能后车轮保持零速。
    if (connectChassis) robot.takeControl();
  });

  robot.setResponseHandler([&](const std::string& operation, bool ok,
                               const std::string& errorCode,
                               const std::string& errorText,
                               const std::string& dataJson) {
    std::printf("[%-8s] %s ok=%d %s%s | %s\n", ok ? "RESP" : "REJECT",
                operation.c_str(), ok ? 1 : 0, errorCode.c_str(),
                errorText.empty() ? "" : (" " + errorText).c_str(),
                dataJson.substr(0, 160).c_str());
    if (operation == "robot.snapshot" && ok) snapshotOk = true;
  });

  robot.setStatusHandler([&](const pa1664::RobotStatus& status) {
    ++telemetryCount;
    if (!sawState || telemetryCount % 20 == 0) {
      sawState = true;
      std::printf("[TELEM   ] #%d state=%s applied=%.3f/%.3f fresh=%d/%d %s\n",
                  telemetryCount, status.stateText.c_str(),
                  status.appliedLinearMps, status.appliedAngularRadps,
                  status.commandFresh ? 1 : 0, status.feedbackFresh ? 1 : 0,
                  status.reason.c_str());
    }
  });

  robot.setEventHandler([](const std::string& name, const std::string& data) {
    std::printf("[EVENT   ] %s %s\n", name.c_str(), data.substr(0, 120).c_str());
  });

  robot.start();

  QCoreApplication* app = QCoreApplication::instance();
  QTimer::singleShot(10000, app, [app] { app->quit(); });

  app->exec();
  robot.stop();

  std::printf(
      "=== 通讯结果：online=%d snapshot=%d 遥测帧=%d ===\n", everOnline ? 1 : 0,
      snapshotOk ? 1 : 0, telemetryCount);
  return (everOnline && snapshotOk && telemetryCount > 0) ? 0 : 5;
}

void printDriveConfig(const pa1664::DriveConfig& drive, const char* tag) {
  if (!drive.loaded) {
    std::printf("[%-9s] 未加载：%s\n", tag, drive.error.c_str());
    return;
  }
  std::printf("[%-9s] path=%s schema=%d\n", tag, drive.path.c_str(),
              drive.schemaVersion);
  std::printf("[%-9s] 轮径=%.4f m 轮距=%.4f m 传动比=%.3f\n", tag,
              drive.wheelRadiusM, drive.trackWidthM,
              drive.motorOutputToWheelRatio);
  std::printf("[%-9s] 限幅 轮速=%.3f 线速度=%.3f 角速度=%.4f 线加速=%.3f "
              "角加速=%.3f\n",
              tag, drive.maximumWheelSpeedMps, drive.maximumLinearSpeedMps,
              drive.maximumAngularSpeedRadps,
              drive.maximumLinearAccelerationMps2,
              drive.maximumAngularAccelerationRadps2);
  std::printf("[%-9s] 超时 指令=%d ms 反馈=%d ms 使能=%d ms\n", tag,
              drive.commandTimeoutMs, drive.feedbackTimeoutMs,
              drive.armingTimeoutMs);
  std::printf("[%-9s] 轮总线 %s port=%s baud=%d bitrate=%d\n", tag,
              drive.wheelMode.c_str(), drive.wheelCanPort.c_str(),
              drive.wheelCanBaudRate, drive.wheelCanBitrate);
  std::printf("[%-9s] 左 id=%d sign=%d port=%s | 右 id=%d sign=%d port=%s\n",
              tag, drive.leftMotorId, drive.leftMotorSign,
              drive.leftPort.c_str(), drive.rightMotorId, drive.rightMotorSign,
              drive.rightPort.c_str());
  for (const auto& motor : drive.clampMotors) {
    std::printf("[%-9s] 夹爪 %s axis=%d index=%d mode=%s id=%d port=%s "
                "gear=%.1f limit=[%.2f,%.2f] on=%d/%d alias=%s\n",
                tag, motor.name.c_str(), motor.axis, motor.index,
                motor.mode.c_str(), motor.motorId, motor.port.c_str(),
                motor.gearRatio, motor.lowerLimit, motor.upperLimit,
                motor.lowerLimitEnabled ? 1 : 0,
                motor.upperLimitEnabled ? 1 : 0, motor.alias.c_str());
  }
  for (const auto& camera : drive.cameras) {
    std::printf("[%-9s] 摄像头%d source=%s index=%d fps=%d auto=%d flip=%d/%d "
                "url=%s\n",
                tag, camera.slot + 1, camera.source.c_str(),
                camera.deviceIndex, camera.fps, camera.autoConnect ? 1 : 0,
                camera.flipHorizontal ? 1 : 0, camera.flipVertical ? 1 : 0,
                camera.url.c_str());
  }
}

// 配置项验证：小车 drive_settings.ini（本地文件 + 经主机 robot.snapshot 回传）
// + 超声 status / 全量参数快照 / 单参数读 / 写入同值回读。
// 只读为主；唯一的写入是把 getGain 读到的值原样写回，不改变硬件状态。
int runConfigTest(const std::string& host) {
  QCoreApplication* app = QCoreApplication::instance();
  pa1664::Config config;
  config.host = host;
  pa1664::RobotApi robot(config);

  // 先测本地 ini：不依赖网络，验证键名解析与数组结构。
  printDriveConfig(pa1664::loadDriveConfig(), "LOCALCFG");

  bool everOnline = false;
  bool driveConfigOk = false;
  bool ultrasoundStatusOk = false;
  bool snapshotOk = false;
  bool getOk = false;
  bool setOk = false;
  bool gainWritten = false;
  double gainValue = 0.0;

  robot.setLogHandler([](pa1664::LogLevel level, const std::string& message) {
    print(level == pa1664::LogLevel::Error ? "ERROR" : "LOG", message);
  });

  robot.setConnectionHandler([&](bool online, const std::string& detail) {
    print(online ? "ONLINE" : "OFFLINE", detail);
    if (!online) return;
    everOnline = true;
    robot.requestDriveConfig();          // 小车配置（settings.iniBase64）
    robot.requestUltrasoundStatus();     // 超声设备状态
    robot.ultrasoundParameterSnapshot(); // 超声全量参数
    robot.ultrasoundGetParameter("getGain", "[0]");  // 单参数读
  });

  robot.setDriveConfigHandler([&](const pa1664::DriveConfig& drive) {
    driveConfigOk = drive.loaded;
    printDriveConfig(drive, "DRIVECFG");
  });

  robot.setUltrasoundStatusHandler([&](const pa1664::UltrasoundStatus& status) {
    ultrasoundStatusOk = true;
    std::printf("[USSTATUS] service=%d hardware=%d capturing=%d ready=%d "
                "point=%d beam=%d maxAmp=%d group=%d frames=%.0f\n",
                status.serviceConnected ? 1 : 0,
                status.hardwareOnline ? 1 : 0, status.capturing ? 1 : 0,
                status.captureReady ? 1 : 0, status.pointCount,
                status.beamCount, status.maxAmplitudeValue, status.currentGroup,
                status.totalFrames);
  });

  robot.setResponseHandler([&](const std::string& operation, bool ok,
                               const std::string& errorCode,
                               const std::string& errorText,
                               const std::string& dataJson) {
    if (!ok) {
      std::printf("[REJECT ] %s [%s] %s\n", operation.c_str(),
                  errorCode.c_str(), errorText.c_str());
      return;
    }
    const QJsonObject data =
        QJsonDocument::fromJson(QByteArray::fromStdString(dataJson)).object();

    if (operation == "ultrasound.parameters.snapshot") {
      snapshotOk = true;
      const QJsonObject values = data.value("values").toObject();
      const QJsonArray errors = data.value("errors").toArray();
      std::printf("[USSNAP  ] 参数=%d 项 失败=%d 项 complete=%d\n",
                  int(values.size()), int(errors.size()),
                  data.value("complete").toBool(false) ? 1 : 0);
      int shown = 0;
      for (auto it = values.begin(); it != values.end() && shown < 6;
           ++it, ++shown) {
        std::printf("[USSNAP  ]   %s = %s\n", it.key().toStdString().c_str(),
                    QJsonDocument(QJsonArray{it.value()})
                        .toJson(QJsonDocument::Compact)
                        .toStdString()
                        .c_str());
      }
      for (int i = 0; i < errors.size() && i < 3; ++i) {
        const QJsonObject item = errors.at(i).toObject();
        std::printf("[USSNAP  ]   failed %s : %s\n",
                    item.value("method").toString().toStdString().c_str(),
                    item.value("error").toString().toStdString().c_str());
      }
      return;
    }

    if (operation == "ultrasound.parameters.getMany") {
      getOk = true;
      const QJsonObject values = data.value("values").toObject();
      std::printf("[USGET   ] 返回 %d 项\n", int(values.size()));
      for (auto it = values.begin(); it != values.end(); ++it) {
        gainValue = it.value().toDouble(0.0);
        std::printf("[USGET   ]   %s = %s\n", it.key().toStdString().c_str(),
                    QJsonDocument(QJsonArray{it.value()})
                        .toJson(QJsonDocument::Compact)
                        .toStdString()
                        .c_str());
      }
      // 把读到的增益原样写回，验证写入 + 回读链路（不改变硬件状态）
      if (!gainWritten) {
        gainWritten = true;
        char args[64];
        std::snprintf(args, sizeof(args), "[%.6f,0]", gainValue);
        robot.ultrasoundSetParameter("setGain", args);
      }
      return;
    }

    if (operation == "ultrasound.parameter.set") {
      setOk = true;
      const QJsonValue readback = data.value("readbackValue");
      std::printf("[USSET   ] 回读 %s = %s\n",
                  data.value("readbackKey").toString().toStdString().c_str(),
                  QJsonDocument(QJsonArray{readback})
                      .toJson(QJsonDocument::Compact)
                      .toStdString()
                      .c_str());
      return;
    }
  });

  robot.start();
  QTimer::singleShot(15000, app, [app] { app->quit(); });
  app->exec();
  robot.stop();

  std::printf("=== 配置测试结果 online=%d 小车配置=%d 超声状态=%d "
              "参数快照=%d 单参数读=%d 参数写回=%d ===\n",
              everOnline ? 1 : 0, driveConfigOk ? 1 : 0, ultrasoundStatusOk ? 1 : 0,
              snapshotOk ? 1 : 0, getOk ? 1 : 0, setOk ? 1 : 0);
  return (everOnline && driveConfigOk && ultrasoundStatusOk) ? 0 : 6;
}

// 全项联调：按时间线把每类接口都打一遍，逐项打印结果。
// 不产生机械动作：夹爪只 connect/status/positions/stop；只有在底盘确实
// Enabled 时才发一次 0.15 m/s 前进并在 0.7 s 后立即停车；纠偏只查询与
// 尝试启动（前置不满足时记录主机返回的错误码，不重试）。
int runFullTest(const std::string& host, const std::string& iniPath) {
  QCoreApplication* app = QCoreApplication::instance();
  pa1664::Config config;
  config.host = host;
  pa1664::RobotApi robot(config);

  const pa1664::DriveConfig drive = pa1664::loadDriveConfig(iniPath);
  printDriveConfig(drive, "LOCALCFG");

  bool everOnline = false;
  int telemetryFrames = 0;
  int cameraFrames[2] = {0, 0};
  int ultrasoundFrames = 0;
  pa1664::DriveState state = pa1664::DriveState::Unknown;
  pa1664::UltrasoundStatus ultrasoundStatus;
  bool driveConfigOk = false;
  bool snapshotOk = false;
  bool ultrasoundStatusOk = false;
  bool ultrasoundSnapshotOk = false;
  bool ultrasoundGetOk = false;
  bool ultrasoundSetOk = false;
  bool clampStatusOk = false;
  bool clampPositionsOk = false;
  bool cameraStatusOk = false;
  bool correctionStatusOk = false;
  bool forwardIssued = false;
  std::string correctionStartError = "(未请求)";

  robot.setLogHandler([](pa1664::LogLevel level, const std::string& message) {
    if (level == pa1664::LogLevel::Error) print("ERROR", message);
  });

  robot.setConnectionHandler([&](bool online, const std::string& detail) {
    print(online ? "ONLINE" : "OFFLINE", detail);
    if (online) everOnline = true;
  });

  robot.setStatusHandler([&](const pa1664::RobotStatus& status) {
    ++telemetryFrames;
    state = status.state;
  });

  robot.setDriveConfigHandler([&](const pa1664::DriveConfig& cfg) {
    driveConfigOk = cfg.loaded;
  });

  robot.setUltrasoundStatusHandler(
      [&](const pa1664::UltrasoundStatus& status) {
        ultrasoundStatus = status;
        ultrasoundStatusOk = true;
      });

  robot.setUltrasoundFrameHandler(
      [&](const pa1664::UltrasoundAScanFrame& frame) {
        ++ultrasoundFrames;
        if (ultrasoundFrames == 1) {
          std::printf("[USFRAME ] group=%d beams=%d points=%d range=%.1f..%.1f "
                      "frame=%u\n",
                      frame.groupId, frame.beamCount, frame.pointCount,
                      frame.rangeStartMm, frame.rangeEndMm, frame.frameNumber);
        }
      });

  robot.setCameraFrameHandler([&](const pa1664::CameraFrame& frame) {
    if (frame.slot < 0 || frame.slot > 1) return;
    ++cameraFrames[frame.slot];
    if (cameraFrames[frame.slot] == 1) {
      std::printf("[CAMFRAME] 摄像头%d %dx%d stride=%d fmt=%d\n",
                  frame.slot + 1, frame.width, frame.height, frame.stride,
                  int(frame.format));
    }
  });

  robot.setCorrectionStatusHandler(
      [&](const pa1664::CorrectionStatus& status) {
        correctionStatusOk = true;
        std::printf("[CORRECT ] active=%d paused=%d phase=%s reason=%s\n",
                    status.active ? 1 : 0, status.paused ? 1 : 0,
                    status.phase.c_str(), status.reason.c_str());
      });

  robot.setResponseHandler([&](const std::string& operation, bool ok,
                               const std::string& errorCode,
                               const std::string& errorText,
                               const std::string& dataJson) {
    if (!ok) {
      std::printf("[REJECT  ] %s [%s] %s\n", operation.c_str(),
                  errorCode.c_str(), errorText.c_str());
      if (operation == "robot.autoCorrection.start")
        correctionStartError = errorCode + " " + errorText;
      return;
    }
    if (operation == "robot.snapshot") {
      snapshotOk = true;
      std::printf("[ACK     ] robot.snapshot ok, data=%d 字节\n",
                  int(dataJson.size()));
    } else if (operation == "ultrasound.parameters.snapshot") {
      ultrasoundSnapshotOk = true;
      const QJsonObject data =
          QJsonDocument::fromJson(QByteArray::fromStdString(dataJson)).object();
      std::printf("[ACK     ] 超声参数快照 %d 项 / 失败 %d 项\n",
                  int(data.value("values").toObject().size()),
                  int(data.value("errors").toArray().size()));
    } else if (operation == "ultrasound.parameters.getMany") {
      ultrasoundGetOk = true;
    } else if (operation == "ultrasound.parameter.set") {
      ultrasoundSetOk = true;
    } else if (operation == "robot.clamp.status") {
      clampStatusOk = true;
    } else if (operation == "robot.clamp.positions") {
      clampPositionsOk = true;
    } else if (operation == "robot.camera.status") {
      cameraStatusOk = true;
    } else if (operation == "robot.autoCorrection.status") {
      correctionStatusOk = true;
    } else if (operation != "robot.clamp.connect" &&
               operation != "robot.camera.connect" &&
               operation != "ultrasound.decoder.settings" &&
               operation != "ultrasound.acquisition.start" &&
               operation != "ultrasound.acquisition.stop" &&
               operation != "robot.clamp.stop" &&
               operation != "robot.clamp.disconnect" &&
               operation != "robot.camera.disconnect") {
      std::printf("[ACK     ] %s\n", operation.c_str());
    }
  });

  robot.setEventHandler([](const std::string& name, const std::string& data) {
    if (name == "robot.clamp.connection" || name == "robot.camera.connection" ||
        name == "robot.stateChanged" ||
        name == "robot.autoCorrection.started" ||
        name == "host.busy") {
      std::printf("[EVENT   ] %s %s\n", name.c_str(), data.substr(0, 120).c_str());
    }
  });

  std::printf("[PROFILE ] path=%s driveCache=%d usCache=%d\n",
              robot.profilePath().c_str(), robot.hasDriveProfile() ? 1 : 0,
              robot.hasUltrasoundProfile() ? 1 : 0);

  robot.start();

  QTimer* timer = new QTimer(app);
  timer->setInterval(700);
  int step = 0;
  QObject::connect(timer, &QTimer::timeout, app, [&, timer] {
    std::printf("[STEP %d]\n", step);
    switch (step) {
      case 0:
        robot.requestDriveConfig();
        robot.requestUltrasoundStatus();
        robot.requestSnapshot();
        break;
      case 1:
        robot.request("robot.clamp.connect");
        robot.request("robot.clamp.status");
        robot.request("robot.clamp.positions");
        break;
      case 2:
        if (drive.loaded && !drive.cameras.empty()) {
          for (const auto& camera : drive.cameras) {
            if (camera.source == "network" && !camera.url.empty()) {
              robot.connectNetworkCamera(camera.slot, camera.url,
                                         camera.flipHorizontal,
                                         camera.flipVertical);
            } else if (camera.deviceIndex >= 0) {
              robot.connectCamera(camera.slot, camera.deviceIndex, camera.fps,
                                  camera.flipHorizontal, camera.flipVertical);
            }
          }
        } else {
          robot.request("robot.camera.scan");
        }
        robot.request("robot.camera.status");
        break;
      case 3:
        robot.request("robot.autoCorrection.status");
        robot.startAutoCorrection("{}");  // 前置不满足时记录错误码
        break;
      case 4:
        // 解码器配置由库在收到 ultrasound.status 后自动下发（几何取远端、
        // 扫描模式取本地），这里不再手动配置，用来验证自动流程。
        std::printf("[DECODER ] 期望库已按 status 自动下发（group=%d point=%d "
                    "beam=%d）\n",
                    ultrasoundStatus.currentGroup, ultrasoundStatus.pointCount,
                    ultrasoundStatus.beamCount);
        robot.ultrasoundStartAcquisition();
        break;
      case 5:
        robot.ultrasoundParameterSnapshot();
        robot.ultrasoundGetParameter("getGain", "[0]");
        robot.ultrasoundSetParameter("setGain", "[13,0]");  // 与实测值相同
        robot.request("robot.clamp.stop");
        break;
      case 6:
        std::printf("[COUNT   ] 遥测=%d 超声A扫=%d 摄像头1=%d 摄像头2=%d\n",
                    telemetryFrames, ultrasoundFrames, cameraFrames[0],
                    cameraFrames[1]);
        robot.ultrasoundStopAcquisition();
        robot.disconnectAllCameras();
        robot.request("robot.clamp.disconnect");
        break;
      case 7:
        robot.enable(true);
        break;
      case 8:
        if (state == pa1664::DriveState::Enabled) {
          forwardIssued = robot.forward(0.15).ok;
          std::printf("[MOTION  ] state=Enabled，已发 0.15 m/s 前进 0.7 s\n");
        } else {
          std::printf("[MOTION  ] state=%d（非 Enabled），跳过运动测试\n",
                      int(state));
        }
        break;
      case 9:
        robot.stopMotion();
        robot.stopAutoCorrection();
        timer->stop();
        app->quit();
        break;
      default:
        break;
    }
    ++step;
  });
  timer->start();
  QTimer::singleShot(15000, app, [app] { app->quit(); });

  app->exec();
  robot.stop();

  std::printf(
      "=== 全项测试汇总 ===\n"
      "  链路 online=%d 遥测=%d\n"
      "  小车配置=%d 快照=%d\n"
      "  超声 状态=%d 参数快照=%d 单读=%d 写回=%d A扫帧=%d\n"
      "  夹爪 状态=%d 位置=%d\n"
      "  摄像头 状态=%d 帧=%d/%d\n"
      "  纠偏 状态=%d 启动错误=%s\n"
      "  运动 是否下发=%d\n"
      "  本地镜像 %s drive=%d us=%d\n",
      everOnline ? 1 : 0, telemetryFrames, driveConfigOk ? 1 : 0,
      snapshotOk ? 1 : 0, ultrasoundStatusOk ? 1 : 0,
      ultrasoundSnapshotOk ? 1 : 0, ultrasoundGetOk ? 1 : 0,
      ultrasoundSetOk ? 1 : 0, ultrasoundFrames, clampStatusOk ? 1 : 0,
      clampPositionsOk ? 1 : 0, cameraStatusOk ? 1 : 0, cameraFrames[0],
      cameraFrames[1], correctionStatusOk ? 1 : 0,
      correctionStartError.c_str(), forwardIssued ? 1 : 0,
      robot.profilePath().c_str(), robot.hasDriveProfile() ? 1 : 0,
      robot.hasUltrasoundProfile() ? 1 : 0);

  const bool coreOk = everOnline && driveConfigOk && snapshotOk &&
                      ultrasoundStatusOk && clampStatusOk && cameraStatusOk;
  return coreOk ? 0 : 7;
}

// 超声单参数读写：--usparam <host> <method> <argsJson>
// 例：--usparam 192.168.66.10 setGain "[20,0]"
//     --usparam 192.168.66.10 getGain "[0]"
int runUltrasoundParam(const std::string& host, const std::string& method,
                       const std::string& rawArgs) {
  QCoreApplication* app = QCoreApplication::instance();
  pa1664::Config config;
  config.host = host;
  pa1664::RobotApi robot(config);

  // 允许命令行直接写 "20,0"（省去 Windows 引号转义），内部补成 [20,0]。
  std::string argsJson = rawArgs;
  if (argsJson.empty() || argsJson.front() != '[')
    argsJson = "[" + argsJson + "]";

  bool online = false;
  bool done = false;

  robot.setConnectionHandler([&](bool isOnline, const std::string& detail) {
    print(isOnline ? "ONLINE" : "OFFLINE", detail);
    if (!isOnline) return;
    online = true;
    if (method.rfind("ultrasound.", 0) == 0)
      robot.request(method);  // 直接发主机 operation，便于设备重连
    else if (method.rfind("set", 0) == 0)
      robot.ultrasoundSetParameter(method, argsJson);
    else
      robot.ultrasoundGetParameter(method, argsJson);
  });

  robot.setResponseHandler([&](const std::string& operation, bool ok,
                               const std::string& errorCode,
                               const std::string& errorText,
                               const std::string& dataJson) {
    std::printf("[RESP] %s ok=%d %s %s\n%s\n", operation.c_str(), ok ? 1 : 0,
                errorCode.c_str(), errorText.c_str(), dataJson.c_str());
    if (!ok) return;
    done = true;
    QCoreApplication::instance()->quit();
  });

  robot.start();
  QTimer::singleShot(15000, app, [app] { app->quit(); });
  app->exec();
  robot.stop();
  return (online && done) ? 0 : 8;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  if (argc > 1 && std::string(argv[1]) == "--selftest")
    return runSelfTest(argc > 2 ? std::string(argv[2]) : std::string());
  if (argc > 1 && std::string(argv[1]) == "--usparam")
    return runUltrasoundParam(argc > 2 ? argv[2] : "192.168.66.10",
                              argc > 3 ? argv[3] : "getGain",
                              argc > 4 ? argv[4] : "[0]");
  if (argc > 1 && std::string(argv[1]) == "--full")
    return runFullTest(argc > 2 ? std::string(argv[2]) : "192.168.66.10",
                       argc > 3 ? std::string(argv[3]) : std::string());
  if (argc > 1 && std::string(argv[1]) == "--config")
    return runConfigTest(argc > 2 ? std::string(argv[2]) : "192.168.66.10");
  if (argc > 1 && std::string(argv[1]) == "--link")
    return runLinkTest(argc > 2 ? std::string(argv[2]) : "192.168.66.10",
                       argc > 3 && std::string(argv[3]) == "connect");

  pa1664::Config config;
  if (argc > 1) config.host = argv[1];
  print("CONFIG", "host=" + config.host + " port=" +
                     std::to_string(config.controlPort));

  int exitCode = 0;
  bool sequenceStarted = false;
  bool finished = false;
  pa1664::RobotApi robot(config);

  robot.setLogHandler([](pa1664::LogLevel level, const std::string& message) {
    print(level == pa1664::LogLevel::Error ? "ERROR" : "LOG", message);
  });

  // 遥测约 20~50 Hz，示例里限制到 2 Hz 打印，避免刷屏。
  robot.setStatusHandler([](const pa1664::RobotStatus& status) {
    static qint64 lastPrintMs = 0;
    static pa1664::DriveState lastState = pa1664::DriveState::Unknown;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (status.state == lastState && now - lastPrintMs < 500) return;
    lastPrintMs = now;
    lastState = status.state;
    std::printf("[STATUS  ] state=%s target=%.3f/%.3f applied=%.3f/%.3f "
                "left=%.3f right=%.3f fresh=%d/%d %s\n",
                status.stateText.c_str(), status.targetLinearMps,
                status.targetAngularRadps, status.appliedLinearMps,
                status.appliedAngularRadps, status.leftWheelMps,
                status.rightWheelMps, status.commandFresh ? 1 : 0,
                status.feedbackFresh ? 1 : 0, status.reason.c_str());
  });

  robot.setResponseHandler([](const std::string& operation, bool ok,
                              const std::string& errorCode,
                              const std::string& errorText,
                              const std::string& dataJson) {
    if (!ok) {
      print("REJECT", operation + " [" + errorCode + "] " + errorText);
      return;
    }
    print("ACK", operation + " data=" + dataJson);
  });

  robot.setConnectionHandler([&](bool online, const std::string& detail) {
    print(online ? "ONLINE" : "OFFLINE", detail);
    if (!online) {
      if (sequenceStarted && !finished) {
        print("ABORT", "演示过程中链路断开，退出");
        exitCode = 3;
        app.quit();
      }
      return;
    }
    if (sequenceStarted) return;  // 自动重连不重放演示动作
    sequenceStarted = true;

    QTimer::singleShot(300, &app, [&] {
      // 一条命令完成 connect + enable（内部等主机应答后延时使能）。
      const pa1664::Result result = robot.takeControl();
      print(result.ok ? "CONTROL" : "CONTROL!", result.message);
    });
    QTimer::singleShot(3200, &app, [&] {
      const pa1664::Result result = robot.forward(0.5);  // 半速前进
      print(result.ok ? "FORWARD" : "FORWARD!", result.message);
      if (!result.ok) {
        finished = true;
        exitCode = 3;
        app.quit();
      }
    });
    QTimer::singleShot(5200, &app, [&] {
      robot.stopMotion();
      print("STOP", "前进结束，已停车");
      finished = true;
      app.quit();
      robot.stop();  // 退出前主动断链
    });
  });

  robot.start();

  // 全局兜底：15 秒内没有走完流程就急停并退出。
  QTimer::singleShot(15000, &app, [&] {
    print("TIMEOUT", "演示流程超时，执行急停");
    robot.emergencyStop();
    robot.stop();
    exitCode = 2;
    app.quit();
  });

  app.exec();
  return exitCode;
}
