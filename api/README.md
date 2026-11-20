# PA1664 机器人 C++ 调用接口（方案 + 接口文档）

本目录把「读取底盘配置 / 前进 / 后退 / 左转 / 右转 / 使能 / 暂停 / 急停 /
启动纠偏 / 停止纠偏 / X1 X2 Z1 Z2 电机 / 摄像头1 2 / 超声全部参数 / 超声 A 扫」
这一整套功能背后复杂的通信细节（TCP 建链握手、会话密钥、UDP 实时通道、媒体
分片、批量通道、JSON 载荷、50 ms 指令重发、200 ms 运动租约、HMAC 校验、断线
重连、图像与扫描数据编解码）封装成一个 C++ 类。

调用方只写：

```cpp
pa1664::RobotApi robot;   // 默认 192.168.1.20:43100
robot.start();
robot.takeControl();      // connect + enable
robot.forward(0.5);       // 半速前进，持续生效
robot.stopMotion();
```

**不新增任何协议，不改动小车端**。所有帧结构、端口、超时、租约参数都直接复用
`src/common` 里上位机正在用的那一份实现，因此 API 与 `PA1664Workbench.exe`
的行为不会分叉。

---

## 1. 功能覆盖矩阵

| 功能 | 一等公民接口 | 底层 operation / 通道 |
|---|---|---|
| 读取 drive_settings.ini | `loadDriveConfig()` / `requestDriveConfig()` / `applyDriveConfig()` | 本地 ini 或主机 `robot.snapshot` |
| 前进 | `forward(r)` | UDP 43101 `MotionSetpoint` |
| 后退 | `backward(r)` | 同上（linear 取负） |
| 左转 | `turnLeft(r)` | 同上（angular 取负） |
| 右转 | `turnRight(r)` | 同上（angular 取正） |
| 使能 | `enable(bool)` / `takeControl()` | `robot.enable` / `robot.connect` |
| 暂停（运动） | `stopMotion()` | UDP 零速 `MotionSetpoint` |
| 暂停（纠偏） | `pauseAutoCorrection(bool)` | `robot.autoCorrection.pause` |
| 急停 | `emergencyStop()` | UDP `EmergencyStop` + `robot.emergencyStop` |
| 启动纠偏 | `startAutoCorrection(json)` | `robot.autoCorrection.start` |
| 停止纠偏 | `stopAutoCorrection()` | `robot.autoCorrection.stop` |
| X1/X2/Z1/Z2 电机 | `x1Motor()` `x2Motor()` `z1Motor()` `z2Motor()`、`clampSetSpeed()` … | `robot.clamp.*` |
| 摄像头1/2 | `connectCamera()` / `connectNetworkCamera()` + 帧回调 | `robot.camera.connect` + UDP 43102 `0x0300+slot` |
| 超声全部参数 | `ultrasoundSetParameter()` / `ultrasoundParameterSnapshot()` … | `ultrasound.parameter*` |
| 超声 A 扫 | `configureUltrasoundDecoder()` + `setUltrasoundFrameHandler()` | UDP 43102 `0x0100+gid` / TCP 43103 批次 |
| 激光轮廓 | `setLaserProfileHandler()` | UDP 43102 `kind=10` / streamId `0x0204`（PAD1 type 6） |
| 超声预设/精度/标定 | `switchUltrasoundPreset()` / `setUltrasoundScanPrecision()` … | `ultrasound.presets.*` / `scan.precision.*` / `encoder.*` |
| 外围设备 | `connectLaser()` / `connectImu()` / `requestSerialPorts()` | `robot.laser.*` / `robot.imu.*` / `robot.peripherals.*` |

`request(operation, json)` 是通用逃生口，覆盖本表之外的所有主机能力
（IMU、激光、预设、扫查精度、编码器标定等）。

---

## 2. 目录结构

```
api/
├── include/pa1664_robot_api.h     对外唯一头文件（不含任何 Qt 类型）
├── src/pa1664_robot_api.cpp       实现：会话桥接 + 单位换算 + 数据解码
├── examples/forward_demo.cpp      最小示例：连接→取控制权→前进 2 s→停车
├── examples/examples.pro          示例工程
├── PA1664RobotApi.pro             静态库工程
├── build_api.bat                  一键编译（lib + example，参数 libonly 只编库）
├── lib/PA1664RobotApi.lib         构建产物（release）
├── API_REFERENCE.md               完整 API 说明书（每个接口的签名、指令、示例、注意事项）
└── README.md                      本文档（方案与逐功能说明）
```

`api/` 不参与 `PA1664Workbench.pro` / `PA1664RobotHost.pro`，独立编译，
不会影响 `bin\PA1664Workbench.exe` 与 `bin\PA1664RobotHost.exe`。

`api\小车api\` 是**纯产物交付目录**，只有头文件和两种库：

```
小车api\
├── include\pa1664_robot_api.h     头文件（唯一需要 include 的文件）
├── static\PA1664RobotApi.lib      静态库
└── shared\PA1664RobotApi.dll      动态库
    shared\PA1664RobotApi.lib      动态库导入库
```

用法二选一：

- **静态**：`INCLUDEPATH += ...\小车api\include`，
  `LIBS += ...\小车api\static\PA1664RobotApi.lib`，不定义任何宏；
- **动态**：`INCLUDEPATH += ...\小车api\include`，
  `LIBS += ...\小车api\shared\PA1664RobotApi.lib`，并定义 `PA1664_ROBOT_API_DLL`；
  运行目录放 `PA1664RobotApi.dll` 与 Qt6Core/Qt6Network/Qt6Gui.dll。

库本身不带任何配置文件；首次运行会在 exe 旁自动生成
`config\pa1664_profile.json`（远端配置的本地镜像，见 5.15），无需随库分发。

重建产物：`build_api.bat libonly`（静态 → `api\lib\`）、`build_api.bat shared`
（动态 → `api\build\shared\`），再把生成物拷进 `api\小车api\` 对应目录。

> 中文路径注意：qmake 写的 Makefile 是 UTF-8，nmake 按 ANSI 读，在含中文的
> 路径下 link 会报 `LNK1181`。因此不要把 `小车api` 目录当作构建目录使用，
> 构建统一在 `api\`（纯英文路径）完成，交付目录只放产物。

---

## 3. 分层：只加一层，且是纯翻译层

```
第三方调用代码
   └── pa1664::RobotApi                        ← 本目录新增（唯一需要看的接口）
         └── pa1664::remote::RemoteWorkbenchSession   ← 复用：控制/实时/媒体/批量 会话聚合
               ├── ControlClient (TCP 43100)          ← 复用：握手、request/response、重连
               ├── RealtimeClient (UDP 43101)         ← 复用：50 ms 重发、200 ms 租约、急停
               ├── MediaClient  (UDP 43102)           ← 复用：摄像头帧、超声 A 扫
               ├── BulkClient   (TCP 43103)           ← 复用：超声扫描批次
               └── control_codec / realtime_codec / media_codec / bulk_codec / workbench_data_codec
                   帧头、字段序、字节序、HMAC、图像与扫描载荷 —— 与 Workbench 同一份代码
```

新增的只有 `RobotApi`：把「速度比例 → 米/秒」「枚举 → 中文状态」「QImage →
裸像素」「DecodedFrame → A 扫视图」翻译掉，**不重写任何 socket / 编解码**。

---

## 4. 编译与集成

### 4.1 编译

```bat
cd /d D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\api
build_api.bat libonly      :: 只编库 → api\lib\PA1664RobotApi.lib
build_api.bat              :: 库 + 示例 → api\examples\bin\forward_demo.exe
```

编完可跑离线自检（不建链、不动电机，逐个调用全部接口验证符号与参数校验）：

```bat
set PATH=D:\qt5.15.2\6.8.3\msvc2022_64\bin;%PATH%
examples\bin\forward_demo.exe --selftest
examples\bin\forward_demo.exe --selftest C:\path\to\drive_settings.ini
```

连真机验证通讯（只建链、收遥测、发一次 `robot.snapshot` 与零速包，**不会让车动**）：

```bat
examples\bin\forward_demo.exe --link 192.168.1.20
:: 再加 connect：额外下发 robot.connect + robot.enable，仍不发任何速度
examples\bin\forward_demo.exe --link 192.168.1.20 connect
```

配置项验证（本地 ini + 主机回传 + 超声状态/全量参数/单参数读写）：

```bat
:: 本地 ini 放在 <exe 目录>\config\drive_settings.ini 时可直接读
examples\bin\forward_demo.exe --config 192.168.1.20
```

脚本内固定 `QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64` 与 `vcvars64.bat`，
与 `build_workbench.bat` 一致；构建目录 `api\build\` 已被 `.gitignore` 覆盖。

### 4.2 集成到自己的工程

qmake：

```pro
QT += core network gui
INCLUDEPATH += <repo>/api/include
LIBS += -L<repo>/api/lib -lPA1664RobotApi
```

CMake：

```cmake
add_library(pa1664_robot_api STATIC
    <repo>/api/src/pa1664_robot_api.cpp
    <repo>/src/common/protocol/control_codec.cpp
    <repo>/src/common/protocol/realtime_codec.cpp
    <repo>/src/common/protocol/media_codec.cpp
    <repo>/src/common/protocol/bulk_codec.cpp
    <repo>/src/common/transport/control_client.cpp
    <repo>/src/common/transport/realtime_channel.cpp
    <repo>/src/common/transport/media_channel.cpp
    <repo>/src/common/transport/bulk_channel.cpp
    <repo>/src/common/services/remote_workbench_session.cpp
    <repo>/src/common/services/workbench_data_codec.cpp
    <repo>/src/robot_app_logger.cpp)
