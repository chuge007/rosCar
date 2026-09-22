# Crawling Robot Desktop

这是一个原生 Qt Widgets 桌面程序，用于替代 ROS 2 底盘控制链路；不依赖 ROS 2 或 DDS。主界面的“IMU / 线激光”按钮可直接连接 RIM302 串口与 MV3DLP 厂商 SDK 设备。

## 当前交付内容（中文界面）

- 控制计算保持 50 Hz，双轮 RS485 正常通信限制为约 10 Hz；行驶时复用 `0xA2` 回复，静止时才用 `0x9C` 保活，结合双轮响应自适应同步、统一限速、命令/反馈看门狗和急停。
- RIM302 实时显示姿态、三轴角速度、三轴加速度。
- MV3DLP 实时采集并显示原始图像；点击一次“启动自动纠偏”后，程序按新鲜轮速反馈积分路程，完成首段前进采集、停稳、等路程原路返回及双边缘拟合。初始采集与后续更新的默认分段距离均为 100 mm。后续跟踪以实时原图中的焊道中心偏差为主环，以滚动双边缘点云拟合方向为慢速辅助，持续前进并平滑调整双轮差速；段末拟合更新不要求停车。点云按行程采样，分别保留左右边缘旋转后的纵坐标，再转到当前车体系拟合，避免转弯时按固定世界 X 筛点造成更新停滞。角速度随线速度限制，默认内轮/外轮速度比不低于 45%。每段连续激光线、缺口边缘、左右拟合线和道路中心线保存为 `laser_trajectory_maps/<会话时间>/segment_XXXX.png`，只保留最新 3 次自动纠偏任务。点云模式仍支持限量预览。
- 焊道定位恢复为原先的主激光基线缺口及双边中心：在相机 SDK 原尺寸图像中提取主基线，沿基线窄带检测断口、关联前一帧位置和宽度，连续 3 帧确认；新增凸起轮廓接管默认关闭。检测与桌面缩放预览分离。相机 `Gain`/`ExposureTime` 保留设备当前值。横向全幅 200 mm、激光前视距离 250 mm 仍是内部标称值，毫米偏差和拟合角不能视为已标定实测。
- 自动纠偏巡航采用配置的车体中心目标速度。边界距离、观测年龄、回中响应不再按比例降速；底盘双轮同步后恢复左右轮命令均值，轮速上限只限制可用差速。配置速度超过底盘可用上限时拒绝启动，运行中发现不可实现命令则停止。启动、停车及返程换向仍保留必要过渡；实际速度受电机响应影响。日志 `constant_speed_correction`、`output_mean_mps` 和 `correction_steering_limited` 可核对最终输出。
- 扫描范围回中优先于柔性纠偏：按实际激光端点和完整焊道包络计算余量，预计余量不超过 25% 时提前向内转向，恢复到 29% 后退出边界优先；差速随余量连续变化，巡航速度不变。有可靠焊道时不会因临边、旧转向或单帧漏检立即锁停。临边首段采集提前进入实时跟踪，返程临边则结束返程、停稳后向前闭环回中。临边实测缺失超过 500 ms、全局实测缺失超过 1000 ms、焊道包络已出界，或余量耗尽且回中响应失败时停止。诊断区分 `scan_boundary_recenter` 和 `scan_boundary_stop`。详见 [本次原始数据复查](docs/laser_recenter_2026-09-20.md)。
- 主界面“车体状态”集中显示车体命令、双驱动轮反馈、轮端编码器、电机转速/控制量、IMU 和辅助设备状态；检测到夹子 CANopen 节点时会显示节点号。
- 外部 Modbus 编码器和夹子轴实时反馈尚未迁移，界面会明确显示未接入，不会向未验证的夹子轴发送控制帧。
- 单一滚动日志：`build/release/logs/robot_console.log`，最大 16 MiB，自动保留近期日志。自动纠偏期间 `CORRECTION.RAW_IMAGE` 约每 100 ms 记录基线位置、基线带/全图投影摘要、预处理亮段、检测结果和源帧身份；这些摘要用于解释检测，不能还原完整二维图像。完整图像以最高 5 Hz 异步保存到每任务目录下的 `raw_frames/`，由 `frames.jsonl` 关联 SDK 帧号、主机收帧时间、检测及控制状态；最多一张图像等待写入，每任务原图归档最多 2000 帧或 512 MiB，达到任一上限停止本任务原图归档，纠偏继续。PNG 无损保存的是 JPEG 解码后的 `QImage`，不等于传感器未经压缩的原始数据。复盘时需同时保留日志、PNG 和 JSONL；删除旧任务时对应原图归档一并清理。
- `build_release.ps1` 会部署 Qt、MV3DLP SDK 和 MSVC x64 运行库；复制整个 `build/release` 文件夹到另一台 Windows x64 电脑即可运行。

