# PA1664 小车 API 使用手册

这份手册按"我想做什么"来组织，不按代码结构组织。
每一节都是：一句说明 + 可直接抄的代码 + 怎么判断成功 + 常见坑。

底层指令（`robot.connect` 这类名字）全部收在附录 B，日常使用不需要看。

---

# 目录

- [先看这个](#先看这个)
- [1. 连接小车](#1-连接小车)
- [2. 让车动起来](#2-让车动起来)
- [3. 夹爪：X1 / X2 / Z1 / Z2](#3-夹爪x1--x2--z1--z2)
- [4. 摄像头](#4-摄像头)
- [5. 超声](#5-超声)
- [6. 自动纠偏](#6-自动纠偏)
- [7. 配置：不用每次重新设置](#7-配置不用每次重新设置)
- [8. 激光相机与 IMU](#8-激光相机与-imu)
- [9. 出错了怎么看](#9-出错了怎么看)
- [附录 A. 接口总表](#附录-a-接口总表)
- [附录 B. 底层指令表（排障用）](#附录-b-底层指令表排障用)
- [附录 C. 事件表](#附录-c-事件表)

---

# 先看这个

## 这个库能干什么

一句话：**把小车上位机的通信协议包成一个 C++ 类，你调函数，它帮你发指令。**

```
你的程序 → pa1664::RobotApi → 网络（TCP/UDP）→ 小车端 PA1664RobotHost → 硬件
```

你不需要知道端口、帧格式、重发、心跳、图像解码，只需要：

```cpp
pa1664::RobotApi robot;      // 连 192.168.1.20
robot.start();               // 连上
robot.takeControl();         // 拿控制权（使能）
robot.forward(0.5);          // 半速前进
robot.stopMotion();          // 停车
```

## 5 分钟跑起来

新建一个控制台工程，链接 `小车api\static\PA1664RobotApi.lib`（静态方式不用定义宏），
把下面整段贴进去就能编：

```cpp
#include "pa1664_robot_api.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  pa1664::RobotApi robot;                       // 默认 192.168.1.20:43100

  robot.setConnectionHandler([&](bool online, const std::string& info) {
    std::printf("%s：%s\n", online ? "已连上" : "已断开", info.c_str());
    if (online) robot.takeControl();            // 连上后自动取控制权
  });

  robot.setStatusHandler([](const pa1664::RobotStatus& s) {
    std::printf("状态=%s 实际速度=%.3f m/s\n", s.stateText.c_str(),
                s.appliedLinearMps);
  });

  robot.start();
  QTimer::singleShot(3000, &app, [&] { robot.forward(0.5); });
  QTimer::singleShot(5000, &app, [&] { robot.stopMotion(); robot.stop(); app.quit(); });
  return app.exec();
}
```

编译要点：

| 项 | 值 |
|---|---|
| 头文件路径 | `小车api\include` |
| 库 | `小车api\static\PA1664RobotApi.lib` |
| Qt 模块 | `core network gui` |
| 运行环境 | MSVC 2022 x64、Qt 6.8.3、C++17、`/MD` |

> 用动态库时：链接 `小车api\shared\PA1664RobotApi.lib`，工程里定义
> `PA1664_ROBOT_API_DLL`，exe 旁边放 `PA1664RobotApi.dll` + Qt 的 Core/Network/Gui 三个 DLL。

## 几个必须知道的名词

| 名词 | 人话解释 |
|---|---|
| **主机 / Host** | 跑在小车那台电脑上的 `PA1664RobotHost.exe`，所有硬件都接在它上面 |
| **使能** | 给轮子电机通电并保持。没使能时，速度指令会被直接忽略（不报错） |
| **租约（200 ms）** | 你每 50 ms 会被自动重发一次速度指令；超过 200 ms 没有新指令，主机自己停车。所以你的程序崩了，车也会停 |
| **A 扫** | 超声的一条深度波形：1000 个采样点，每个点对应一个深度 |
| **解码器配置** | 告诉主机"超声数据有多少点、几条波束"，配错或没配就收不到 A 扫，且不报错 |
| **预设** | 主机上保存的一整套超声参数模板，掉电保留 |
| **控制权** | 同一时刻只允许一个客户端连主机；被占用时后来的会收到 `host.busy` |

## 三条最容易踩的规则

1. **运动是持续指令**：`forward()` 之后不会自己停，必须 `stopMotion()`。
2. **没使能一切白搭**：`takeControl()` 或 `enable(true)` 之后，等状态变成"已使能"再发速度。
3. **只能有一个客户端**：你的程序和工作台不要同时连。

---

# 1. 连接小车

## 1.1 最小连接

```cpp
pa1664::RobotApi robot;                 // 默认地址
// 或指定： pa1664::Config cfg; cfg.host = "192.168.1.21"; pa1664::RobotApi robot(cfg);

robot.setConnectionHandler([](bool online, const std::string& info) {
  std::printf("%s %s\n", online ? "ONLINE" : "OFFLINE", info.c_str());
});

robot.start();      // 立即返回，后台连接，断线会自动重连
```

## 1.2 怎么判断"真的连上了"

`start()` 返回不代表连上。正确的判断只有一处：

```cpp
robot.setConnectionHandler([&](bool online, const std::string&) {
  if (online) {
    // 到这里才是可以发指令的时候
  }
});
```

也可以随时查：

```cpp
robot.isOnline();      // 控制通道是否就绪
robot.mediaReady();    // 图像/扫描数据通道是否就绪
robot.bulkReady();     // 大批量扫描数据通道是否就绪
robot.statusText();    // 最近一次状态描述，中文，可直接打日志
```

## 1.3 断开与退出

```cpp
robot.stopMotion();    // 先停车
robot.stop();          // 再断链
```

对象析构时会自动 `stop()`，所以哪怕忘了写也不会把车留在运动状态（还有 200 ms 租约兜底）。

## 常见坑

- **一直连不上**：先确认小车端 `PA1664RobotHost.exe` 在跑，再 `ping 192.168.1.20`。
- **报 `host.busy`**：工作台或另一个程序占着，关掉再连。
- **网络断了又恢复**：库会自动重连，你的回调会再收到一次 `online=true`，注意不要重复初始化（用一个标志位）。

---

# 2. 让车动起来

## 2.1 先拿控制权（使能）

```cpp
robot.takeControl();     // 一步搞定：连接底盘 + 使能
```

`takeControl()` 内部做了两件事：先通知主机连接底盘，等主机回复后 1.2 秒再使能
（主机要检查电机反馈，这个时间不能省）。

想自己控制时序：

```cpp
robot.connectChassis();
// 等事件 robot.stateChanged 里 state 变成 "enabled" 再发速度
robot.enable(true);
```

**怎么判断使能成功**：`RobotStatus::state == DriveState::Enabled`，
`stateText` 会显示"已使能"。

## 2.2 前进 / 后退 / 转弯

```cpp
robot.forward(0.5);      // 半速前进（0.5 × 0.12 m/s = 0.06 m/s）
robot.backward(0.3);     // 三成速度后退
robot.turnLeft(0.5);     // 原地左转
robot.turnRight(0.5);    // 原地右转

robot.stopMotion();      // 停车（保持使能，可继续发指令）
```

参数是**比例**，不是米/秒：`1.0` = 配置里的最大速度（默认 0.12 m/s、50 deg/s）。

要精确速度：

```cpp
robot.moveVelocity(0.05, -0.2);   // 0.05 m/s 前进 + 左转 0.2 rad/s
robot.moveVelocity(0.05, 0.2);    // 右转（正角速度 = 右转）
```

**怎么判断真的动了**：

```cpp
robot.setStatusHandler([](const pa1664::RobotStatus& s) {
  std::printf("state=%s 目标=%.3f 实际=%.3f 左轮=%.3f 右轮=%.3f\n",
              s.stateText.c_str(), s.targetLinearMps, s.appliedLinearMps,
              s.leftWheelMps, s.rightWheelMps);
});
```

`applied` 会跟着你的指令爬坡上来；如果一直是 0，看下面的清单。

## 2.3 车不动？按这个顺序查

1. `state` 是 `Enabled` 吗？（不是就先 `takeControl()`）
2. `reason` 里写了什么？（"安全故障：轮子电机库未连接"= 主机侧 CAN/串口没打开）
3. `appliedLinearMps` 有变化吗？（没变化说明指令没被接受）
4. 主机是不是在急停状态？（`EmergencyStop`）
5. 有没有别的客户端占着控制权？（`host.busy`）

## 2.4 停车与急停

```cpp
robot.stopMotion();        // 普通停车：零速指令，仍保持使能
robot.emergencyStop();     // 急停：锁存，同时停夹爪，恢复要 reset + enable
```

**不要用 `stopMotion()` 代替急停。** 急停是双通道（UDP 锁存包 + TCP 请求），
即使控制连接断了也能生效。

急停之后恢复：

```cpp
robot.reset();             // 清故障（有故障时）
robot.enable(true);        // 重新使能
```

## 常见坑

- 前进/后退不会自己停，忘了 `stopMotion()` 会一直走；
- 未使能时发速度不报错，只是没反应，别以为是库坏了；
- 直线中加减速由主机斜坡处理，别指望瞬间反向；
- 速度上限和主机 ini 不一致时以主机为准，看 `applied`。

---

# 3. 夹爪：X1 / X2 / Z1 / Z2

## 3.1 先连接总线

```cpp
robot.clampConnect();          // 用主机已保存的夹爪配置去连
```

**怎么判断连上**：

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.connection") std::printf("%s\n", data.c_str());
});
```

看到 `connected:true` 才能调速。

## 3.2 四个电机的对应关系

| 你看到的 | 调用 | 含义 |
|---|---|---|
| X1 | `robot.x1Motor(40)` | X 轴第 1 台，正转 40% |
| X2 | `robot.x2Motor(-40)` | X 轴第 2 台，反转 40% |
| Z1 | `robot.z1Motor(25)` | Z 轴第 1 台 |
| Z2 | `robot.z2Motor(-25)` | Z 轴第 2 台 |

参数是**额定转速的百分比**（-100 到 100），正负方向由主机 ini 里该电机的
`sign` 决定。Y 轴如果有电机，用 `robot.clampSetSpeed(1, 0, 40)`（axis=1）。

## 3.3 停

```cpp
robot.clampStopMotor(0, 0);   // 停 X1 这一台
robot.clampStop();            // 全部停（推荐，尤其是异常退出前）
```

## 3.4 走到指定角度（部分电机支持）

```cpp
robot.clampMoveToPosition(2, 0, 120.0);   // Z1 走到 120 度

robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.position") std::printf("位置：%s\n", data.c_str());
});
robot.clampRequestPositions();            // 主动查全部位置
```

不支持位置控制的固件会返回 `position_unsupported`，改用调速 + 限位。

## 常见坑

- 没 `clampConnect()` 就调速 → `clamp_service_unavailable`；
- 百分比不是 mm/s，实际位移与 ini 里的 `gearRatio` 有关；
- 软限位在主机侧生效，撞限位会收到 `robot.clamp.limitReached` 并自动停；
- 断开夹爪（`clampDisconnect()`）会先停全部电机，属于正常现象。

---

# 4. 摄像头

## 4.1 先看看有哪些摄像头

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.camera.devices") std::printf("设备：%s\n", data.c_str());
});
robot.scanCameras();
```

## 4.2 连上并取图（完整示例：按帧存 png）

```cpp
#include <QImage>
#include <QDateTime>

robot.setCameraFrameHandler([](const pa1664::CameraFrame& f) {
  // 注意：f.data 只在回调里有效，要保存必须 copy
  QImage::Format fmt = (f.format == pa1664::PixelFormat::Grayscale8)
                           ? QImage::Format_Grayscale8
                           : QImage::Format_RGB888;
  QImage img(f.data, f.width, f.height, f.stride, fmt);
  img.copy().save(QStringLiteral("cam%1_%2.png")
                      .arg(f.slot + 1)
                      .arg(QDateTime::currentMSecsSinceEpoch()));
});

robot.scanCameras();                                  // 拿到 deviceIndex
robot.connectCamera(0, 0, 15);                        // 摄像头1：USB 设备 0，15 fps
robot.connectNetworkCamera(1, "rtsp://192.168.1.104:554/stream1");  // 摄像头2：网络流
```

`slot`：`0` = 摄像头1，`1` = 摄像头2。

## 4.3 改分辨率

```cpp
robot.setCameraDisplaySize(0, 640, 480);   // 让主机按 640x480 下发
```

## 常见坑

- USB 摄像头必须用 `scanCameras()` 得到的 `deviceIndex`，不能猜；
- 回调里不要做耗时操作（15~30 fps，会拖慢整条链路）；
- 图像字节序是 B,G,R,A，OpenCV 直接 `CV_8UC4`；
- 断线会发 `robot.camera.connection` 事件，可据此重连。

---

# 5. 超声

## 5.1 先看设备状态

```cpp
robot.setUltrasoundStatusHandler([](const pa1664::UltrasoundStatus& s) {
  std::printf("服务=%d 硬件=%d 采集中=%d 点数=%d 波束=%d 最大幅值=%d\n",
              s.serviceConnected, s.hardwareOnline, s.capturing, s.pointCount,
              s.beamCount, s.maxAmplitudeValue);
});
robot.requestUltrasoundStatus();
```

三个字段的含义：

| 字段 | 含义 | 什么时候为 false |
|---|---|---|
| `serviceConnected` | SDK 服务起来了 | 主机刚启动的几秒 |
| `hardwareOnline` | USB 板卡在线 | 线没插好、板卡没上电 |
| `capturing` | 正在采集 | 主动停采，或设备掉了 |

**只有 `hardwareOnline=true` 时才能读参数、配解码器、收 A 扫。**

## 5.2 收 A 扫（完整示例：打印波形峰值）

```cpp
robot.setUltrasoundFrameHandler([](const pa1664::UltrasoundAScanFrame& f) {
  if (f.beamCount < 1 || f.pointCount <= 0) return;
  const std::int16_t* beam = f.beams[0];          // 第 1 条波束
  int peakIndex = 0, peak = 0;
  for (int i = 0; i < f.pointCount; ++i) {
    if (beam[i] > peak) { peak = beam[i]; peakIndex = i; }
  }
  const double depthMm = f.rangeStartMm +
      (f.rangeEndMm - f.rangeStartMm) * peakIndex / double(f.pointCount - 1);
  std::printf("帧 %u 峰值 %d 出现在 %.1f mm\n", f.frameNumber, peak, depthMm);
});

robot.ultrasoundStartAcquisition();
```

**解码器不用你配**：库收到设备状态、且设备在线时，会自动按真实几何
（1000 点、1 条波束、幅值 2048）下发一次解码配置。

如果 `f.measurements` 不为空，还能拿到闸门 A/B/C/I 的幅值和位置：

```cpp
if (f.measurementCount > 0) {
  std::printf("闸门A 幅值=%u 位置=%u\n",
              f.measurements[0].amplitudeA, f.measurements[0].positionA);
}
```

## 5.3 读参数 / 改参数

参数一共 84 项，用 SDK 的方法名访问：`getGain` 读、`setGain` 写。

```cpp
// 读一个
robot.ultrasoundGetParameter("getGain", "[0]");
// 响应里： {"values":{"getGain:WzBd":13}}

// 写一个（会自动回读校验，回读方法可省略）
robot.ultrasoundSetParameter("setGain", "[40,0]");
// 响应里： {"readbackKey":"getGain:WzBd","readbackValue":40}

// 一次读全部（84 项）
robot.ultrasoundParameterSnapshot();
```

参数里的 `[40,0]` = `[值, 组号]`，组号一般填 `0`。

**改完参数一定要恢复采集**：

```cpp
robot.ultrasoundParametersSettled(true);   // 主机停采 → 写参数 → 自动恢复采集
```

批量改（一次事务，少几次硬件重配）：

```cpp
robot.ultrasoundApplyParameters(R"([
  {"method":"setGain","args":[40,0]},
  {"method":"setRangeEnd","args":[60.0,0]}
])");
robot.ultrasoundParametersSettled(true);
```

## 5.4 预设（一整套参数，掉电保留）

```cpp
robot.requestUltrasoundPresets();      // 列出预设
robot.captureUltrasoundPreset(0);      // 把当前参数存成预设 0
robot.switchUltrasoundPreset(0);       // 切回预设 0（会重配硬件）
```

## 5.5 扫查精度与编码器标定

```cpp
robot.setUltrasoundScanPrecision(0, 5.0);   // 5 mm/格，按预设保存

robot.requestUltrasoundEncoderCalibration();          // 读
robot.setUltrasoundEncoderCalibration(R"({
  "wheelDiameterMm":500, "resolution":4096, "calibrationMode":0,
  "countsPerMmA":2.6075945876176134, "countsPerMmB":2.6075945876176134
})");
```

## 常见坑

- **`hardwareOnline=false` 时别读参数**：SDK 会卡住，表现为请求 5 秒超时；
- 采集中做"全量读 84 项"会和 USB 数据抢资源，建议先停采再读；
- 参数写完后由主机预设文件（`default_config.json`）在下次启动时覆盖，
  想长期保留要 `captureUltrasoundPreset()`；
- 主动 `ultrasoundStopAcquisition()` 后再次启动，主机要重新初始化，约 30 秒才恢复；
- 普通连接/断开**不会**停采集，放心长连接。

---

# 6. 自动纠偏

## 6.1 三个前置条件

缺一不可，缺了主机会直接拒绝并说明原因：

1. 底盘已连接（`connectChassis()`）；
2. 底盘已使能（`state == Enabled`）；
3. 激光相机已连接（`connectLaser(序列号)`）。

```cpp
robot.connectLaser("DB2666828");    // 序列号要与主机上装的那台一致
robot.takeControl();
```

## 6.2 启动 / 暂停 / 停止

```cpp
robot.setCorrectionStatusHandler([](const pa1664::CorrectionStatus& s) {
  std::printf("纠偏 active=%d paused=%d 阶段=%s 速度=%.4f\n", s.active,
              s.paused, s.phase.c_str(), s.linearCommandMps);
});

robot.startAutoCorrection(R"({"targetSpeedMps":0.008})");   // {} 表示全用默认
// ...
robot.pauseAutoCorrection(true);    // 暂停
robot.pauseAutoCorrection(false);   // 继续
robot.stopAutoCorrection();         // 停止
robot.stopMotion();                 // 停止后务必跟一句停车
```

## 6.3 可调参数（常用的几个）

| 键 | 默认 | 说明 |
|---|---|---|
| `targetSpeedMps` | 0.005 | 纠偏时的前进速度 |
| `segmentLengthM` | 0.03 | 每次拟合多长一段焊缝（米） |
| `proportionalGain` | 3.0 | 横向偏差的纠正力度 |
| `derivativeGain` | 0.12 | 抑制抖动 |
| `imageTimeoutMs` | 500 | 激光帧超时 |

## 常见坑

- 启动返回 `drive_not_enabled` / `laser_not_connected`，先补前置条件；
- 应答"开始准备"不等于在跑，看事件 `robot.autoCorrection.started`；
- 纠偏运行时会接管运动通道，此时手动 `forward()` 无效；
- 停止后车不会自动停，要自己 `stopMotion()`。

---

# 7. 配置：不用每次重新设置

## 7.1 库帮你记住什么

连上主机后，库会自动做三件事（都在后台，不需要你调）：

1. 读一次底盘配置，存进 `<你的exe>\config\pa1664_profile.json`；
2. 设备在线时，自动配好超声解码器（所以 A 扫开箱可用）；
3. 设备在线且**没在采集**时，把 84 项超声参数也存一份。

下次启动，即使主机一时连不上，你也能从本地文件看到上次的配置。

## 7.2 想让限幅和主机一致

```cpp
robot.setDriveConfigHandler([](const pa1664::DriveConfig& cfg) {
  robot.applyDriveConfig(cfg);   // 用主机的最大速度/角速度覆盖本地默认值
});
robot.requestDriveConfig();
```

## 7.3 想知道本地记了什么

```cpp
std::printf("文件：%s\n  底盘=%d  超声=%d\n", robot.profilePath().c_str(),
            robot.hasDriveProfile(), robot.hasUltrasoundProfile());
```

## 7.4 不想留记录

```cpp
pa1664::ProfileOptions opts;
opts.enabled = false;
robot.setProfileOptions(opts);
```

## 常见坑

- 这个文件里有主机的串口、PCAN 通道、RTSP 地址，**不要外发或提交到仓库**；
- 删掉它不会影响功能，下次连接会重新生成；
- 它只是"操作端的记录"，主机的真实配置永远在主机自己的 `drive_settings.ini` 里。

---

# 8. 激光相机与 IMU

```cpp
robot.scanLaserDevices();               // 列出可用的激光相机
robot.connectLaser("DB2666828");
robot.setLaserProfileHandler([](const pa1664::LaserProfileFrame& f) {
  std::printf("轮廓 %d 点，有效=%d，置信度=%.2f\n", f.pointCount, f.valid,
              f.confidence);
});

robot.connectImu("COM3", 115200, 10);   // 串口, 波特率, 分频
robot.requestImuStatus();
robot.requestSerialPorts();             // 主机有哪些串口
```

- 激光相机的 `coordinates` 是 `pointCount` 组 `(x, y, z)`，只在回调里有效；
- 这两个设备只影响纠偏与扫查轨迹，不影响底盘运动。

---

# 9. 出错了怎么看

## 9.1 三类错误，分开看

| 类型 | 你怎么知道 | 典型原因 |
|---|---|---|
| 调用直接返回失败 | `Result r = robot.forward(0.5); r.ok == false` | 没连上、参数越界 |
| 指令发出但主机拒绝 | 响应回调里 `ok == false` | 前置条件不满足、参数不对 |
| 指令被接受但没效果 | 一切正常，就是不动 | 没使能、急停、限位、别的客户端在控制 |

第一类看 `r.message`（中文，直接打日志）。
第二类：

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string& code, const std::string& text,
                            const std::string&) {
  if (!ok) std::printf("失败 %s [%s] %s\n", op.c_str(), code.c_str(), text.c_str());
});
```

## 9.2 常遇到的错误码

| 错误码 | 人话 | 怎么办 |
|---|---|---|
| `not_connected` | 链路断了 | 等自动重连 |
| `timeout` | 主机 5 秒没回 | 重试；若总在超声参数上，先停采 |
| `host.busy` | 有别人占着 | 关掉工作台或另一个客户端 |
| `invalid_argument` | 参数不对 | 看 `text` 里的范围说明 |
| `drive_not_enabled` | 没使能 | `takeControl()` |
| `laser_not_connected` | 激光相机没连 | `connectLaser()` |
| `clamp_service_unavailable` | 夹爪没连 | `clampConnect()` |
| `position_unsupported` | 电机不支持定位 | 改用调速 |
| `parameter_write_failed` | 超声参数值非法 | 查参数范围 |

## 9.3 日志在哪

| 位置 | 内容 |
|---|---|
| `<你的exe>\logs\robot_console.log` | 链路、协议错误 |
| `<你的exe>\logs\comm\comm_control.log` | 逐条请求明细 |
| 小车端 `robot_host\app\logs\robot_host.log` | 主机侧硬件与采集 |

---

# 附录 A. 接口总表

## 会话

| 接口 | 说明 |
|---|---|
| `RobotApi(Config)` | 构造，不连接 |
| `start()` / `stop()` | 建链 / 断链 |
| `isOnline()` / `mediaReady()` / `bulkReady()` | 通道状态 |
| `statusText()` | 最近状态描述 |
| `config()` | 当前配置 |

## 底盘

| 接口 | 说明 |
|---|---|
| `takeControl()` | 连接 + 使能（推荐） |
| `connectChassis()` / `disconnectChassis()` | 单独连接/断开底盘 |
| `enable(bool)` | 使能 / 撤销 |
| `emergencyStop()` | 急停（锁存） |
| `reset()` / `clearAlarm()` | 清故障 / 清报警 |
| `requestSnapshot()` | 查快照 |
| `requestDriveConfig()` | 查配置（结果在回调） |
| `applyDriveConfig(DriveConfig)` | 用主机限幅对齐本地 |

## 运动

| 接口 | 说明 |
|---|---|
| `forward(r)` / `backward(r)` | 前进 / 后退，r ∈ [0,1] |
| `turnLeft(r)` / `turnRight(r)` | 原地转 |
| `moveVelocity(v, w)` | 精确速度（m/s、rad/s） |
| `stopMotion()` | 停车（保持使能） |
| `stopMotionOnHost()` | 额外让主机侧清零 |

## 纠偏

| 接口 | 说明 |
|---|---|
| `startAutoCorrection(settingsJson)` | 启动 |
| `pauseAutoCorrection(bool)` | 暂停 / 继续 |
| `stopAutoCorrection()` | 停止 |
| `requestAutoCorrectionStatus()` | 查状态 |

## 夹爪

| 接口 | 说明 |
|---|---|
| `clampConnect()` / `clampDisconnect()` | 连接 / 断开总线 |
| `clampSetSpeed(axis, index, percent)` | 调速 |
| `x1Motor(p)` `x2Motor(p)` `z1Motor(p)` `z2Motor(p)` | 四个电机的快捷方式 |
| `clampStopMotor(axis, index)` / `clampStop()` | 停单台 / 全停 |
| `clampMoveToPosition(axis, index, deg)` | 定位 |
| `clampRequestPosition()` / `clampRequestPositions()` | 查位置 |
| `requestClampStatus()` / `requestClampSettings()` | 查状态 / 查配置 |

## 摄像头

| 接口 | 说明 |
|---|---|
| `scanCameras()` | 枚举 USB 摄像头 |
| `connectCamera(slot, deviceIndex, fps, flipH, flipV)` | 连 USB |
| `connectNetworkCamera(slot, url, flipH, flipV)` | 连网络流 |
| `setCameraDisplaySize(slot, w, h)` | 改下行分辨率 |
| `requestCameraStatus()` | 查状态 |
| `disconnectCamera(slot)` / `disconnectAllCameras()` | 断开 |

## 超声

| 接口 | 说明 |
|---|---|
| `requestUltrasoundStatus()` | 查设备状态 |
| `ultrasoundStartAcquisition()` / `ultrasoundStopAcquisition()` | 开始 / 停止采集 |
| `ultrasoundParameterSnapshot()` | 读全部参数 |
| `ultrasoundGetParameter(method, args)` | 读一个 |
| `ultrasoundSetParameter(setMethod, args, ...)` | 写一个（带回读） |
| `ultrasoundApplyParameters(changesJson)` | 批量写 |
| `ultrasoundParametersSettled(resume)` | 写完后恢复采集 |
| `configureUltrasoundDecoder(cfg)` | 手动配解码器（一般不用） |
| `requestUltrasoundPresets()` / `switchUltrasoundPreset()` / `captureUltrasoundPreset()` | 预设 |
| `requestUltrasoundScanPrecision()` / `setUltrasoundScanPrecision()` | 扫查精度 |
| `requestUltrasoundEncoderCalibration()` / `setUltrasoundEncoderCalibration()` | 编码器标定 |
| `ultrasoundResetEncoder()` / `ultrasoundRefreshGeometry()` | 复位 / 刷新几何 |

## 外围

| 接口 | 说明 |
|---|---|
| `scanLaserDevices()` / `connectLaser(sn)` / `disconnectLaser()` / `requestLaserStatus()` | 激光相机 |
| `connectImu(port, baud, divider)` / `disconnectImu()` / `requestImuStatus()` | IMU |
| `requestPeripheralsSnapshot()` / `requestSerialPorts()` | 外围快照 / 串口列表 |

## 配置镜像

| 接口 | 说明 |
|---|---|
| `setProfileOptions(ProfileOptions)` / `profileOptions()` | 开关与路径 |
| `profilePath()` / `hasDriveProfile()` / `hasUltrasoundProfile()` | 查记录 |
| `reloadProfile()` / `saveProfile()` | 手动读写 |
| `applyDecoderProfile()` | 重发解码器 |
| `applyDriveProfileToHost()` | 把缓存配置推回主机 |

## 其他

| 接口 | 说明 |
|---|---|
| `request(operation, json)` | 通用出口，见附录 B |
| `setConnectionHandler` / `setStatusHandler` / `setResponseHandler` / `setEventHandler` | 基础回调 |
| `setDriveConfigHandler` / `setCameraFrameHandler` / `setLaserProfileHandler` / `setUltrasoundFrameHandler` / `setCorrectionStatusHandler` / `setUltrasoundStatusHandler` / `setLogHandler` | 数据回调 |

---

# 附录 B. 底层指令表（排障用）

日常不需要看。`robot.request("指令", "JSON")` 可以调下面任意一条。

## 底盘

| 指令 | 载荷 | 对应接口 |
|---|---|---|
| `robot.snapshot` | `{}` | `requestSnapshot()` |
| `robot.connect` | `{}` 或 `{"settings":{...}}` | `connectChassis()` |
| `robot.disconnect` | `{}` | `disconnectChassis()` |
| `robot.enable` | `{"enabled":true}` | `enable()` |
| `robot.emergencyStop` | `{"latched":true}` | `emergencyStop()` |
| `robot.reset` / `robot.clearAlarm` | `{}` | 同名 |
| `robot.motion.stop` | `{}` | `stopMotionOnHost()` |
| `robot.settings.update` | `{"settings":{...}}` | 无（用 `request`） |

## 夹爪

| 指令 | 载荷 |
|---|---|
| `robot.clamp.connect` / `.disconnect` | `{}` |
| `robot.clamp.setMotorSpeed` | `{"axis":0,"index":0,"percent":40}` |
| `robot.clamp.stopMotor` | `{"axis":0,"index":0}` |
| `robot.clamp.stop` | `{}` |
| `robot.clamp.moveToPosition` | `{"axis":..,"index":..,"degrees":..}` |
| `robot.clamp.requestPosition` | `{"axis":..,"index":..}` |
| `robot.clamp.positions` / `.status` / `.settings.get` | `{}` |

## 摄像头

| 指令 | 载荷 |
|---|---|
| `robot.camera.scan` | `{}` |
| `robot.camera.connect` | `{"slot":0,"source":"usb","deviceIndex":0,"fps":15,"flipHorizontal":false,"flipVertical":false}` |
| `robot.camera.connect`（网络） | `{"slot":1,"source":"network","url":"rtsp://..."}` |
| `robot.camera.displaySize` | `{"slot":0,"width":640,"height":480}` |
| `robot.camera.disconnect` | `{"slot":0}` |
| `robot.camera.disconnectAll` / `.status` | `{}` |

## 超声

| 指令 | 载荷 |
|---|---|
| `ultrasound.status` | `{}` |
| `ultrasound.device.connect` | `{"address":"127.0.0.1","deviceId":0}` |
| `ultrasound.acquisition.start` / `.stop` | `{}` |
| `ultrasound.decoder.settings` | `{"groups":[{...}],"displayGroupId":0}` |
| `ultrasound.parameters.snapshot` | `{}` |
| `ultrasound.parameters.getMany` | `{"requests":[{"method":"getGain","args":[0]}]}` |
| `ultrasound.parameter.set` | `{"method":"setGain","args":[40,0],"readbackMethod":"getGain","readbackArgs":[0]}` |
| `ultrasound.parameters.apply` | `{"changes":[{"method":"setGain","args":[40,0]}]}` |
| `ultrasound.parameters.settled` | `{"resumeCapture":true}` |
| `ultrasound.presets.list` | `{}` |
| `ultrasound.presets.switch` / `.capture` / `.remove` | `{"presetId":0}` |
| `ultrasound.presets.copy` | `{"sourcePresetId":0}` |
| `ultrasound.scan.precision.set` | `{"presetId":0,"mmPerCell":5.0}` |
| `ultrasound.encoder.calibration.set` | `{"wheelDiameterMm":500,"resolution":4096,"calibrationMode":0,"countsPerMmA":..,"countsPerMmB":..}` |
| `ultrasound.encoder.reset` / `ultrasound.geometry.refresh` | `{}` |

## 外围

| 指令 | 载荷 |
|---|---|
| `robot.laser.scan` / `.disconnect` / `.status` | `{}` |
| `robot.laser.connect` | `{"serialNumber":"DB2666828"}` |
| `robot.imu.connect` | `{"port":"COM3","baudRate":115200,"outputDivider":10}` |
| `robot.imu.disconnect` / `.status` | `{}` |
| `robot.peripherals.snapshot` / `robot.peripherals.serialPorts` | `{}` |

---

# 附录 C. 事件表

用 `robot.setEventHandler([](事件名, JSON){...})` 接收。

| 事件 | 什么时候来 | 关键字段 |
|---|---|---|
| `host.ready` | 会话建立完成 | `telemetry` |
| `host.busy` | 会话被占用 | — |
| `robot.stateChanged` | 底盘状态变化 | `state`、`stateKey`、`reason` |
| `robot.connectionChanged` | 底盘连接变化 | `connected`、`message` |
| `robot.clamp.connection` | 夹爪总线变化 | `connected`、`message` |
| `robot.clamp.position` | 位置回报 | `axis`、`index`、`degrees` |
| `robot.clamp.limitReached` | 撞软限位 | `axis`、`index`、`upper` |
| `robot.camera.connection` | 摄像头连接变化 | `connected`、`slot`、`message` |
| `robot.camera.devices` | 扫描结果 | 设备列表 |
| `robot.laser.connection` | 激光相机变化 | `connected`、`message` |
| `robot.laser.devices` | 扫描结果 | 序列号列表 |
| `robot.autoCorrection.starting` / `started` / `paused` / `stopped` / `failed` | 纠偏状态 | `status`、`reason` |
| `robot.safetyStop` / `robot.motionStopped` | 安全停车 | `reason`（如 `motion_lease_expired`） |
| `ultrasound.status` | 超声状态（约 1 Hz） | `device{...}` |
| `ultrasound.parameters.invalidated` | 参数失效 | `hardwareOnline`、`captureReady` |
| `ultrasound.log` | 超声侧日志 | `message` |