target_include_directories(pa1664_robot_api PUBLIC
    <repo>/api/include <repo>/src <repo>/src/common <repo>/src/robot)
target_compile_features(pa1664_robot_api PUBLIC cxx_std_17)
target_link_libraries(pa1664_robot_api PUBLIC Qt6::Core Qt6::Network Qt6::Gui)
```

依赖：Qt 6.8 Core + Network + Gui（Gui 仅用于 `QImage` 解码），C++17。
不需要 Widgets、串口、CAN、相机 SDK。vcpkg/自建均可。

### 4.3 会话与线程前提

```cpp
QCoreApplication app(argc, argv);   // 必须有 Qt 事件循环
pa1664::RobotApi robot;
robot.setConnectionHandler([](bool online, const std::string& detail) { ... });
robot.start();                      // 立即返回，后台握手 + 自动重连
return app.exec();
```

- 必须在**有事件循环的线程**构造、使用、销毁（通常是 main）。
- 所有回调都在该线程触发；回调里不要阻塞。
- 小车端 `PA1664RobotHost.exe` 必须先启动，且底盘参数已在工作台保存过一次
  （`robot_host\app\config\drive_settings.ini`）。

---

## 5. 功能逐项说明

下面每一项都给出：**接口** → **实际指令** → **示例** → **注意事项**。

### 5.1 读取 drive_settings.ini

**接口**

```cpp
DriveConfig loadDriveConfig(const std::string& path = "");  // 自由函数
void RobotApi::applyDriveConfig(const DriveConfig& drive);
```

**文件与指令**：不经过网络，直接读本地 ini。`path` 为空时等价于上位机的
`DriveSettings::persistentFilePath()`：

```
<exe 目录>\config\drive_settings.ini      // group = "drive"
```

小车端的那份在主机 PC 上：`C:\Users\a\Desktop\robot_host\app\config\drive_settings.ini`。
若本程序不在主机上跑，需要先把文件拷过来或显式传路径。

**示例**

```cpp
pa1664::DriveConfig drive = pa1664::loadDriveConfig();   // 或传绝对路径
if (!drive.loaded) {
  std::printf("读取失败：%s\n", drive.error.c_str());
} else {
  std::printf("schema=%d 轮径=%.4f 轮距=%.4f 最大线速度=%.3f m/s\n",
              drive.schemaVersion, drive.wheelRadiusM, drive.trackWidthM,
              drive.maximumLinearSpeedMps);
  for (const auto& m : drive.clampMotors)
    std::printf("夹爪 %s: mode=%s port=%s id=%d\n", m.name.c_str(),
                m.mode.c_str(), m.port.c_str(), m.motorId);
  std::printf("摄像头0: source=%s index=%d fps=%d\n",
              drive.cameras[0].source.c_str(), drive.cameras[0].deviceIndex,
              drive.cameras[0].fps);

  robot.applyDriveConfig(drive);   // 用 ini 的限幅对齐本机 Config
}
```

**从主机拉取（本地没有 ini 时用）**

```cpp
robot.setDriveConfigHandler([](const pa1664::DriveConfig& drive) {
  if (!drive.loaded) { std::printf("主机没给配置：%s\n", drive.error.c_str()); return; }
  robot.applyDriveConfig(drive);     // 用主机真实限幅覆盖本机默认值
  std::printf("主机配置 schema=%d 轮距=%.3f 夹爪=%d 台\n", drive.schemaVersion,
              drive.trackWidthM, int(drive.clampMotors.size()));
});
robot.requestDriveConfig();          // = robot.snapshot，解码 settings.iniBase64
```

**注意**

- `raw` 只包含 group `drive` 下的**标量**键；`clampXMotors` 这类数组用
  `clampMotors` 读取。
- schema 13 起用数组描述每轴电机数量，`clampXMotors/size=0` 表示该轴没有
  电机（本仓库实机就是 X 轴 2 台、Z 轴 2 台、Y 轴 0 台）；只有老文件（没有
  `clampAxesConfigured`）才会回退到旧版单电机键。
- `applyDriveConfig()` 只改本机的速度限幅，**不会下发到主机**；主机仍以自己
  ini 内的参数为准。两者不一致时以 `RobotStatus::applied*` 为准。
- ini 的键名与 `DriveSettings::save()` 一一对应（schema 13），新增字段请同步
  两边，否则 API 读不到。

### 5.2 前进

**接口**：`Result forward(double speedRatio = 1.0)`

**实际指令**：

```
UDP 43101  MotionSetpoint
  linearMilliMps  = +round(speedRatio * Config::maxLinearSpeedMps * 1000)
  angularMilliRadps = 0
  ttlMs = Config::motionLeaseMs (默认 200)
每 50 ms 重发一帧（RealtimeClient 的 PreciseTimer）
```

**示例**

```cpp
pa1664::Result r = robot.forward(0.5);   // 0.06 m/s（默认上限 0.12）
if (!r.ok) std::printf("失败：%s\n", r.message.c_str());
// 持续前进，直到：
robot.stopMotion();
```

**注意**

- **持续指令**：调用一次就一直走，不调 `stopMotion()` 不会自己停。
- 必须先 `takeControl()` 且 `RobotStatus::state == Enabled`，否则主机忽略。
- `speedRatio` 会被夹到 `[-1, 1]`；负数等于后退，但请用 `backward()`。
- 主机对 200 ms 内没有新指令的会话自动安全停车；API 进程崩溃也会在 200 ms
  内停车，这是唯一不依赖调用方代码的保护。
- 速度上限必须与主机 `maximumLinearSpeedMps` 一致，否则主机限幅，实际值看
  `RobotStatus::appliedLinearMps`。

### 5.3 后退

**接口**：`Result backward(double speedRatio = 1.0)`

**实际指令**：与前进相同，`linearMilliMps` 取负值。

**示例**

```cpp
robot.backward(0.3);                 // -0.036 m/s
QTimer::singleShot(1500, [&] { robot.stopMotion(); });
```

**注意**

- 斜坡由主机 `maximumLinearAccelerationMps2` 处理，反向时需要先经过零速，
  不要在前进中直接 `backward()` 期待瞬间反向。
- 后退时 `left/right.wheelSpeedMps` 为负，属正常。

### 5.4 左转

**接口**：`Result turnLeft(double speedRatio = 1.0)`

**实际指令**：`linearMilliMps = 0`，`angularMilliRadps = -round(ratio * maxAngular * 1000)`。

**示例**

```cpp
robot.turnLeft(0.5);                 // 原地左转 0.436 rad/s（25 deg/s）
QTimer::singleShot(1000, [&] { robot.stopMotion(); });
```

**注意**

- **负角速度 = 左转**。这是差速混合器的约定（`DifferentialMixer::mix()` 的
  注释：正角速度表示右转，左轮为外侧轮）。自行调 `moveVelocity()` 时必须遵守。
- 原地转向时左右轮反向，轮速会接近 `maximumWheelSpeedMps`，主机可能限幅。
- 直线行驶中叠加转向请用 `moveVelocity(v, w)`，别连续调 `forward` + `turnLeft`。

### 5.5 右转

**接口**：`Result turnRight(double speedRatio = 1.0)`

**实际指令**：`angularMilliRadps = +round(ratio * maxAngular * 1000)`。

**示例**

```cpp
robot.turnRight(0.4);
QTimer::singleShot(800, [&] { robot.stopMotion(); });
```

**注意**：同左转，符号相反；角速度上限来自 `Config::maxAngularSpeedRadps`
（默认 50 deg/s）。

### 5.6 使能

**接口**

```cpp
Result connectChassis();          // robot.connect（不带 settings）
Result enable(bool enabled = true);// robot.enable
Result takeControl();             // connect → 1.2 s → enable
```

**实际指令**

```
TCP 43100  Request  operation="robot.connect"  data={}
  → 主机用自己 ini 里的驱动配置 connectDrive()，应答 {"queued":true,"settings":{...}}