The implementation was derived from `D:\dev\CrawlingRobot` with these boundaries:

- Included: MWD RS485 command/feedback framing, differential drive kinematics, motor mapping, ramp limits, command watchdogs, feedback watchdogs, and the paired-wheel feedback path.
- Included: RIM302 live-frame parsing over its own serial port and MV3DLP camera discovery, serial-number connection, range-image acquisition, and point-cloud conversion through the supplied Windows x64 SDK.
- Not included: the remote `robot_control_suite` and `robot_control_suite_portable` programs and odometry. Laser correction uses the raw-image gap position and does not depend on ROS.
- Separate legacy logic: the source-root `Locke` Qt program controls scanning axes over TCP. It is not the ROS chassis drive and was not merged into this safety-critical drive console.

## Why the architecture changes

The original ROS `base_drive_node` correctly calculated left/right wheel targets, but it sent two independent motor frames. The CAN bridge then managed each frame independently, while motor feedback was published only for monitoring. This leaves three practical failure modes:

1. Each motor accepts a speed loop independently, so manufacturing/load differences create a straight-line drift.
2. A delayed or overwritten frame can leave the two motors acting on different command generations.
3. A failed feedback path cannot stop the robot because feedback does not gate command output.

This program treats both wheels as one drive pair on a dedicated 50 Hz controller thread, while rate-limiting normal motor exchanges to approximately 10 Hz:

```
manual command -> vector acceleration ramp -> differential mix -> effective motor speed cap
                                                        |                         |
                                           adaptive response match <- dual fresh feedback
                                                        |
                                            paired left/right RS485 commands
```

- The controller computes at each control tick but sends the latest paired wheel target once per 100 ms. Moving feedback comes from the `0xA2` reply, so no redundant `0x9C` query is sent; stopped motors use a rate-limited `0x9C` keepalive.
- A translating turn preserves a configurable inner/outer ratio so the inner physical wheel cannot unexpectedly reverse. Pivot turns remain available at zero linear speed.
- The pair synchronizer compares each actual wheel speed with the last command that was really sent. It keeps the less responsive side unchanged and throttles only the faster side, retaining the intended physical speed ratio of a translating turn. It is disabled for pivots and very low speed, where division by small values is unsafe.
- The configured wheel-speed limit is combined with a `4300 dps` paired-motion ceiling (about `0.084 m/s` with the installed 36:1 drive) before synchronization. This is below the slower drive's measured limit, so a short manual jog starts with equal attainable commands instead of waiting for feedback adaptation.
- Nonzero motion output requires fresh feedback from **both** motor IDs. Each side has its own freshness timestamp, and brief missed 10 Hz replies are tolerated within the 800 ms watchdog window; continued command or feedback loss enters a fault state and repeatedly sends paired stop frames at the same limited rate. Zero speed, disable, emergency stop, and the first fault stop bypass normal rate limiting.
- The UI process never writes motor frames itself. The controller, timers, and serial port run in a dedicated Qt thread.

## Hardware assumptions

The program implements the same interface used by the ROS driver:

- MWD RS485 adapter over one Windows serial port, `8-N-1`; V3.8 supports `115200`, `500000`, `1000000`, `1500000`, and `2500000` bps.
- Two motors share the bus and use IDs `1..32` (the UI defaults to left `1`, right `2`).
- V3.8 RS485 frames are fixed 13-byte packets: `0x3E | ID | 0x08 | D0..D7 | CRC16_L | CRC16_H`.
  The supplied V3.8 document specifies the CRC field and byte order but not the polynomial or initial value; the implementation currently uses Modbus-compatible CRC16 parameters.
