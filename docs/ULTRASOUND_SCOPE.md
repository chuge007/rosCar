# 超声改动边界

本文用于执行“只更新超声方面代码”“不要修改小车控制”“只处理 PA-1664 采集/成像/参数”等范围受限的任务。

## 执行原则

当任务被明确限定为超声方向时：

1. 默认只修改本文“超声自有范围”内、且直接服务于当前需求的代码。
2. 不修改小车底盘、激光轮廓、焊道识别、相机、IMU、夹持电机和探头调节行为。
3. 共享文件只允许做最小的超声接线修改，不得顺带重构或改变非超声行为。
4. 如果需求必须跨越超声边界，先说明具体原因、拟修改文件和可能影响；不要静默扩大范围。
5. 完成后报告实际修改范围，并明确是否触碰了共享文件或非超声模块。

## 判定规则

满足以下任一条件时，代码属于超声链路：

- 直接包含 `client.h`、调用 `Client::getInstance()` 或访问厂商 USB SDK。
- 处理 PA-1664 工作组、SDK 连接、采集启停、硬件帧、声束、A/B/C/E 扫或闸门测量。
- 处理超声参数，例如增益、声程、声速、电压、脉宽、聚焦、TCG、探头/楔块、PRF。
- 处理超声原始帧的录制、回放、解码、波形缓存和 A/B/C/E 扫显示。
- 位于 `remotecontrolserver` 的 `ultrasound.*` 白名单链路。
- 处理 PA-1664 USB BootLoader/Streamer 枚举、恢复或 SDK 配置同步。

满足以下任一条件时，代码属于小车控制，不属于默认超声范围：

- 调用 `SynchronizedDriveController` 或 `WheelMotorController`。
- 计算左右轮目标、差速、轮速同步、底盘使能、底盘急停或轮电机反馈超时。
- 向底盘轮电机发送 CAN/RS485 速度、停止或反馈命令。
- 处理激光轮廓、焊道识别、自动纠偏命令、IMU 或小车页相机。

## 超声自有范围

以下文件可作为超声需求的直接修改范围：

- `src/devicecontroller.*`
- `src/parameterpanel.*`
- `src/parameterpresetstore.*`
- `src/pulseloadguard.*`
- `src/framedecoderworker.*`
- `src/packetdecoder.*`
- `src/rawpacket.h`
- `src/scanwidgets.*`
- `src/scanwaveformcache.*`
- `src/framerecorder.*`
- `src/frameplayer.*`
- `src/usbbootstrap.*`
- `src/remotecontrolserver.*`，仅限 `ultrasound.*` 操作、状态、参数接口及它们的最小接线
- `docs/DATA_FORMAT.md`
- `docs/ARCHITECTURE.md` 中仅限超声架构的章节

超声测试或诊断代码，以及仅用于超声采集、解码、成像和参数保存的新增文件，也属于自有范围；新增文件应有清晰、唯一的超声命名。

## 默认禁止修改

未获得明确跨域授权时，不得修改：

- `src/robot/**`
- `src/robotcontrolpanel.*`
- `src/robot_sensor_controller.*`
- `src/robot_usb_camera_controller.*`
- `src/camera_server.*`
- `src/camera_server_client.*`
- `src/camera_frame_processor.*`
- `src/network_stream_reader.*`
- `src/robot_profile_view.*`
- `src/laser_correction_controller.*`
- `src/laser_gap_detector.*`
- `src/laser_path_estimator.*`
- `src/laser_seam_trajectory.*`
- `src/laser_trajectory_renderer.*`
- `src/opencv_laser_contour.*`
- `src/seam_state_machine.*`
- `src/profile_weld_detector.h`
- `src/adaptive_weld_locator.h`
- `src/enhanced_weld_locator.h`
- `src/simple_weld_locator.h`
- `src/probeadjustmentpanel.*`，除任务明确要求修改“超声探头调节”
- `src/jog_button_touch_filter.h`，除任务明确涉及超声侧夹具点动

`src/robot/clamp_motor_controller.*` 及相关 CAN/Modbus 协议文件是共享机械执行模块。它们不属于 PA-1664 超声 SDK，也不是小车驱动轮控制；仅当需求明确涉及探头调节或夹持机构时才可修改。

## 共享文件

以下文件同时连接多个业务域，只能做最小、可解释的增量修改：

- `src/mainwindow.*`
  - 允许：超声页签、A/B/C/E 扫、超声参数、超声采集状态、超声信号槽接线。
  - 不允许：底盘运动逻辑、激光纠偏策略、相机或夹持控制重构。
- `src/main.cpp`
  - 允许：应用初始化、超声 SDK/服务所需的启动逻辑。
  - 不允许：直接引入底盘或激光控制。
- `src/remotecontrolserver.*`
  - 允许：`ultrasound.*` 操作、状态、参数接口及它们的最小接线。
  - 不允许：新增底盘、激光、相机或夹持控制能力，也不允许重构无关的 RPC 处理。
- `PA1664Workbench.pro`
  - 允许：增删本次超声新增的源码/头文件。
  - 不允许：改动 `src/robot/**` 或其他非超声模块的编译项。
- `README.md`、`AGENTS.md`、`docs/**`
  - 允许：随超声改动更新对应说明。
  - 不允许：在本任务中重写整车或激光架构说明。

对共享文件的 diff 必须能用一句话解释为“超声接线”。如果一个 hunk 同时包含超声和非超声行为变化，应拆开或停止修改。

## 修改前检查

修改函数、类或方法前，按仓库规则执行：

```powershell
node .gitnexus/run.cjs impact <symbol> --repo pa1664-workbench --direction upstream
```

若影响分析返回 `HIGH` 或 `CRITICAL`，先向用户说明风险。若符号位于默认禁止范围，应先确认该修改是否真的属于当前超声需求。

可以用以下命令快速核对链路：

```powershell
rg -l '#include "client.h"|Client::getInstance\(\)' src
rg -l 'SynchronizedDriveController|WheelMotorController' src
rg -l 'mv3dlp|LaserCorrectionController|ProfileWeldDetector' src
git diff --name-only
```

## 修改后检查

1. 运行 `git diff --name-only`，逐项确认修改文件属于超声自有范围或已获准的共享接线。
2. 对共享文件的 diff 运行 `git diff -- <shared-files>`，确认每个 hunk 都能解释为超声接线。
3. 运行 `node .gitnexus/run.cjs detect-changes --repo pa1664-workbench`，确认没有意外影响小车、激光或相机执行流。
4. 检查 diff 中没有无关格式化、编码转换、批量重排或顺手重构。
5. Qt 工程默认不主动编译；只有用户明确要求编译或构建验证时才运行构建。
6. 最终说明列出超声改动、共享文件接线，以及“未修改小车控制”的范围结论。

## 当前已知易混点

- `DeviceController::resetEncoder()` 复位的是超声 SDK 采集编码器，不是轮电机编码器。
- `src/robot/` 不代表整个目录都属于底盘控制；`clamp_motor_*` 是共享夹持电机模块。
- `RobotControlPanel` 是小车页聚合层，同时包含底盘、激光、相机、IMU 和夹持子模块。
- `LaserCorrectionController` 会产生底盘纠偏命令，但本身属于小车感知与自动纠偏，不是超声。
- `ProbeAdjustmentPanel` 当前用于超声探头调节，但底层控制的是共享夹持电机，不是 PA-1664 超声 SDK。