TCP 43100  Request  operation="robot.enable"   data={"enabled":true}
```

**示例**

```cpp
robot.setStatusHandler([](const pa1664::RobotStatus& s) {
  std::printf("state=%s reason=%s\n", s.stateText.c_str(), s.reason.c_str());
});
robot.takeControl();                  // 一条命令：连接 + 使能
// 或者分两步，自己控制时序：
// robot.connectChassis();
// QTimer::singleShot(1500, [&]{ robot.enable(true); });
```

**注意**

- 顺序不能反：`robot.enable` 在未 `connect` 时会让主机进入
  `安全故障：轮子电机库未连接`。
- 使能后主机要检查电机反馈（`Arming`），约 `armingTimeoutMs`（默认 1 s）后
  才变 `Enabled`；`takeControl()` 内部等 1.2 s 就是按这个值 + 余量。
- `robot.connect` 带空 settings 表示沿用主机 ini；如果主机从未配置过底盘，
  会回 `invalid_settings`（缺串口/ID 等），必须先用工作台保存一次。
- 需要能力 `robot.control.v1`，未声明时 `enable()` 直接返回 `HostRejected`。
- 同一台主机只允许一个控制会话，被占用时事件 `host.busy`。

### 5.7 暂停

有两种「暂停」，别混用：

| 场景 | 接口 | 底层 |
|---|---|---|
| 停止车辆运动（界面上的停车） | `stopMotion()` | UDP 零速 `MotionSetpoint` |
| 暂停/继续自动纠偏 | `pauseAutoCorrection(true/false)` | `robot.autoCorrection.pause` |

**示例**

```cpp
robot.stopMotion();                    // 车停下，仍保持使能，可继续发运动
...
if (correctionRunning) robot.pauseAutoCorrection(true);   // 纠偏暂停
robot.pauseAutoCorrection(false);                          // 纠偏继续
```

**注意**

- `stopMotion()` 不清除使能，也不会锁存；重新 `forward()` 会立刻恢复行驶。
- `pauseAutoCorrection()` 只在纠偏已运行时有效，否则主机回
  `correction_not_active`。
- 纠偏暂停期间手动运动通道被禁用（工作台同款逻辑），要手动开车请先
  `stopAutoCorrection()`。
- 界面上的「暂停」按钮语义是暂停纠偏，不是停车；两者别搞反。

### 5.8 急停

**接口**：`Result emergencyStop()`

**实际指令**（双通道，互不依赖）：

```
UDP 43101  EmergencyStop  payload {"latched":true}  ×5 帧（50 ms 间隔）
TCP 43100  Request  "robot.emergencyStop" data {"latched":true}
```

**示例**

```cpp
robot.emergencyStop();     // 立即调用，不要等
// 恢复：主机进入 EmergencyStop 状态，必须重新使能
QTimer::singleShot(1000, [&] {
  robot.reset();           // 需要时先清故障
  robot.enable(true);
});
```

**注意**

- 不要用 `stopMotion()` 代替急停：急停是**锁存**的，且同时停夹爪。
- UDP 单帧即可锁存，TCP 断了也能生效；这就是为什么 API 两条都发。
- 急停后 `RobotStatus::state == EmergencyStop`，运动指令会被忽略。
- 触发急停的事件也会以 `robot.emergencyStop` / `robot.safetyStop` 出现在
  事件回调里。

### 5.9 启动纠偏

**接口**：`Result startAutoCorrection(const std::string& settingsJson = "{}")`

**实际指令**

```json
operation = "robot.autoCorrection.start"
data = { "settings": { ...LaserCorrectionSettings 字段子集... } }
```

常用键（其余用主机默认值）：

| 键 | 默认 | 含义 |
|---|---|---|
| `targetSpeedMps` | 0.005 | 纠偏目标速度，主机限到 `0.8 × min(最大线速度, 最大轮速)` |
| `segmentLengthM` | 0.03 | 每次拟合的焊缝段长，限 0.001..1.0 |
| `proportionalGain` | 3.0 | 横向偏差比例增益 |
| `derivativeGain` | 0.12 | 微分增益 |
| `imageTimeoutMs` | 500 | 激光帧超时，限 100..5000 |
| `maximumCurvatureRadPerM` | 1.50 | 转向曲率上限 |

**示例**

```cpp
robot.startAutoCorrection(R"({"targetSpeedMps":0.008,"segmentLengthM":0.04})");
// 状态经事件回调：robot.autoCorrection.starting / started / status / failed
robot.setCorrectionStatusHandler([](const pa1664::CorrectionStatus& s) {
  std::printf("纠偏 active=%d paused=%d phase=%s 速度=%.4f\n", s.active,
              s.paused, s.phase.c_str(), s.linearCommandMps);
});
```

**注意**

- 前置条件（主机逐条校验，缺一不可）：底盘已连接、`state == Enabled`、
  激光轮廓相机已连接。否则回 `drive_not_connected` / `drive_not_enabled` /
  `laser_not_connected`。
- 应答 `{"state":"preparing"}` 只表示开始准备；真正运行看
  `robot.autoCorrection.started` 事件。
- 纠偏运行时会接管运动通道，手动 `forward()` 无效；要手动接管必须先停止纠偏。
- 纠偏会自动前进，不要同时发运动指令。

### 5.10 停止纠偏

**接口**：`Result stopAutoCorrection()`

**实际指令**：`robot.autoCorrection.stop`，应答 `{"wasRunning":true|false}`。

**示例**

```cpp
robot.stopAutoCorrection();
robot.stopMotion();      // 双保险：确保底盘不再前进
```

**注意**

- 停止后底盘不会自动停车，请紧跟 `stopMotion()`。
- 停止事件 `robot.autoCorrection.stopped` 带 `reason`（`operator_stop`、
  `operator_timeout`、`motion_lease_expired`、`udp_emergency_stop` 等）。
- 操作端控制连接断开 3 s（`kSessionTimeoutMs`）主机也会自动停纠偏。

### 5.11 X1 / X2 / Z1 / Z2 电机

**坐标映射**（与工作台界面名字一致，`axisName(axis)+(index+1)`）：

| 名称 | axis | index |
|---|---|---|
| X1 | 0 (X 轴) | 0 |
| X2 | 0 (X 轴) | 1 |
| Y1 | 1 (Y 轴) | 0 |
| Z1 | 2 (Z 轴) | 0 |
| Z2 | 2 (Z 轴) | 1 |

**接口与指令**

| 接口 | operation | 载荷 |
|---|---|---|
| `x1Motor(p)` / `clampSetSpeed(axis,index,p)` | `robot.clamp.setMotorSpeed` | `{"axis":0,"index":0,"percent":p}` |
| `clampStopMotor(axis,index)` | `robot.clamp.stopMotor` | `{"axis":0,"index":0}` |
| `clampStop()` | `robot.clamp.stop` | `{}` |
| `clampMoveToPosition(axis,index,deg)` | `robot.clamp.moveToPosition` | `{"axis":..,"index":..,"degrees":..}` |
| `clampRequestPosition(axis,index)` | `robot.clamp.requestPosition` | `{"axis":..,"index":..}` |
| `clampRequestPositions()` | `robot.clamp.positions` | `{}` |
| `clampConnect()` / `clampDisconnect()` | `robot.clamp.connect` / `robot.clamp.disconnect` | `{}` |
| `requestClampStatus()` | `robot.clamp.status` | `{}` |
| `requestClampSettings()` | `robot.clamp.settings.get` | `{}` |

`percent` ∈ [-100, 100]，正负由 ini 里该电机的 `sign` 决定。

**示例**

```cpp
robot.clampConnect();                        // 先连接夹爪总线
// ... 等 robot.clamp.connection 事件 connected=true