- Closed-loop speed command `0xA2` carries signed little-endian `int32` speed in `0.01 dps/LSB`; `0x81` stops rotation, `0x92` reads the current multi-turn angle, `0xA4` holds that angle with a speed-limited position loop, and `0x76` resets the motor system.
- Status 2 query `0x9C` and speed/position replies return temperature, motor current/control value, output speed in `1 dps/LSB`, and output angle in `1 degree/LSB`. The separate `0x60` query supplies the signed multi-turn encoder position.
- The installed wheel drives default to a `36:1` motor-to-wheel reduction ratio. Motor commands are multiplied by this ratio, while speed feedback and encoder distance are divided by it. Enter the gearbox nameplate ratio if the installed hardware differs.
- Every stationary transition first uses `0x81`, then reads each motor's current multi-turn position with `0x92` and sends the same position through speed-limited `0xA4`. The application intentionally does not send `0x8C`, because no mechanical brake has been verified on the installed wheel motors. A nonzero `0xA2` command switches directly back to speed control without a separate release step.

The `0xA4` position loop provides powered stationary holding only. It releases when motor power is removed and must not be treated as a safety brake. A requirement to hold against power loss still needs verified mechanical brake hardware.

Do not use the console on a moving vehicle until this has been validated with the wheels suspended. If the adapter or MWD firmware differs from these assumptions, update `src/mwd_rs485_protocol.cpp` and the tests before connecting it to the chassis.

## First commissioning

1. Lift both drive wheels clear of the surface and connect the MWD RS485 adapter to the shared motor bus.
2. Configure the RS485 port/baud, motor IDs, motor signs, wheel radius, track width, and reduction ratio. The starting values are IDs `1/2`, signs `+1/-1`, radius `0.040 m`, track `0.300 m`, ratio `36.0`.
3. Connect the adapter, then enable the drive. It must show both feedback streams as `live`; otherwise it will refuse to arm.
4. At 5-10% manual output, confirm that Forward makes both physical wheels move forward. Correct the motor signs if not. Then confirm Left and Right make the correct outer wheel faster.
5. Start with synchronization P=`0.30`, I=`0.08`, correction limit=`0.300 m/s`. P controls how strongly a new response sample changes the learned factor, while I adds a small feedback-interval contribution. If the chassis alternates side-to-side, lower P first.
6. Test loss of RS485 feedback and command input while suspended. The state must become `Safety fault` and both motors must stop.

The program intentionally requires a deliberate disable/re-enable after an emergency stop or watchdog fault.

## IMU and line laser

Click `IMU / LASER` in the main window:

- RIM302 uses an independent RS485/RS422 serial adapter. Select its COM port and divider. The program opens it at the manual default of 115200 bps, configures continuous output, and displays roll/pitch/yaw, gyro Z, and acceleration Z from valid CRC-checked frames.
- The MV3DLP camera connection runs in a separate worker thread. Click `Scan cameras`, select the returned serial number, and click `Connect camera`. The program uses the included SDK to acquire the original image; each fetched source frame is sent directly to the detector, independently of the display refresh rate. Its DLL tree is deployed to `mv3dlp_sdk` beside the executable.
- Automatic correction locates the two edges of the parent laser baseline interruption in full-size SDK images; displaced-contour recovery remains opt-in. Live image centering supplies the main feedback, with a rolling edge fit supplying slower heading guidance. Forward correction keeps the configured chassis-centre cruising speed. Boundary approach gives inward steering priority and can end the initial straight survey/return early. Brief missed detections and wheel reversal delays have bounded recovery windows; persistent observation loss, failed recovery, camera-stream loss, stale wheel feedback or a motion stall stops correction. Start, stop and return transitions retain acceleration and motor limits. The 200 mm image span and 250 mm lookahead are nominal internal values, not verified camera calibration.

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
- legacy `1:1` and `100:1` gearbox settings migrate once to the installed `36:1` default;
- automatic correction stays stopped after the survey return until a fresh valid laser-gap image arrives; and
- baseline-gap detection rejects displaced bright reflections, handles tilted/vertical/diagonal stripes, and produces the same result with or without diagnostic output;
- camera source identities reject duplicate or delayed frames; archived source pixels and metadata support detector replay;
- parallel edge fits use each rotated edge's own longitudinal coordinate and retain outlier rejection and legacy sample compatibility;
- MWD RS485 V3.8 speed frame encoding, CRC16 frame extraction, and feedback parsing match the supplied protocol.

Build and run them with the same toolchain from `tests\build`.

本次纠偏改动新增了上述相关回归测试并进行了静态检查；遵照 Qt 项目要求，未执行编译或测试，不能据此认定实机纠偏已经通过验证。
