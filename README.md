# Crawling Robot Desktop

这是一个原生 Qt Widgets 桌面程序，用于替代 ROS 2 底盘控制链路；不依赖 ROS 2 或 DDS。主界面的“IMU / 线激光”按钮可直接连接 RIM302 串口与 MV3DLP 厂商 SDK 设备。

## 当前交付内容（中文界面）

- 双轮以 50 Hz 成对发送速度帧，结合反馈比例/积分同步、统一限速、命令/反馈看门狗和急停。
- RIM302 实时显示姿态、三轴角速度、三轴加速度。
- MV3DLP 实时采集、深度转点云并显示限量点云预览；界面可选择 X-Y、X-Z 或 Y-Z 投影并记忆上次选择；全量点云不写磁盘，避免阻塞控制程序。
- 主界面“车体状态”集中显示车体命令、双驱动轮反馈、轮端编码器、电机转速/电流、IMU 和辅助设备状态；检测到夹子 CANopen 节点时会显示节点号。
- 外部 Modbus 编码器和夹子轴实时反馈尚未迁移，界面会明确显示未接入，不会向未验证的夹子轴发送控制帧。
- 单一滚动日志：`build/release/logs/robot_console.log`，最大 2 MiB，自动保留近期日志。
- `build_release.ps1` 会部署 Qt、MV3DLP SDK 和 MSVC x64 运行库；复制整个 `build/release` 文件夹到另一台 Windows x64 电脑即可运行。

The implementation was derived from `D:\dev\CrawlingRobot` with these boundaries:

- Included: V3.8 servo-CAN command encoding, SLCAN serial transport, differential drive kinematics, motor mapping, ramp limits, command watchdogs, feedback watchdogs, and the paired-wheel feedback path.
- Included: RIM302 live-frame parsing over its own serial port and MV3DLP camera discovery, serial-number connection, range-image acquisition, and point-cloud conversion through the supplied Windows x64 SDK.
- Not included: the remote `robot_control_suite` and `robot_control_suite_portable` programs, odometry, and laser path following. The new camera connection is deliberately diagnostic-only; it does not autonomously steer the robot.
- Separate legacy logic: the source-root `Locke` Qt program controls scanning axes over TCP. It is not the ROS chassis drive and was not merged into this safety-critical drive console.

## Why the architecture changes

The original ROS `base_drive_node` correctly calculated left/right wheel targets, but it sent two independent motor frames. The CAN bridge then managed each frame independently, while motor feedback was published only for monitoring. This leaves three practical failure modes:

1. Each motor accepts a speed loop independently, so manufacturing/load differences create a straight-line drift.
2. A delayed or overwritten frame can leave the two motors acting on different command generations.
3. A failed feedback path cannot stop the robot because feedback does not gate command output.

This program treats both wheels as one drive pair on a dedicated 50 Hz controller thread:

```
manual command -> vector acceleration ramp -> differential mix -> uniform speed cap
                                                        |                 |
                                                  paired PI sync <- dual fresh feedback
                                                        |
                                              one serial write: left frame + right frame
```

- The controller writes both wheel frames together in one SLCAN serial write; when output is congested it drops stale transmit text before the newest pair.
- A translating turn preserves a configurable inner/outer ratio so the inner physical wheel cannot unexpectedly reverse. Pivot turns remain available at zero linear speed.
- The pair synchronizer compares `actual wheel speed / requested wheel speed`, which means it corrects progress mismatch while retaining the intended speed difference of a turn. It is disabled for pivots and very low speed, where division by small values is unsafe.
- Enabling waits for fresh feedback from **both** motor IDs. During motion, either stale command input or stale feedback immediately enters a fault state and repeatedly sends paired stop frames.
- The UI process never writes motor frames itself. The controller, timers, and serial port run in a dedicated Qt thread.

## Hardware assumptions

The program implements the same interface used by the ROS driver:

- SLCAN USB-CAN adapter over a Windows serial port, serial baud normally `115200`, CAN normally `1000000`.
- Standard 11-bit frames, V3.8 servo command ID `0x140 + node_id`, feedback ID `0x240 + node_id`.
- Closed-loop speed command `0xA2`; speed payload is little-endian degrees/s times 100; stop is `0x81`.
- Motor feedback arrives as an `0xA2` response containing speed at bytes 4-5 and uses the same two motor IDs configured in the UI.

Do not use the console on a moving vehicle until this has been validated with the wheels suspended. If the adapter or servo firmware differs from these assumptions, update `src/servo_protocol.cpp` and the test before connecting it to the chassis.

## First commissioning

1. Lift both drive wheels clear of the surface and connect the SLCAN adapter.
2. Configure the serial port, CAN bitrate, node IDs, motor signs, wheel radius, track width, and reduction ratio. The starting values match the ROS `robot.yaml`: IDs `1/2`, signs `+1/-1`, radius `0.040 m`, track `0.300 m`, ratio `1.0`.
3. Connect the adapter, then enable the drive. It must show both feedback streams as `live`; otherwise it will refuse to arm.
4. At 5-10% manual output, confirm that Forward makes both physical wheels move forward. Correct the motor signs if not. Then confirm Left and Right make the correct outer wheel faster.
5. Start with synchronization P=`0.30`, I=`0.08`, correction limit=`0.030 m/s`. If the chassis alternates side-to-side, lower P first; increase I only after the proportional response is stable.
6. Test loss of CAN feedback and command input while suspended. The state must become `Safety fault` and both motors must stop.

The program intentionally requires a deliberate disable/re-enable after an emergency stop or watchdog fault.

## IMU and line laser

Click `IMU / LASER` in the main window:

- RIM302 uses an independent RS485/RS422 serial adapter. Select its COM port and divider. The program opens it at the manual default of 115200 bps, configures continuous output, and displays roll/pitch/yaw, gyro Z, and acceleration Z from valid CRC-checked frames.
- The MV3DLP camera connection runs in a separate worker thread. Click `Scan cameras`, select the returned serial number, and click `Connect camera`. The program uses the included SDK to acquire range images and convert depth frames to a point cloud. Its DLL tree is deployed to `mv3dlp_sdk` beside the executable.
- Neither device is used to command wheel motion yet. This preserves the requested focus on solving paired wheel control before an IMU/vision feedback law is commissioned.

## Build and run

The configured project uses the installed Qt 5.12.4/MSVC2017 x64 toolchain.

```powershell
cd D:\dev\deskCrawlingRobot\crawling_robot_desktop\build
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat" && "D:\qt\5.12.4\msvc2017_64\bin\qmake.exe" ..\CrawlingRobotDesktop.pro -spec win32-msvc && nmake'
```

The deployable executable is at `build\release\CrawlingRobotDesktop.exe`. `windeployqt` has already been run against this local release directory, so it contains the Qt runtime DLLs and platform plug-in needed to start on a compatible Windows x64 machine.

## Tests

`tests\drive_core_tests.pro` covers the properties that directly protect drive behavior:

- translating turn wheels stay in the same physical direction;
- a wheel cap preserves the turn ratio;
- feedback correction works in forward and reverse without flipping a wheel; and
- V3.8 speed frame encoding/feedback parsing matches the inherited protocol.

Build and run them with the same toolchain from `tests\build`.