robot.x1Motor(40);      // X1 正转 40%
robot.x2Motor(-40);     // X2 反转 40%
robot.z1Motor(25);      // Z1 正转 25%
robot.z2Motor(-25);     // Z2 反转 25%

QTimer::singleShot(2000, [&] {
  robot.clampStop();    // 全部停
});

// 位置模式（仅部分协议支持）
robot.clampMoveToPosition(2, 0, 120.0);      // Z1 走到 120 度
// 位置反馈走事件：robot.clamp.position {axis,index,degrees}
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.position") std::printf("%s\n", data.c_str());
});
```

**注意**

- 必须先 `robot.clamp.connect`（或 `robot.clamp.connect` 带 settings 先更新
  配置），否则回 `clamp_service_unavailable`。
- 速度是电机额定转速的百分比，不是 mm/s；实际位移与 ini 里的 `gearRatio`
  （每转多少 mm）有关。
- `moveToPosition` / `requestPosition` 需要协议支持（TT CANopen、MWD、ZDT
  视固件而定），不支持时回 `position_unsupported`。
- 上限/下限（`upperLimit`/`lowerLimit`）在主机侧生效，触发时发
  `robot.clamp.limitReached` 事件并停机。
- `axis` 与 `index` 必须落在 ini 配置的数量范围内，越界回 `invalid_argument`。

### 5.12 摄像头 1 / 摄像头 2

**接口**

```cpp
Result connectCamera(int slot, int deviceIndex, int fps = 15,
                     bool flipHorizontal = false, bool flipVertical = false);
Result connectNetworkCamera(int slot, const std::string& url, ...);
Result disconnectCamera(int slot);
Result disconnectAllCameras();
Result requestCameraStatus();      // robot.camera.status
Result scanCameras();              // robot.camera.scan，结果走 robot.camera.devices 事件
Result setCameraDisplaySize(int slot, int width, int height);
void setCameraFrameHandler(std::function<void(const CameraFrame&)>);
```

**实际指令与数据流**

```
TCP 43100  "robot.camera.connect"
           {"slot":0|1,"source":"usb","deviceIndex":N,"fps":15,
            "flipHorizontal":false,"flipVertical":false}
           或 {"slot":..,"source":"network","url":"rtsp://..."}
下行：UDP 43102  媒体流 0x0300 + slot，kind = UsbCameraImage
      载荷 = encodeImagePayload(QImage, jpeg 质量 72)
      → API 用 decodeImagePayload 还原成 QImage，再以裸像素交给回调
```

**示例**

```cpp
robot.setCameraFrameHandler([](const pa1664::CameraFrame& f) {
  // 只在回调内有效，需要留存请自行拷贝
  std::printf("相机%d %dx%d stride=%d bytes=%d fmt=%d\n", f.slot + 1,
              f.width, f.height, f.stride, f.sizeBytes, int(f.format));
  // OpenCV: cv::Mat(h, w, CV_8UC1, (void*)f.data, f.stride)
});

robot.connectCamera(0, 0, 15);          // 摄像头1：USB 设备 0
robot.connectNetworkCamera(1, "rtsp://192.168.1.30:554/stream1");  // 摄像头2
```

**注意**

- `slot` 只能是 0（摄像头1）或 1（摄像头2）。
- 画质由主机固定为 JPEG 72，分辨率由主机的 `robot.camera.displaySize`
  决定；需要更高分辨率可先发 `robot.camera.displaySize`。
- 回调频率可达 15~30 fps，**不要在回调里做重活**；需要显示请丢到自己的队列。
- `PixelFormat::RGBA8888` 实际是 BGRA 字节序（Qt `ARGB32`），OpenCV 用
  `CV_8UC4` 直接按 BGR 读。
- 网络摄像头需要在主机侧能访问该 URL；USB 设备编号可用
  `robot.camera.scan` 扫描（结果走事件 `robot.camera.devices`）。
- 断线/拔插会发 `robot.camera.connection` 事件，可用它做重连。

### 5.13 超声：设置所有参数

超声参数用**厂商 SDK 方法名**寻址，共 87 项，`get*` 读取、`set*` 写入。

**接口**

```cpp
Result ultrasoundParameterSnapshot();   // 一次读全部
Result ultrasoundGetParameter(method, argsJson = "[0]");
Result ultrasoundSetParameter(setMethod, argsJson, readbackMethod = "", readbackArgsJson = "");
Result ultrasoundApplyParameters(changesJson);
Result ultrasoundParametersSettled(bool resumeCapture = true);

// 预设 / 扫查精度 / 编码器标定（主机端 parameter_presets.json，掉电保留）
Result requestUltrasoundPresets();               // ultrasound.presets.list
Result switchUltrasoundPreset(int presetId);     // 切换工况，主机会重配硬件
Result captureUltrasoundPreset(int presetId);    // 把当前硬件参数存成预设
Result requestUltrasoundScanPrecision();         // scan.precision.get
Result setUltrasoundScanPrecision(int presetId, double mmPerCell);
Result requestUltrasoundEncoderCalibration();    // encoder.calibration.get
Result setUltrasoundEncoderCalibration(const std::string& json);
Result ultrasoundResetEncoder();                 // encoder.reset
Result ultrasoundRefreshGeometry();              // geometry.refresh
```

**实际指令**

```json
// 读单个
operation="ultrasound.parameters.getMany"
data={"requests":[{"method":"getGain","args":[0]}]}

// 写单个（必须带回读方法，主机写后立即回读校验）
operation="ultrasound.parameter.set"
data={"method":"setGain","args":[40,0],
      "readbackMethod":"getGain","readbackArgs":[0]}

// 批量写
operation="ultrasound.parameters.apply"
data={"changes":[{"method":"setGain","args":[40,0],
                  "readbackMethod":"getGain","readbackArgs":[0]}, ...]}

// 一次读全部：返回 values{ "方法名:base64(args)" : 值 } + configBase64 + status
operation="ultrasound.parameters.snapshot"

// 参数写入后让主机恢复采集（真停 → 800 ms → 自动启动）
operation="ultrasound.parameters.settled" data={"resumeCapture":true}
```

**参数全集**（括号内是 getter 的默认参数，setter 一律为「值..., groupId」）

| 分组 | 参数（getter） |
|---|---|
| 波束/角度 | `getBeamAngleLinear`、`getBeamAngleMax`、`getBeamAngleMin`、`getBeamAngleStep`、`getBeamAperture`、`getBeamElementStep`、`getBeamFirstElement`、`getBeamLastElement`、`getBeamMode`、`getScanMode`、`getScanType`、`getScannerDirection` |
| 增益/滤波 | `getGain`、`getFilterHigh`、`getFilterLow`、`getVideoFilterMHz`、`getRectifierMode`、`getReceiverReject`、`getMaxAmplitude`、`getTcgEnable` |
| 闸门 A | `getGateAEnable`、`getGateAStart`、`getGateAEnd`、`getGateAThreshold`、`getGateAMeasureType`、`getGateASynchronMode` |
| 闸门 B | `getGateBEnable`、`getGateBStart`、`getGateBEnd`、`getGateBThreshold`、`getGateBMeasureType`、`getGateBSynchronMode` |
| 闸门 C | `getGateCEnable`、`getGateCStart`、`getGateCEnd`、`getGateCThreshold`、`getGateCMeasureType`、`getGateCSynchronMode` |
| 闸门 I | `getGateIEnable`、`getGateIStart`、`getGateIEnd`、`getGateIThreshold`、`getGateIMeasureType`、`getGateISynchronMode`、`getGateISyncSample` |
| 发射 | `getPaVoltage`、`getUtVoltage`、`getPulseWidth`、`getVoltagePolarity`、`getTransmissionChannel`、`getReceptionChannel`、`getDualCrystalEnable` |
| 接收/聚焦 | `getFocalPosition`、`getFocusMode`、`getFrameRate`、`getPointQuantity`、`getReverseEnable`、`getTfmRecDelay`、`getThickness` |
| TCG | `getTcgPointDepth`、`getTcgPointDepthIndex`、`getTcgPointGain`、`getTcgPointGainIndex` |
| 量程 | `getRangeStart`、`getRangeEnd` |
| 探头 | `getProbeFrequency`、`getProbePrimaryElements`、`getProbePrimaryElementsPitch`、`getProbeSecondaryElements`、`getProbeSecondaryElementsPitch` |
| 楔块 | `getWedgeEnable`、`getWedgeAngle`、`getWedgeRoofAngle`、`getWedgeHeight`、`getWedgeLength`、`getWedgeWidth`、`getWedgeX`、`getWedgeZ`、`getWedgeVelocity`、`getWedgeGCp`、`getWorkpieceVelocity` |
| 设备状态（只读） | `getGroupsNo`、`getBeamCounts`、`getCurrentGroup` |

**示例**

```cpp
// 1) 读单个参数
robot.ultrasoundGetParameter("getGain", "[0]");

// 2) 写单个参数（回读方法可省略，默认 setXxx → getXxx）
robot.ultrasoundSetParameter("setGain", "[40,0]");        // 增益 40，组 0
robot.ultrasoundSetParameter("setRangeStart", "[2.5,0]");
robot.ultrasoundSetParameter("setRangeEnd", "[60.0,0]");

// 3) 批量写（推荐：一次事务、一次重配硬件）
robot.ultrasoundApplyParameters(R"([
  {"method":"setGain","args":[40,0],"readbackMethod":"getGain","readbackArgs":[0]},
  {"method":"setFilterLow","args":[1.0,0],"readbackMethod":"getFilterLow","readbackArgs":[0]},
  {"method":"setGateAStart","args":[5.0,0],"readbackMethod":"getGateAStart","readbackArgs":[0]}
])");
robot.ultrasoundParametersSettled(true);     // 参数生效后恢复采集

// 4) 读全部参数（一次快照，约 87 次 SDK 读取）
robot.ultrasoundParameterSnapshot();
// 结果在响应回调里：dataJson 含 values / configBase64 / status / errors
```

**注意**

- 所有参数都跑在**小车主机进程**里（厂商 SDK 只装在主机上）；本 API 只发
  RPC，不直接调 SDK。
- `ultrasound.parameter.set` 必须带**回读**方法，否则主机拒绝
  （`invalid_parameter_set`）；API 会按 `setXxx → getXxx` 自动推导，并把
  setter 的最后一个参数（groupId）作为 getter 参数。
- 写参数会让 SDK 重配 USB 端点，采集会中断约 0.8 s；务必在写完后再调
  `ultrasoundParametersSettled()`，不要自己 `acquisition.start`（会撞上重配
  窗口，表现为画面卡死）。
- `ultrasound.parameters.commit` 不是「提交参数」，它的载荷是
  `{"presetId":N}`（把当前硬件参数存成预设），别用错。
- 参数值范围由 SDK 校验，非法值回 `parameter_write_failed`，可见 errorText。
- 设备状态（`getGroupsNo` / `getBeamCounts` / `getCurrentGroup`）建议在连接
  后先读一次，用于配置解码工作组。

### 5.14 超声：A 扫数据接收

**完整流程**

```
1) ultrasound.device.connect        {"address":"127.0.0.1","deviceId":0}
2) ultrasound.status                → device{pointCount, beamCount, maxAmplitudeValue}
3) ultrasound.decoder.settings      {"groups":[{...几何...}],"displayGroupId":0}   ← 库自动完成
4) ultrasound.acquisition.start
5) 数据下行两条路径：
   实时：UDP 43102  streamId = 0x0100 + groupId, kind = UltrasoundFrame
         payload = encodeDecodedFrame(DecodedFrame)
   批量：TCP 43103  kind = UltrasoundScanBatch
         payload = encodeGroupScanBatch(GroupScanFrameBatch)
6) 回调 setUltrasoundFrameHandler() 拿到 A 扫波形（qint16 采样）
```

**接口**

```cpp
Result configureUltrasoundDecoder(const UltrasoundDecoderConfig& config);
void setUltrasoundFrameHandler(std::function<void(const UltrasoundAScanFrame&)>);
```

**示例**

```cpp
robot.setUltrasoundStatusHandler([](const pa1664::UltrasoundStatus& s) {
  std::printf("超声 online=%d capturing=%d point=%d beam=%d maxAmp=%d\n",
              s.hardwareOnline, s.capturing, s.pointCount, s.beamCount,
              s.maxAmplitudeValue);
  // 用真实几何配置解码器（否则解不出 A 扫）
  pa1664::UltrasoundDecoderConfig cfg;
  cfg.groupId = s.currentGroup;
  cfg.pointCount = s.pointCount;
  cfg.beamCount = s.beamCount;
  cfg.amplitudeScale = s.maxAmplitudeValue;
  cfg.scanModes = 0x1;              // 只开 A 扫
  cfg.selectedBeam = 0;
  cfg.rangeStart = 2.5;
  cfg.rangeEnd = 60.0;              // 单位 mm，与 getRangeStart/End 一致
  robot.configureUltrasoundDecoder(cfg);
});

robot.setUltrasoundFrameHandler([](const pa1664::UltrasoundAScanFrame& f) {
  // f.beams[b][p]：第 b 条波束第 p 个采样（qint16）
  const std::int16_t* beam0 = f.beams[0];
  std::printf("组%d 帧%u 点数%d 首值%d 末值%d\n", f.groupId, f.frameNumber,
              f.pointCount, beam0[0], beam0[f.pointCount - 1]);
  if (f.measurementCount > 0)
    std::printf("闸门A 幅值%u 位置%u\n", f.measurements[0].amplitudeA,
                f.measurements[0].positionA);
});

robot.ultrasoundConnect("127.0.0.1", 0);
QTimer::singleShot(800, [&] { robot.requestUltrasoundStatus(); });
QTimer::singleShot(1500, [&] { robot.ultrasoundStartAcquisition(); });
```

**注意**

- 解码器配置**由库自动下发**（见 5.15）：连上后收到 `ultrasound.status` 就会按
  「远端几何 + 本地偏好」发一次 `ultrasound.decoder.settings`，正常不需要手动调。
  只有临时改扫描模式/量程时才手动 `configureUltrasoundDecoder()`。
- 如果关掉了自动流程（`setProfileOptions({autoApplyDecoder:false})`），就必须自己配：
  主机用这份几何把原始包解成 `DecodedFrame`，配错 `pointCount`/`beamCount`/
  `groupOffsetWords` 就收不到帧，且无错误提示。
- `pointCount` / `beamCount` / `maxAmplitudeValue` 必须取自
  `ultrasound.status` 的 `device` 段，不能猜。
- A 扫是**实时流**，丢帧不会重传；C 扫/组扫走批量通道（TCP 43103），
  可靠性更高。
- `beams` / `measurements` 指针只在回调内有效；要缓存请自行 memcpy。
- 回调频率约 20 Hz，单帧 1 波束 × 1024 点 ≈ 2 KB，处理要快。
- `frameNumber` 是设备帧号，可用来判断丢帧（不连续即丢帧）。
- 采样值是原始 ADC 码，量纲换算用 `amplitudeScale`（= `maxAmplitudeValue`）。
- 采集停止后回调自然停止；`ultrasound.acquisition.stop` 不会清空主机缓冲，
  可能在停止后再收到 1~2 帧，属正常。

### 5.15 本地配置镜像（操作端记录）

库把远端配置缓存到**操作端自己的文件**，下次启动直接有记录，不需要重新设置：

```
<exe 目录>\config\pa1664_profile.json
```

里面存三样东西：

| 内容 | 来源 | 用途 |
|---|---|---|
| 底盘配置原文 `drive.iniBase64` + `schema` | 主机 `robot.snapshot` | 展示、回传，避免操作端自己序列化 ini |
| 超声 84 项参数 `ultrasound.params` | 主机 `ultrasound.parameters.snapshot` | 上次的增益/闸门/量程等记录，改参数时同步更新 |
| 超声解码器 `ultrasound.decoder` | 本地偏好 + 远端几何 | 主机不持久化，靠本地记住并自动补发 |

**连上主机后自动执行**：

1. `robot.snapshot` → 缓存底盘配置原文，并把它解析成 `DriveConfig`（可 `applyDriveConfig()` 对齐限幅）；
2. `ultrasound.status` → 设备**就绪**（`hardwareOnline=true`）时，用远端几何
   （pointCount/beamCount/maxAmplitude/group）+ 本地偏好（scanModes/range/selectedBeam）
   自动下发 `ultrasound.decoder.settings`；
3. 设备就绪后自动 `ultrasound.parameters.snapshot` → 缓存 84 项参数；
4. 之后每次 `ultrasoundSetParameter` / `ultrasoundApplyParameters` 成功，用主机回读值更新本地；
5. 设备掉线→恢复（`hardwareOnline` false→true）时，会重新下发一次解码器并补一次参数快照。

> **设备不在线时自动流程会挂起**：超声板卡未就绪时主机侧 SDK 调用会阻塞，
> 库不会发解码器/参数请求（否则只会拿到 `timeout`），因此调用方**不需要**在
> `setUltrasoundStatusHandler` 里自己写 `if (!s.hardwareOnline) return;`。

接口：

| 接口 | 作用 |
|---|---|
| `setProfileOptions({enabled, path, autoApplyDecoder, mirrorRemote, autoSnapshotUltrasound})` | 开关镜像、改文件位置、关自动下发/自动读参数 |
| `profilePath()` | 实际使用的文件路径 |
| `hasDriveProfile()` / `hasUltrasoundProfile()` | 本地是否已有记录 |
| `reloadProfile()` / `saveProfile()` | 手动重读 / 立即落盘 |
| `applyDecoderProfile()` | 用本地保存的解码器配置再下发一次 |
| `applyDriveProfileToHost()` | 把缓存的底盘配置下发给主机（串口/PCAN 端口由主机保留） |

**关于主机端的 `parameter_presets.json` / `default_config.json`：不要随库分发。**
它们是小车主机上厂商 SDK 的硬件配置与预设，和那台车的探头/板卡绑定；操作端只需要上面这个 `pa1664_profile.json`，首次运行会自动生成。

注意：镜像文件里的 `iniBase64` 是主机回传原文，只在 `applyDriveProfileToHost()` 时原样回传，主机侧 `adoptHostPorts()` 会保留它自己的端口，因此不会把操作端机器上的 COM 口号覆盖到主机。

### 5.16 外围设备（激光相机 / IMU / 串口）

激光相机是启动自动纠偏的前置条件（`laser_not_connected` 就是它没连）；
IMU 与编码器一起参与扫查轨迹。

| 接口 | operation | 载荷 |
|---|---|---|
| `scanLaserDevices()` | `robot.laser.scan` | `{}`（结果走 `robot.laser.devices` 事件） |
| `connectLaser(sn)` | `robot.laser.connect` | `{"serialNumber":"DB2666828"}` |
| `disconnectLaser()` | `robot.laser.disconnect` | `{}` |
| `requestLaserStatus()` | `robot.laser.status` | `{}` |
| `connectImu(port, baud, divider=10)` | `robot.imu.connect` | `{"port":"COM3","baudRate":115200,"outputDivider":10}` |
| `disconnectImu()` | `robot.imu.disconnect` | `{}` |
| `requestImuStatus()` | `robot.imu.status` | `{}` |
| `requestPeripheralsSnapshot()` | `robot.peripherals.snapshot` | `{}` |
| `requestSerialPorts()` | `robot.peripherals.serialPorts` | `{}`（结果走响应回调） |

```cpp
robot.scanLaserDevices();          // 事件 robot.laser.devices 里有 serialNumber
robot.connectLaser("DB2666828");
robot.requestLaserStatus();

robot.connectImu("COM3", 115200, 10);
robot.requestImuStatus();
```

注意：

- 激光相机序列号必须与主机上装的那台一致，否则连接失败；
- IMU 分频 `outputDivider` 会被主机夹到 1..200，超范围回 `invalid_argument`；
- 这两个设备只影响纠偏与扫查轨迹，断开不影响底盘运动。

### 5.17 激光轮廓数据（0x0204）

**接口**

```cpp
struct LaserProfileFrame {
  int pointCount = 0;                 // 本帧点数（典型 2048 / 4072）
  const float* coordinates = nullptr; // pointCount 组 (x, y, z)，长度 = pointCount*3
  std::uint32_t frameNumber = 0;      // 激光 SDK 帧号，用来判断丢帧
  std::int64_t receivedAtEpochMs = 0; // 主机收到该帧的时间

  bool valid = false;                 // 本帧焊道检测是否有效
  bool baselineSupported = false;
  double normalizedCenter = 0.5;      // 0..1
  double absoluteCenterRatio = 0.5;   // 0..1
  double confidence = 0.0;            // 0..1
  int gapStartPx = -1;                // -1 表示无效
  int gapEndPx = -1;
  std::string detectionReason;        // 中文原因，如 "located"
};

void RobotApi::setLaserProfileHandler(
    std::function<void(const LaserProfileFrame&)> handler);
```

**数据来源**

```
UDP 43102   kind=10  LaserProfileObservation
            streamId=0x0204
            payload = qCompress(PAD1 type=6, level 4)
              pointCount u32 + pointCount×(x,y,z) f32
              + frameNumber u32 + receivedAtEpochMs i64
              + fieldCount u32(52) + 52 个焊道检测字段
频率 ≈33 ms（30 Hz），主机侧只在“激光相机已连接”时才产生
```

**示例**

```cpp
robot.setLaserProfileHandler([](const pa1664::LaserProfileFrame& frame) {
  if (frame.pointCount < 2) return;
  const float* p = frame.coordinates;
  // 曲线视图取 (x, z)：x 横向、z 高度、y 沿扫描方向
  const float x0 = p[0], z0 = p[2];
  std::printf("点=%d 帧=%u 首点=(%.3f, %.3f) 焊道=%d 置信=%.2f %s\n",
              frame.pointCount, frame.frameNumber, x0, z0, int(frame.valid),
              frame.confidence, frame.detectionReason.c_str());
});

robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.laser.connection") std::printf("%s\n", data.c_str());
});
robot.scanLaserDevices();                       // 事件 robot.laser.devices
robot.connectLaser("DB2666828");                // 序列号必须与主机上的一致
QTimer::singleShot(1500, [&] { robot.requestLaserStatus(); });
```

**注意**

- **前置条件是激光相机已连接**：`robot.laser.connect` 不带序列号会被主机回
  `请先选择相机`（事件 `robot.laser.connection{connected:false,message}`）。先用
  `robot.laser.scan` 拿设备串（格式 `"序列号 | IP | 型号"`），再按串号连接。
- 坐标是**激光 SDK 原始值**，库不做任何单位换算（协议层也不换算）。曲线显示只
  需要 `(x, z)`；做三维展示再取 `y`。
- `coordinates` 指向解码缓存，**仅回调期间有效**；要留存必须自行拷贝。
- 同一路流是“最新值”语义：解码慢于 33 ms 时中间帧直接丢弃，用 `frameNumber`
  跳号判断丢帧，不要用回调次数估算帧率。
- 本库**没有**导出 `0x0202` 预览点云（`LaserPointCloud`）接口；需要预览点云时用
  `request("ultrasound.parameters.snapshot")` 之外的私有通道不可行，只能等库扩展。
- 与 `5.16` 的 `robot.laser.*` 区别：那组只给设备状态与元数据（事件
  `robot.laser.frame` 只有 `pointCount/width/height/sequence`），**不含点集**。
- 设备掉线后主机停止推流，回调自然停止；可用 `robot.laser.connection` 事件做重连。
- 关闭重连退避：`Config` 里没有相关开关；断线期间调用 `connectLaser()` 会被主机
  按 `sensor_service_unavailable` 拒绝，等 `connected=true` 后再用轮廓数据。

---

## 6. 通用请求口（覆盖剩余功能）

```cpp
std::uint64_t request(const std::string& operation,
                      const std::string& dataJson = "{}");
```

已支持但未包装成函数的能力（载荷见主机 `src/backend/*`）：

| operation | 载荷 | 作用 |
|---|---|---|
| `robot.snapshot` | `{}` | 底盘+外围快照（含 settings） |
| `robot.settings.update` | `{"settings":{...}}` | 更新底盘配置（需 schema 匹配） |
| `robot.motion.stop` | `{}` | 主机侧停车 |
| `robot.reset` / `robot.clearAlarm` | `{}` | 复位 / 清报警 |
| `robot.imu.connect` / `disconnect` / `status` | `{}` | IMU |
| `robot.laser.scan` / `connect` / `disconnect` / `status` | `{"serialNumber":"..."}` | 激光轮廓相机 |
| `robot.camera.scan` / `displaySize` / `status` | `{"slot":0,"width":640,"height":480}` | 摄像头 |
| `robot.clamp.settings.get` / `settings.update` | `{"settings":{...}}` | 夹爪配置 |
| `robot.peripherals.snapshot` / `serialPorts` | `{}` | 外围与串口枚举 |
| `ultrasound.presets.list` / `switch` / `capture` / `copy` / `remove` | `{"presetId":N}` | 参数预设 |
| `ultrasound.scan.precision.get` / `set` | 见主机 | 扫查精度 |
| `ultrasound.encoder.calibration.get` / `set` | 见主机 | 编码器标定 |
| `ultrasound.encoder.reset` / `geometry.refresh` | `{}` | 编码器/几何刷新 |

---

## 7. 数据与状态回调一览

| 回调 | 触发 | 关键内容 |
|---|---|---|
| `setConnectionHandler` | 会话状态变化 | `online` + 中文描述 |
| `setStatusHandler` | 遥测 ~20 Hz | `RobotStatus`（state / 速度 / 轮速） |
| `setResponseHandler` | 每个请求应答 | operation / ok / errorCode / dataJson |
| `setEventHandler` | 主机主动上报 | `robot.stateChanged`、`robot.clamp.position`、`robot.log` … |
| `setCameraFrameHandler` | 摄像头帧 | `CameraFrame` 裸像素 |
| `setLaserProfileHandler` | 激光轮廓帧（~33 ms） | `LaserProfileFrame` 单行轮廓点 + 焊道检测子集 |
| `setUltrasoundFrameHandler` | A 扫帧 | `UltrasoundAScanFrame` 波形 |
| `setCorrectionStatusHandler` | `robot.autoCorrection.*` | `CorrectionStatus` |
| `setUltrasoundStatusHandler` | `ultrasound.status` | `UltrasoundStatus` |
| `setLogHandler` | 协议错误 | 中文错误文本 |

遥测字段映射（`driveTelemetryToJson` → `RobotStatus`）：

| JSON key | 字段 | 说明 |
|---|---|---|
| `state` (0..5) | `state` / `stateText` | 0 未连接 / 1 就绪 / 2 检查反馈 / 3 已使能 / 4 故障 / 5 急停 |
| `reason` | `reason` | 状态或故障原因 |
| `commandFresh` / `feedbackFresh` | 同名 | 指令/反馈是否新鲜 |
| `targetLinearMps` / `targetAngularRadps` | 同名 | 目标速度 |
| `appliedLinearMps` / `appliedAngularRadps` | 同名 | 斜坡后的实际值 |
| `left.wheelSpeedMps` / `right.wheelSpeedMps` | `leftWheelMps` / `rightWheelMps` | 轮速反馈 |

---

## 8. 错误码

同步返回：

| `ErrorCode` | 含义 | 处理 |
|---|---|---|
| `None` | 请求已发出（不代表已执行） | 等响应回调 |
| `NotOnline` | 会话未就绪 | 等 `setConnectionHandler(true)` |
| `InvalidArgument` | 参数非法/越界 | 修正参数 |
| `Transport` | 本机发送失败或缓冲满 | 降频重试 |
| `HostRejected` | 主机未声明所需能力 | 升级主机 |

主机异步返回：

| errorCode | 含义 |
|---|---|
| `not_connected` / `request_rejected` / `timeout` | 会话或链路问题（超时 5 s） |
| `invalid_settings` / `invalid_argument` | 配置或参数非法 |
| `unsupported_operation` | 主机不支持 |
| `drive_not_connected` / `drive_not_enabled` | 纠偏前置条件不满足 |
| `laser_not_connected` | 激光相机未连接 |
| `correction_busy` / `correction_not_active` | 纠偏已在跑 / 未在跑 |
| `clamp_service_unavailable` / `camera_service_unavailable` | 后台线程未就绪 |
| `position_unsupported` | 该电机不支持位置控制 |
| `device_connect_failed` / `acquisition_start_failed` | 超声设备或采集失败 |
| `parameter_write_failed` / `parameter_readback_failed` | 超声参数写入/回读失败 |

---

## 9. 线程与生命周期

- 必须在**有 Qt 事件循环的线程**构造、使用、销毁（通常 main）。
- 所有回调都在该线程触发，可直接更新自己的状态机；不要阻塞。
- 底层实时通道由 `RemoteWorkbenchSession` 放到工作线程，调用方无需关心。
- `stop()` 后可再次 `start()`；析构自动 `stop()` 并断开全部回调。
- 同一主机只允许一个 `robot.control.v1` 会话，被占用时事件 `host.busy`。

---

## 10. 安全设计要点

1. **运动是持续指令**：`forward()` 之后必须显式 `stopMotion()`。
2. **进程级失效保护**：API 进程消失 → 50 ms 重发停止 → 主机 200 ms 租约到期
   → `safetyStop(motion_lease_expired)`。
3. **使能前置**：未 `enable` 或未到 `Enabled`，运动指令被忽略。
4. **急停锁存**：不要用 `stopMotion()` 代替急停；急停后必须重新使能。
5. **速度上限**：`Config` 必须与主机 ini 一致（`applyDriveConfig()` 可对齐）。
6. **单一控制权**：第三方接管前确认工作台已停止操作。

---

## 11. 排障

| 现象 | 先看哪里 |
|---|---|
| 一直 `NotOnline` | 主机进程是否在跑；`ping 192.168.1.20`；主机 `app\logs\robot_host.log` |
| `takeControl()` 返回 ok 但停在 `Idle` | 主机 `invalid_settings`（首次未配置底盘），看响应 errorText |
| `Enabled` 但不动 | `RobotStatus::appliedLinearMps`；是否 `EmergencyStop`/`Fault`；`logs\comm\comm_realtime.log` |
| 纠偏起不来 | 是否 `Enabled`、激光相机是否连接（事件 `robot.laser.connection`） |
| 夹爪不动 | 是否先 `robot.clamp.connect`；`robot.clamp.connection` 事件 |
| 摄像头没画面 | `robot.camera.status`；主机侧设备是否被占用；`logs\comm\comm_media.log` |
| A 扫没数据 | 是否配过 `ultrasound.decoder.settings`；`ultrasound.status` 的 `capturing` |
| 参数写完画面卡死 | 是否用了 `ultrasoundParametersSettled()` 而不是直接 `acquisition.start` |
| 中文乱码 | 控制台先 `chcp 65001`；库内字符串一律 UTF-8 |

日志位置（与 Workbench 共用 `AppLogger`）：

- 调用方 exe 同级 `logs\robot_console.log`（`LINK.CONTROL` / `LINK.REALTIME`）
- `logs\comm\comm_control.log`（逐请求明细）
- 小车端 `C:\Users\a\Desktop\robot_host\app\logs\robot_host.log`

---

## 12. 配置同步（远端 ↔ 本地镜像）详解

这一章回答三个问题：**哪些配置归主机、哪些归本机、连上之后库自动同步了什么**。

### 12.1 配置归属

| 配置 | 权威副本在哪 | 谁负责持久化 | 本地镜像缓存？ |
|---|---|---|---|
| 底盘参数（轮径/轮距/限幅/串口/夹爪轴…） | 主机 `robot_host\app\config\drive_settings.ini` | 主机 | ✅ 存 `iniBase64` 原文 + `schema` |
| 超声硬件参数（增益/闸门/量程等 84 项） | 主机 SDK + 主机 `parameter_presets.json` | 主机 | ✅ 存 `values` 全量 |
| 超声解码器配置（组几何） | **主机只放内存，不持久化** | 本库（每次连接后重发） | ✅ 存 `decoder` 偏好 |
| 超声预设 / 扫查精度 / 编码器标定 | 主机 `parameter_presets.json` | 主机 | ❌ 只在响应里返回 |
| 运动限幅（`Config::maxLinearSpeedMps` 等） | 调用方进程内存 | 调用方 | ❌ `applyDriveConfig()` 只改本机 |
| 相机/激光/IMU 的连接选择 | 主机 ini + 调用方业务配置 | 各自 | ❌ |

**关键点**：解码器配置是唯一“主机不存、必须由操作端每次补发”的东西。主机重启后
不重发 `ultrasound.decoder.settings`，A 扫一帧都解不出来，而且**没有任何报错**。

### 12.2 镜像文件长什么样

默认位置 `<exe 目录>\config\pa1664_profile.json`（可用 `ProfileOptions::path` 改）：

```json
{
  "schemaVersion": 1,
  "host": "192.168.1.20",
  "savedAt": "2026-11-13T17:20:31",
  "drive": {
    "schema": 13,
    "iniBase64": "W2RyaXZlXQ0Kc2V0dGluZ3NTY2hlbWFWZXJzaW9uPTEzDQpzZXJpYWxQb3J0PUNPTTYN..."
  },
  "ultrasound": {
    "params": {
      "getGain:WzBd": 20,
      "getRangeStart:WzBd": 0.0,
      "getRangeEnd:WzBd": 100.0,
      "getPointQuantity:WzBd": 1000,
      "...": "共 84 项，键 = 方法名:base64(args)"
    },
    "decoder": {
      "groupId": 0, "beamCount": 1, "pointCount": 1000,
      "amplitudeScale": 2048, "selectedBeam": 0, "scanModes": 1,
      "rangeStart": 0.0, "rangeEnd": 50.0
    }
  }
}
```

`params` 的键是 `parameterCacheKey(method, args)` = `方法名 + ':' + base64(JSON 参数)`：
`getGain` + `[0]` → `getGain:WzBd`（`WzBd` 就是 `"[0]"` 的 Base64）。所以取增益要拼
`getGain` 与 `groupArgs(groupId)`，不能只用方法名。

### 12.3 连上主机后库自动做什么（时序）

```
握手成功
  │
  ├─(库) robot.snapshot ─────────────► 回包 settings{schema,iniBase64}
  │                                     ├─ mirrorDriveSettings() 写盘（内容有变化才写）
  │                                     └─ setDriveConfigHandler() 回调 DriveConfig
  │
  ├─(主机) 事件 ultrasound.status（1000 ms 周期，不等请求）
  │        └─ 板卡 hardwareOnline=true 时：
  │             ├─ maybeAutoApplyDecoder()  每会话一次
  │             │    远端几何(pointCount/beamCount/amplitudeScale/currentGroup)
  │             │    + 本地偏好(scanModes/selectedBeam/rangeStart/rangeEnd)
  │             │    → ultrasound.decoder.settings
  │             └─ maybeAutoSnapshotUltrasound()  每会话一次，且要求 !capturing
  │                  → ultrasound.parameters.snapshot
  │                  → values 写盘（mirrorUltrasoundParameters）
  │
  └─ 设备掉线→恢复（hardwareOnline false→true）
        两个“每会话一次”的标记复位，上面两步重新执行
```

要点：

- `autoApplyDecoder` / `autoSnapshotUltrasound` **不受 `ProfileOptions::enabled` 影响**。
  `enabled=false` 只是“不读盘不写盘”，自动下发仍然生效。
- 采集进行中**不会**自动拉全量参数（84 次 SDK 读会顶住 USB 接收，把超声堵成“假掉线”）。
  需要参数先停采，或自己调 `ultrasoundParameterSnapshot()`。
- 一次 `parameters.snapshot` 要跑 84 次 SDK 读，别在高频循环里调。

### 12.4 上行（从主机读回本地）

| 接口 | 实际 operation | 结果去向 |
|---|---|---|
| `requestDriveConfig()` / `requestSnapshot()` | `robot.snapshot` | `setDriveConfigHandler` + **写镜像** |
| `requestUltrasoundStatus()` | `ultrasound.status` | `setUltrasoundStatusHandler`，不写盘 |
| `ultrasoundParameterSnapshot()` | `ultrasound.parameters.snapshot` | 响应 `values` + **写镜像** |
| `ultrasoundGetParameter(m,args)` | `ultrasound.parameters.getMany` | 只走响应，**不写盘** |
| `requestUltrasoundPresets()` | `ultrasound.presets.list` | 响应 `presetState`，不写盘 |
| `requestUltrasoundScanPrecision()` | `ultrasound.scan.precision.get` | 响应，不写盘 |
| `requestUltrasoundEncoderCalibration()` | `ultrasound.encoder.calibration.get` | 响应，不写盘 |

**坑**：`robot.connect` 的应答里也有 `settings`，但库**只在 `robot.snapshot` 时**写镜像。
所以用 `applyDriveProfileToHost()` 或 `robot.settings.update` 改过主机参数后，想刷新
镜像必须再调一次 `requestDriveConfig()`（或手动 `saveProfile()` 保留当前内存值）。

### 12.5 下行（把本地配置推给主机）

| 接口 | 实际 operation | 会不会改主机持久化 |
|---|---|---|
| `applyDriveProfileToHost()` | `robot.connect {settings{schema,iniBase64}}` | 会（主机写入自己的 ini）；**串口/PCAN 端口由主机保留** |
| `request("robot.settings.update",{...})` | `robot.settings.update` | 会；需要 `schema`+`iniBase64`，空对象判版本不符 |
| `configureUltrasoundDecoder(cfg)` | `ultrasound.decoder.settings` | ❌ 只改主机内存 |
| `applyDecoderProfile()` | 同上，用镜像里的 `decoder` | ❌ 只改主机内存 |
| `ultrasoundSetParameter(...)` | `ultrasound.parameter.set` | 会改硬件；回读值**写镜像** |
| `ultrasoundApplyParameters(json)` | `ultrasound.parameters.apply` | 同上（`readbacks` 写镜像） |
| `switchUltrasoundPreset(id)` / `captureUltrasoundPreset(id)` | `ultrasound.presets.*` | 会（主机 `parameter_presets.json`） |
| `setUltrasoundScanPrecision()` / `setUltrasoundEncoderCalibration()` | `scan.precision.set` / `encoder.calibration.set` | 会 |
| `applyDriveConfig(drive)` | **不发网络** | ❌ 只改本机 `Config` 限幅 |

底盘配置下发为什么不覆盖主机端口：主机侧 `adoptHostPorts()` 会用自己 ini 里的
`serialPort` / CAN 端口覆盖回传内容，所以把 A 机器人的 ini 推给 B 车不会把 B 车的
COM 口号改坏——但**其余参数会按 A 车覆盖**，换车前请注意。

### 12.6 常见操作组合

```cpp
// 1) 让主机与新操作端对齐（把本机记录的底盘配置推上去）
robot.applyDriveProfileToHost();
robot.requestDriveConfig();          // 刷新镜像（connect 应答不会自动写盘）

// 2) 参数改完让主机恢复采集
robot.ultrasoundApplyParameters(R"([
  {"method":"setGain","args":[40,0],"readbackMethod":"getGain","readbackArgs":[0]}
])");
robot.ultrasoundParametersSettled(true);   // 停采→重配→恢复，别自己调 acquisition.start

// 3) 临时关掉镜像（多台设备/换车联调，避免旧配置污染）
pa1664::ProfileOptions options;
options.enabled = false;             // 不读盘不写盘；自动下发仍生效
options.autoApplyDecoder = false;    // 想自己完全掌控解码器时再关这个
options.autoSnapshotUltrasound = false;
robot.setProfileOptions(options);
std::printf("镜像位置（即使未启用）: %s\n", robot.profilePath().c_str());

// 4) 换回带镜像的模式并重载
robot.setProfileOptions(pa1664::ProfileOptions{});
robot.reloadProfile();               // 失败时返回 InvalidArgument（文件不存在/解析失败）
```

### 12.7 排障对照

| 现象 | 原因 | 处理 |
|---|---|---|
| 镜像文件一直不生成 | `ProfileOptions::enabled=false`，或 exe 目录不可写 | 打开 enabled；`profilePath()` 看实际路径 |
| 改了参数镜像没变 | 用的是 `getMany`（不写盘）或主机没回 `readbackKey` | 用 `parameter.set`/`parameters.apply`，或重新 `snapshot` |
| 换车后底盘参数被覆盖 | 镜像里是上一台车的 `iniBase64` | 先 `requestDriveConfig()` 覆盖镜像，或临时 `enabled=false` |
| 主机重启后 A 扫没数据 | 解码器配置丢失且没重发 | 检查 `autoApplyDecoder` 是否为 false；手动 `applyDecoderProfile()` |
| 采集时参数读取超时 | 84 次读与 USB 接收抢锁 | 先 `ultrasoundStopAcquisition()` 再 snapshot |
| 推配置后主机没反应 | `schema != 13` 或 ini > 512 KiB | 用主机回传的原文，不要手工拼 |
| 两个操作端看到的配置不一致 | 各自本地镜像独立 | 后连的那台先 `requestDriveConfig()` / `reloadProfile()` |

### 12.8 一句话规则

- **主机是权威**：连上就读，读到的就写进镜像；
- **解码器是例外**：主机不存，库每次会话补发一次；
- **写参数必须回读**，写完用 `settled` 恢复采集；
- **`enabled=false` 只关镜像，不关自动下发**。
