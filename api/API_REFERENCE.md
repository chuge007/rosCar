# PA1664 小车 API 使用手册

这份手册按"我想做什么"来组织，不按代码结构组织。
每一节都是：一句说明 + 可直接抄的代码 + 怎么判断成功 + 常见坑。

底层指令（`robot.connect` 这类名字）全部收在附录 B，日常使用不需要看。

---

# 目录

- [第 0 章 从零开始（新手必读）](#第-0-章-从零开始第一次用请按顺序看完)
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
- [附录 D. 编译与运行报错速查](#附录-d-编译与运行报错速查)
- [附录 E. 术语表](#附录-e-术语表)
- [附录 F. 上线前检查清单](#附录-f-上线前检查清单)

---

# 第 0 章 从零开始（第一次用请按顺序看完）

## 0.1 你需要准备什么

| 东西 | 说明 | 怎么确认 |
|---|---|---|
| Windows 10/11 64 位 | 库是 x64 的，32 位程序用不了 | 设置 → 系统 → 关于 |
| Visual Studio 2022 | 安装时必须勾"使用 C++ 的桌面开发" | 开始菜单能搜到 "Developer Command Prompt" |
| Qt 6.8.3 MSVC2022 64bit | 库是用它编的，版本必须一致 | Qt Creator → 帮助 → 关于 |
| 小车主机 | 小车那台电脑上的 `PA1664RobotHost.exe` 正在跑 | 任务管理器能看到这个进程 |
| 网络 | 你的电脑和小车主机在同一网段 | `ping 192.168.1.20` 有回复 |

**为什么版本必须一致**：这个库把 `std::string`、`std::function` 这类 C++ 对象
跨 DLL 传来传去，不同编译器版本的内部结构不一样。版本不一致往往**编译能过、
运行才崩**，很难查。所以编译器大版本、Qt 大版本都要和库一致。

## 0.2 拿到的三个文件夹分别是什么

```
小车api\
├── include\pa1664_robot_api.h     头文件：告诉编译器"有哪些函数可以调"
├── static\PA1664RobotApi.lib      静态库：代码会被拷进你的 exe
└── shared\PA1664RobotApi.dll      动态库：代码在 DLL 里，运行时才加载
    shared\PA1664RobotApi.lib      动态库的"导入库"，链接时装样子用的
```

**选哪个？**

| 你的情况 | 用哪个 | 好处 | 代价 |
|---|---|---|---|
| 自己写个小工具、想快速跑起来 | 静态库 | 只多一个 lib，不用管宏 | exe 大一点 |
| 多个程序共用、想单独升级库 | 动态库 | 换 DLL 就是升级 | 要带 DLL、要定义宏 |

新手**先用静态库**，等跑通了再考虑动态库。

## 0.3 新建第一个工程（Qt Creator，手把手）

1. 打开 Qt Creator → `文件` → `新建文件或项目`；
2. 左边选 `Application (Qt)` → `Qt Console Application` → `Choose`；
3. 名称填 `demo`，路径选**纯英文、无空格**的目录（例如 `D:\demo`）；
4. Kit 选 `Desktop Qt 6.8.3 MSVC2022 64bit`；
5. 一路 `Next` → `Finish`；
6. 打开 `demo.pro`，在最上面加三行（路径换成你的实际位置，用 `/` 不要用 `\`）：

```pro
QT += core network gui
INCLUDEPATH += D:/path/to/小车api/include
LIBS += D:/path/to/小车api/static/PA1664RobotApi.lib
```

7. 把 `main.cpp` 全部删掉，换成下面这段（先不要改 IP）：

```cpp
#include "pa1664_robot_api.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  pa1664::RobotApi robot;   // 默认连 192.168.1.20:43100

  robot.setConnectionHandler([&](bool online, const std::string& info) {
    std::printf("[连接] %s：%s\n", online ? "已连上" : "已断开", info.c_str());
  });

  robot.setStatusHandler([](const pa1664::RobotStatus& s) {
    std::printf("[状态] %s 实际速度 %.3f m/s\n", s.stateText.c_str(),
                s.appliedLinearMps);
  });

  robot.start();
  QTimer::singleShot(8000, &app, [&] { robot.stop(); app.quit(); });
  return app.exec();
}
```

8. 点左下角绿色三角（或 `Ctrl+R`）编译运行。

**如果主机不是 192.168.1.20**，把构造那行换掉：

```cpp
pa1664::Config cfg;
cfg.host = "192.168.66.10";     // 改成你的主机 IP
pa1664::RobotApi robot(cfg);
```

## 0.4 正常运行应该看到什么

```
[连接] 已连上：会话 2 在线，实时 43101 / 媒体 43102 / 批量 43103
[状态] 就绪 实际速度 0.000 m/s
[状态] 就绪 实际速度 0.000 m/s
...
[连接] 已断开：已停止
```

第一行的三个端口（43101 / 43102 / 43103）都出现，说明三条通道全通。

看到这个就说明**环境、库、网络全部正确**，可以往后读了。看不到就查 0.5、0.6。

## 0.5 编译/链接报错对照表

| 报错 | 原因 | 解决 |
|---|---|---|
| `无法打开源文件 "pa1664_robot_api.h"` | 没加头文件路径 | `.pro` 里加 `INCLUDEPATH` |
| `LNK2019 无法解析的外部符号 ...RobotApi...` | 没链接库 | `.pro` 里加 `LIBS` |
| `LNK2019 ... __imp_...RobotApi...` | 静态方式却定义了 `PA1664_ROBOT_API_DLL` | 删掉这个宏 |
| `LNK1181 无法打开输入文件 ...lib` | 路径含中文/空格，或 lib 不在那里 | 路径改 `/`，确认文件存在 |
| `LNK2038 RuntimeLibrary 不匹配` | 你的工程用了 `/MT` | 改成 `/MD`（默认就是） |
| 运行时提示找不到 `Qt6Core.dll` | PATH 里没有 Qt 的 bin | 把 `Qt\6.8.3\msvc2022_64\bin` 加进 PATH |
| 找不到 `Qt6Network.dll` / `Qt6Gui.dll` | 只拷了一个 DLL | 三个都要：Core、Network、Gui |
| 程序一闪而过 | 直接在资源管理器双击运行 | 用 `cmd` 运行，或末尾加 `getchar()` |

## 0.6 连不上？按这个顺序查（从最常见开始）

1. **`ping 192.168.1.20` 通不通** —— 不通就是网络或小车没开机，先解决这个；
2. **小车端 `PA1664RobotHost.exe` 在不在跑** —— 任务管理器看，不在就启动；
3. **端口 43100 是否被防火墙挡住** —— 临时关杀毒/防火墙试一次；
4. **是不是被工作台占了** —— 同一时刻只允许一个客户端，报 `host.busy` 就关掉工作台；
5. **看日志** —— `<你的exe>\logs\comm\comm_control.log`，每条请求都有记录。

## 0.7 新手最容易犯的五个错

1. 没 `takeControl()` 就发速度 → 车不动，而且**不报错**（最坑）；
2. 发了 `forward()` 忘了 `stopMotion()` → 车一直走；
3. 工程路径有中文 → 链接报 `LNK1181`；
4. 用了动态库却忘了 `PA1664_ROBOT_API_DLL` 宏 → 链接失败；
5. 自己程序和工作台同时连 → `host.busy`。

---

# 先看这个

## 先按你的情况选一条路线

| 你的情况 | 建议读法 |
|---|---|
| 第一次接触，刚拿到库 | 第 0 章（环境 + 第一个程序）→ 第 1 章 → 第 2 章 |
| 已经能连上，只想让车动 | 第 2 章；车不动就查 2.3 |
| 要接夹爪 / 摄像头 / 超声 | 直接跳第 3 / 4 / 5 章，每章开头都有最小示例 |
| 要激光循迹 | 第 6 章 + 第 8 章（激光相机） |
| 要保存或下发配置 | 第 7 章，重点看 7.5 |
| 报错了 | 第 9 章 → 附录 D |
| 想知道底层发了什么 | 附录 B |

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

## 3.5 面板上那四个按钮，对应哪个电机

工作台的探头调节区有四个按钮，对应关系如下（来自 `drive_settings.ini` 里每台电机的 `alias`）：

| 面板按钮 | 电机 | 调用 | 本机 ini 里的 alias |
|---|---|---|---|
| 激光相机摆动 | X1（axis 0, index 0） | `robot.x1Motor(percent)` | 激光轮廓仪 |
| 探头间距 | X2（axis 0, index 1） | `robot.x2Motor(percent)` | 探头间距 |
| 探头 1 升降 | Z1（axis 2, index 0） | `robot.z1Motor(percent)` | 探头1上下 |
| 探头 2 升降 | Z2（axis 2, index 1） | `robot.z2Motor(percent)` | 探头1上下 |

**这个对应关系不是写死的**，来自配置里的 `alias`。换车或改了接线，用
`loadDriveConfig()` / `requestDriveConfig()` 读 `DriveConfig::clampMotors[i].alias`
重新对上即可。

**注意**：面板底部那句"激光摆动、间距与独立升降未接入电机接口"说明当前界面这几个
按钮没绑定电机，按下去不会动。要用就直接调 API：

```cpp
robot.clampConnect();                 // 先连总线，等 robot.clamp.connection 事件
robot.x1Motor(20);                    // 激光相机摆动 +20%
QTimer::singleShot(800, [&] { robot.clampStopMotor(0, 0); });

robot.x2Motor(-10);                   // 探头间距收
robot.z1Motor(15);                    // 探头 1 升
robot.z2Motor(15);                    // 探头 2 升（两台必须一起动，否则探头会歪）
```

做"按住动、松开停"的按钮时：

```cpp
void beginMove(int axis, int index, int percent) {
  robot.clampSetSpeed(axis, index, percent);   // 按下
}
void endMove(int axis, int index) {
  robot.clampStopMotor(axis, index);           // 松开，立刻停
}
```

Y 轴如果有电机，用 `clampSetSpeed(1, 0, percent)`，面板上没有对应按钮。

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

### 5.2.1 编码器数值（每帧原始数据里就有）

A 扫帧除了波形，还带**原始包尾**：5 路编码器计数 + 错误标志。这是设备原始
数据，不经过主机换算，可以直接算物理位置。

| 字段 | 含义 |
|---|---|
| `encoders[0]` | A 轴计数（扫查方向） |
| `encoders[1]` | B 轴计数（行方向） |
| `encoders[2..4]` | 设备其余轴（C/D/E） |
| `encoderTriggerError` | 编码器触发错误，这一帧不应参与拼图 |
| `encoderSyncError` | 编码器同步错误 |
| `frameError` / `scannerIo` / `multiFrame` | 包级状态 |

换算成毫米（系数来自 `ultrasound.encoder.calibration`）：

```cpp
robot.requestUltrasoundEncoderCalibration();     // 先读一次系数

robot.setUltrasoundFrameHandler([](const pa1664::UltrasoundAScanFrame& f) {
  const double countsPerMm = robot.encoderCountsPerMm(0);   // A 轴
  if (countsPerMm <= 0.0) return;
  if (f.encoderTriggerError || f.encoderSyncError) return;  // 该帧丢弃
  const double positionMm = f.encoders[0] / countsPerMm;
  std::printf("帧 %u 位置 %.2f mm（B 轴 %d）\n", f.frameNumber, positionMm,
              f.encoders[1]);
});
```

另外 `f.cColumn` / `f.cRow` / `f.bColumn` 是主机按编码器算好的**网格列号**，
只在 C 扫批次通道（TCP 43103）出现；实时显示流（UDP 43102）里是 `-1`：

```cpp
positionMm = f.cColumn * robot.scanPrecisionMmPerCell();
```

两者的区别：`encoders` 是原始计数（精度高、可自己定原点），`cColumn` 是
主机按 `mm/格` 取整后的列号（直接画图方便）。

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

### 5.3.1 参数那么多，我怎么知道有哪些、哪些常用

**先别背参数名。** 让主机把 84 项全报一遍，你从里面挑：

```cpp
robot.setResponseHandler(
    [&](const std::string& op, bool ok, const std::string&,
        const std::string&, const std::string& data) {
      if (op != "ultrasound.parameters.snapshot" || !ok) return;
      const QJsonObject values =
          QJsonDocument::fromJson(QByteArray::fromStdString(data))
              .object().value("values").toObject();
      for (auto it = values.begin(); it != values.end(); ++it) {
        std::printf("%s = %s\n", it.key().toStdString().c_str(),
                    QJsonDocument(QJsonArray{it.value()})
                        .toJson(QJsonDocument::Compact)
                        .toStdString().c_str());
      }
    });
robot.ultrasoundParameterSnapshot();
```

输出的 key 长这样：`getGain:WzBd`。冒号前是方法名，冒号后是参数的 base64
（`WzBd` 就是 `[0]`，组号 0）。要用哪个，就把 `getXxx` 换成 `setXxx`。

**实测确认可用的常用参数**：

| 方法名 | 含义 | 典型值 |
|---|---|---|
| `getGain` / `setGain` | 增益，越大越亮也越噪 | 13 / 20 / 40 |
| `getRangeStart` / `getRangeEnd` | 显示深度范围（mm） | 0 / 50 |
| `getPulseWidth` | 脉冲宽度 | 50 |
| `getBeamCounts` | 波束数 | 1 |
| `getBeamAperture` | 孔径（激活晶片数） | 1 |
| `getBeamAngleMin` / `getBeamAngleMax` / `getBeamAngleStep` | 扫查角度范围与步进 | 35 / 70 / 1 |
| `getProbeFrequency` | 探头频率档位 | 5 |

不在表里的按同样规则试：`setXxx` 的 args 写 `[值, 0]`，库会自动回读；
回读值和写入值不一致，就是参数名或取值超范围。

**写参数的三条铁律**：

1. 一次只改一个，改完看回读值；
2. 改完必须 `ultrasoundParametersSettled()`，否则采集不恢复；
3. `hardwareOnline=false` 时别写，必超时。

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

## 6.4 激光轮廓线是怎么画的

### 数据从哪来

```
激光相机 → USB → 主机 LaserCorrectionController
        → 媒体流 kind=LaserProfileObservation（streamId 0x0204）
        → 你的 setLaserProfileHandler(LaserProfileFrame)
```

`LaserProfileFrame` 里只有两样关键东西：

| 字段 | 含义 |
|---|---|
| `pointCount` | 这一帧有多少个点 |
| `coordinates` | `pointCount` 组 `(x, y, z)`，长度 = `pointCount * 3`，**仅回调内有效** |

坐标是激光 SDK 原始值（相机坐标系，不做单位换算），约定 **x = 横向、z = 高度**，
y 是扫描方向。

### 第一步：过滤无效点

```cpp
std::vector<QPointF> points;              // 只留 (x, z)
for (int i = 0; i < f.pointCount; ++i) {
  const float x = f.coordinates[i * 3 + 0];
  const float z = f.coordinates[i * 3 + 2];
  if (!std::isfinite(x) || !std::isfinite(z)) continue;     // 非有限值
  if (z <= 0.0f) continue;                                  // 打到镜头外
  if (!points.empty() && x <= points.back().x()) continue;  // x 必须递增
  points.emplace_back(x, z);
}
```

工作台就是按这三条过滤的；不过滤会看到轮廓"乱连、锯齿、回头线"。

### 第二步：映射到屏幕

X 轴要**以视野零点（激光线中心）为对称轴**，不能按数据范围铺满 —— 否则焊道在
屏幕上会被拉到中间，和激光线实际打到的位置对不上：

```cpp
const double halfSpanX = std::max(std::abs(minX), std::abs(maxX));
const double minXPlot = -halfSpanX;       // 关于 0 对称
const double maxXPlot = +halfSpanX;

auto mapPoint = [&](double x, double z) {
  return QPointF(
      plot.left() + (x - minXPlot) / (maxXPlot - minXPlot) * plot.width(),
      plot.bottom() - (z - minZ) / (maxZ - minZ) * plot.height());
};
```

高度（z）方向按数据范围线性铺满即可；工作台还加了 0.2 的平滑，避免量程抖动。

### 第三步：逐点连线

```cpp
QPainter painter(this);
painter.fillRect(rect(), QColor("#101b27"));

bool first = true;
QPointF previous;
for (const QPointF& p : points) {
  const QPointF device = mapPoint(p.x(), p.y());
  if (!first) painter.drawLine(previous, device);
  painter.drawPoint(device);
  previous = device;
  first = false;
}
```

颜色照抄工作台即可：

| 元素 | 颜色 | 含义 |
|---|---|---|
| 轮廓线 | `#59d8ff`（新鲜）/ `#78848b`（过期） | 数据超时必须变灰 |
| 视野中心 | `#9aa5af` 虚线 | x = 0 |
| 焊道区间 | 绿色半透明填充 + 两条 `#53ee82` 竖线 | `gapStartPx..gapEndPx` |
| 焊道中心 | `#ff668a` 粉色虚线 | 纠偏用的定位线 |
| 拟合基线 | `#f6cb54` 黄色虚线 | 基线+肩部算法的参考线 |

只想画轮廓、不画检测标记时，忽略 `gapStartPx/gapEndPx/profileCenterX` 即可。

### 画线和纠偏是两件事

- **画线**：`setLaserProfileHandler` 给你看，不参与控制；
- **纠偏**：主机用同一份轮廓自己算焊道中心 → 偏差 → 角速度 → 底盘，
  你只调 `startAutoCorrection()`，**不要**用自己画的线去下发速度。

### 常见坑

- `coordinates` 只在回调里有效，要留存必须拷贝成 `std::vector<QPointF>`；
- 约 30 fps（33 ms 一帧），回调里别做重绘以外的重活；
- 没连激光相机时一帧都收不到，先 `connectLaser(序列号)`；
- 数据过期（`receivedAtEpochMs` 太旧）要清空画布，否则屏幕会留一条"假轮廓"。

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

## 7.5 界面上的配置项，对应哪些接口、存到哪里

先记住一条分界线：**有些参数只存在你自己的电脑上，有些必须下发给小车主机。**

### 只在客户端（你的 exe 目录）

| 界面项 | 对应 | 说明 |
|---|---|---|
| 底盘 TCP 地址 | `Config::host` | 就是主机的 IP，构造时传 |
| PA1664 控制端口 | `Config::controlPort` | 默认 43100 |
| 速度 / 几何（轮径、轮距、限幅） | `Config::maxLinearSpeedMps` 等 | 本地换算用，可用 `applyDriveConfig()` 对齐主机 |
| 安全看门狗超时 | `Config::motionLeaseMs` 一类 | 客户端侧判定 |

这些不经过网络，构造 `RobotApi` 时给就行：

```cpp
pa1664::Config cfg;
cfg.host = "192.168.66.10";
cfg.controlPort = 43100;
pa1664::RobotApi robot(cfg);
```

工作台把它们存在 `<exe>\config\drive_settings.ini`（客户端本机那份）。

### 要下发给主机（主机 ini）

| 界面项 | 下发接口 | 主机保存位置 |
|---|---|---|
| 驱动轮通信方式（CAN/RS485） | `robot.settings.update` | `robot_host\app\config\drive_settings.ini` |
| CAN / SLCAN 串口 | `robot.settings.update` | 同上（端口由主机强制保留） |
| CAN 适配器波特率 / 总线波特率 | `robot.settings.update` | 同上 |
| 左右轮电机 ID、方向 | `robot.settings.update` | 同上 |
| 夹子电机（协议、ID、减速比、软限位） | `robot.clamp.settings.update` | 同上 |
| 摄像头（USB 索引 / RTSP 地址） | `robot.camera.connect` | 主机内存，重连后需重新下发 |
| IMU 串口与波特率 | `robot.imu.connect` | `drive_settings.ini` 里的 `imuPort/imuBaudRate` |
| 激光相机序列号 | `robot.laser.connect` | `drive_settings.ini` 的 `laserSerialNumber` |

写配置只有三条路：

```cpp
// 1) 用缓存里的完整配置（先 requestDriveConfig() 拿到主机原文再改）
robot.applyDriveProfileToHost();

// 2) 自己拼 settings 下发（iniBase64 是主机 ini 的 base64）
robot.request("robot.settings.update",
              R"({"settings":{"schema":13,"iniBase64":"<base64 ini>"}})");

// 3) 只改夹爪部分
robot.request("robot.clamp.settings.update",
              R"({"settings":{"schema":13,"iniBase64":"<base64 ini>"}})");
```

主机收到后：解析 → 校验（`driveSettingsFromJson` / `clampValidationError`）→
`adoptHostPorts()` 保留本机端口 → `persistSettings()` 落盘 → 回一份 `snapshot`。

**端口为什么改不动**：主机在 `adoptHostPorts()` 里强制保留自己的 COM/PCAN 口，
客户端传的串口名会被忽略；波特率、电机 ID、减速比这些照常生效。这是防止客户端
把主机端口改瞎，属于设计行为，不是 bug。

### 改完立刻验证

```cpp
robot.setDriveConfigHandler([](const pa1664::DriveConfig& c) {
  std::printf("主机现在是：CAN 口=%s 适配器波特率=%d 总线波特率=%d 左轮ID=%d\n",
              c.wheelCanPort.c_str(), c.wheelCanBaudRate, c.wheelCanBitrate,
              c.leftMotorId);
});
robot.requestDriveConfig();
```

### 常见坑

- 界面写着"**仅保存**"的区块（如图里的"MWD RS485 / CAN · 仅保存"）表示只写本机，
  不会下发硬件；要生效必须重启主机并带 settings 重新 `robot.connect`；
- 改了波特率/ID 后主机不会自动重连驱动，需要 `disconnectChassis()` + `connectChassis()`；
- `applyDriveProfileToHost()` 用的是**缓存**配置（上次 `requestDriveConfig()` 的结果），
  别拿过期缓存覆盖主机；
- 配置错误的主机答复是 `invalid_settings` + 中文原因，看 `errorText` 就知道哪一项不合法。

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

# 附录 A. 函数逐个速查

> 先读这段通用约定，后面表格不再重复：
>
> - 返回 `Result` 的函数：`ok=true` 只表示**已经发给主机**，是否成功要看响应回调
>   （`setResponseHandler`）或状态回调（`setStatusHandler`）。
> - `NotOnline` = 还没连上；`InvalidArgument` = 参数越界（看 `message` 里的范围）；
>   `Transport` = 本机发送失败。
> - 坐标：`axis` 0=X、1=Y、2=Z；`index` 从 0 开始（X1 是 axis0/index0）。
> - 运动参数 `r` ∈ [0,1]；`moveVelocity` 用物理单位（m/s、rad/s）。
> - 没有列在表里的底层操作，用 `request(operation, json)`，见附录 B。

## A.1 构造、会话与本地配置（7 个方法，下面逐个讲）

### `RobotApi(Config)` — 创建对象

**干什么**：创建一个"遥控器对象"。构造时**不会连网**，只是把配置记下来。

**参数**：`Config` 结构体，可以整个省略。常用字段：

| 字段 | 默认 | 说明 |
|---|---|---|
| `host` | `"192.168.1.20"` | 小车主机的 IP |
| `controlPort` | `43100` | 控制端口，一般不用改 |
| `maxLinearSpeedMps` | `0.120` | `forward(1.0)` 对应的速度 |
| `maxAngularSpeedRadps` | `0.8727` | `turnLeft(1.0)` 对应的角速度 |
| `motionLeaseMs` | `200` | 多久没新指令就自动停车 |

```cpp
pa1664::RobotApi robot;                    // 全默认

pa1664::Config cfg;                        // 或自己指定 IP
cfg.host = "192.168.66.10";
pa1664::RobotApi robot2(cfg);
```

**注意**：一个进程里可以有多个对象，但**同一台车只允许一个会话**，
第二个对象连上去会收到 `host.busy`。

### `config()` — 看当前配置

**干什么**：返回构造时那份 `Config` 的引用，只读。

```cpp
std::printf("上限 %.3f m/s\n", robot.config().maxLinearSpeedMps);
```

**什么时候用**：算"半速到底是多少 m/s"时；或者界面要显示当前设置。

### `applyDriveConfig(cfg)` — 用主机 ini 修正本地限幅

**干什么**：把主机配置里的轮径、轮距、最大速度、加速度抄到本地 `Config`，
让 `forward(1.0)` 的速度和主机允许的一致。

**参数**：`DriveConfig`，来源有两个：

```cpp
// 来源一：本地 ini（程序就在主机那台电脑上时）
auto cfg = pa1664::loadDriveConfig();
robot.applyDriveConfig(cfg);

// 来源二：从主机网络拉（推荐，程序在任何一台电脑上都能用）
robot.setDriveConfigHandler([](const pa1664::DriveConfig& c) {
  if (c.loaded) robot.applyDriveConfig(c);
});
robot.requestDriveConfig();
```

**注意**：这是**本地**修正，不会改主机配置；它只影响你这边算出来的速度。

### `start()` — 开始连接

**干什么**：启动后台线程去连主机。**立刻返回**，不会阻塞你的界面。

**参数**：没有。

**调用后**：后台做三件事——TCP 握手、协商三个端口、开始收遥测。
成功后回调 `setConnectionHandler(true)`。

```cpp
robot.setConnectionHandler([](bool online, const std::string& info) {
  std::printf("%s %s\n", online ? "连上" : "断开", info.c_str());
});
robot.start();
```

**注意**：`start()` 之后**不能马上发指令**，要等回调。断线会自动重连，
重连成功还会再回调一次 `online=true`，注意别重复初始化。

### `stop()` — 主动断开

**干什么**：关闭会话，同时清零运动指令。

```cpp
robot.stopMotion();   // 先停
robot.stop();         // 再断
```

**注意**：对象析构时也会自动调 `stop()`，所以忘了写不会把车留在运动状态
（还有 200 ms 租约兜底）。

### `isOnline()` / `mediaReady()` / `bulkReady()` — 查通道状态

**干什么**：三个不同通道是否就绪。

| 方法 | 通道 | 什么时候需要它 |
|---|---|---|
| `isOnline()` | TCP 43100 控制 | 发任何指令前 |
| `mediaReady()` | UDP 43102 媒体 | 收摄像头、A 扫、激光轮廓前 |
| `bulkReady()` | TCP 43103 批量 | 收 C 扫批次前 |

```cpp
if (robot.isOnline() && robot.mediaReady())
  robot.connectCamera(0, 0, 15);
```

**注意**：`isOnline()` 为 true 时另外两个通常也好了，但网络抖动时可能短暂不一致，
收图像的程序最好判断一下 `mediaReady()`。

### `statusText()` — 看最近一次状态描述

**干什么**：返回一句中文（UTF-8），比如"会话 2 在线，实时 43101 / 媒体 43102 / 批量 43103"。

```cpp
std::printf("[链路] %s\n", robot.statusText().c_str());
```

**什么时候用**：打日志、做界面状态栏；比 `isOnline()` 多了"为什么"的信息。

## A.2 底盘（10 个方法，下面逐个讲）

> 底盘这一组有个固定顺序：**连接（connect）→ 使能（enable）→ 才能动**。
> 反了会被主机拒绝，或者进了故障状态。

### `takeControl()` — 一步拿到控制权（推荐）

**干什么**：先发 `robot.connect`，等主机应答后再等 1.2 秒发 `robot.enable`。

**为什么有 1.2 秒**：主机使能时要检查左右轮电机的反馈，`armingTimeout` 默认 1009 ms，
库加了点余量。这个等待是必须的，不是库在偷懒。

**参数**：没有。

```cpp
robot.setStatusHandler([](const pa1664::RobotStatus& s) {
  std::printf("state=%s %s\n", s.stateText.c_str(), s.reason.c_str());
});
robot.takeControl();
```

**调用后**：`RobotStatus::state` 会经历
`Disconnected → Idle(就绪) → Arming(使能中) → Enabled(已使能)`。
看到 `Enabled` 才能发速度。

**出错怎么办**：

| state / 返回 | 原因 | 处理 |
|---|---|---|
| `Fault` + "轮子电机库未连接" | 没先 connect，或主机 CAN 口打不开 | 先 `connectChassis()`；仍失败就看主机日志 |
| `Fault` + "Cannot open ... CAN ..." | 主机 COM 口被占用/驱动没装 | 在主机上关掉占用程序 |
| 一直 `Arming` 然后回 `Idle` | 电机反馈收不到（轮子被卡住/编码器线断） | 检查轮子是否被压住 |

### `connectChassis()` — 只连接底盘

**干什么**：只发 `robot.connect`，不使能。底座驱动按主机 ini 里的配置去打开。

**什么时候用**：需要自己控制时序时（比如先看状态，确认没问题再使能）。

**参数**：没有。串口、CAN 口、电机 ID 全部用主机自己 ini 里的值
（客户端传的端口会被主机忽略，见 7.5）。

```cpp
robot.connectChassis();
// 等 robot.stateChanged 里 state 变成"就绪"再 enable
robot.enable(true);
```

**注意**：第一次用之前，必须已经在工作台里配好并存过一次底盘参数，
否则主机连不上驱动（会报 `invalid_settings` 或直接进故障）。

### `disconnectChassis()` — 断开底盘

**干什么**：让主机断开与驱动器的连接，电机会失电。

```cpp
robot.stopMotion();          // 先停
robot.disconnectChassis();
```

**注意**：只是断驱动连接，控制会话还在（还能读状态、改配置）。

### `enable(bool)` — 使能 / 撤销使能

**干什么**：`true` 给电机通电并保持（可以接受速度指令）；`false` 撤销使能（电机松轴）。

**参数**：

| 值 | 效果 |
|---|---|
| `true` | 使能，进入 Arming，约 1 秒后 Enabled |
| `false` | 撤销使能，立即回 Idle，车会滑行（不是急停） |

```cpp
robot.enable(true);    // 使能
robot.enable(false);   // 松轴（不要用这个当停车）
```

**注意**：`enable(false)` ≠ 停车。要停车用 `stopMotion()`，要紧急停用 `emergencyStop()`。

### `emergencyStop()` — 急停（锁存）

**干什么**：同时走两条路——UDP 发 5 次锁存急停包，再走 TCP 发一次请求。
即使控制连接断了，主机也能收到锁存。

```cpp
robot.emergencyStop();
```

**调用后**：`state` 变 `EmergencyStop`，电机立刻停，**并且锁住**——
后续速度指令一律无效，直到你做恢复动作。

**恢复步骤**：

```cpp
robot.reset();        // 清故障（有故障时）
robot.enable(true);   // 重新使能
```

**注意**：不要用 `stopMotion()` 代替急停；急停是"停下来并锁住"，
`stopMotion()` 只是"发零速"。

### `reset()` — 清故障、重启控制环

**干什么**：清掉故障状态与报警位，让底盘回到"就绪"。

**什么时候用**：急停之后、进 `Fault` 之后、换电机/换线之后。

```cpp
robot.reset();
```

**注意**：reset 不会自动使能，要再 `enable(true)`；也不会自动连底盘，
如果之前 `disconnectChassis()` 过，要先 `connectChassis()`。

### `clearAlarm()` — 只清报警位

**干什么**：清报警标志，不动故障状态机。

**和 `reset()` 的区别**：

| 方法 | 清报警 | 复位控制环 | 用途 |
|---|---|---|---|
| `clearAlarm()` | 是 | 否 | 误报、轻微告警 |
| `reset()` | 是 | 是 | 急停后、故障后 |

```cpp
robot.clearAlarm();
```

### `requestSnapshot()` — 查一份完整快照

**干什么**：让主机回一份"当前状态大礼包"：连接状态、配置、遥测、限位、
夹爪/摄像头/超声的概要。

**参数**：没有。结果异步回到 `setResponseHandler`：

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.snapshot" && ok) std::printf("%s\n", data.c_str());
});
robot.requestSnapshot();
```

**什么时候用**：程序刚连上时做一次全量同步；排查问题时手动抓一份。

### `requestDriveConfig()` — 拉底盘配置（推荐）

**干什么**：发 `robot.snapshot`，但只关心其中的 `settings`，把它解析成
`DriveConfig` 结构体，通过 `setDriveConfigHandler` 给你。

**和 `loadDriveConfig()` 的区别**：

| 方式 | 数据来源 | 适用场景 |
|---|---|---|
| `loadDriveConfig()` | 本地 ini 文件 | 程序就跑在主机那台电脑上 |
| `requestDriveConfig()` | 主机网络回传 | 程序在任意电脑上（推荐） |

```cpp
robot.setDriveConfigHandler([](const pa1664::DriveConfig& c) {
  if (!c.loaded) {
    std::printf("主机没给配置：%s\n", c.error.c_str());
    return;
  }
  robot.applyDriveConfig(c);      // 对齐限幅
  std::printf("轮距 %.3f m，夹爪 %d 台\n", c.trackWidthM,
              int(c.clampMotors.size()));
});
robot.requestDriveConfig();
```

**注意**：`c.loaded == false` 表示主机没返回有效配置（不是库坏了），
看 `c.error` 里写的什么。

## A.3 运动（7 个方法，下面逐个讲）

> 这三条必须同时成立，车才会动：**已连接 → 已使能 → 指令持续发**。
> 前两个不满足时，速度指令会被**静默忽略**（不报错），这是新手最容易卡住的地方。

### `forward(r)` — 前进

**干什么**：让车以 `r × 最大线速度` 前进，直到你调 `stopMotion()`。

**参数**：

| 参数 | 范围 | 说明 |
|---|---|---|
| `r` | 0.0 ~ 1.0 | 比例，不是 m/s。`1.0` = `config().maxLinearSpeedMps`（默认 0.12 m/s） |

```cpp
robot.forward(0.5);      // 0.5 × 0.12 = 0.06 m/s
```

**调用后**：主机把速度指令缓存在运动通道里，每 50 ms 自动重发。
`RobotStatus::appliedLinearMps` 会从 0 慢慢爬到目标值（斜坡）。

**怎么确认真的动了**：

```cpp
robot.setStatusHandler([](const pa1664::RobotStatus& s) {
  std::printf("state=%s 目标=%.3f 实际=%.3f\n", s.stateText.c_str(),
              s.targetLinearMps, s.appliedLinearMps);
});
```

**注意**：

- **不会自己停**，必须 `stopMotion()`；
- 未使能时调用不报错、也不动；
- 程序崩溃时靠 200 ms 租约自动停车，但别依赖它。

### `backward(r)` — 后退

**干什么**：和 `forward` 一样，只是线速度取负。

```cpp
robot.backward(0.3);     // 0.036 m/s 后退
```

**注意**：后退时如果车后面有障碍，主机不会帮你避障——限位只在夹爪/行程上，
轮子没有。

### `turnLeft(r)` / `turnRight(r)` — 原地转向

**干什么**：原地左转 / 右转，线速度为 0，只有角速度。

**参数**：`r` 是角速度比例，`1.0` 对应 `maxAngularSpeedRadps`（默认 50 deg/s）。

```cpp
robot.turnLeft(0.5);     // 原地左转 25 deg/s
robot.turnRight(0.5);    // 原地右转 25 deg/s
```

**为什么容易搞混**：差速底盘的约定是**正角速度 = 右转**。
所以 `turnLeft` 内部把角速度取负，`turnRight` 取正。你按名字用就行，
只有自己调 `moveVelocity` 时才需要记住这个符号。

### `moveVelocity(v, w)` — 精确速度

**干什么**：直接给线速度（m/s）和角速度（rad/s），不做比例换算。

**参数**：

| 参数 | 单位 | 说明 |
|---|---|---|
| `v` | m/s | 正前进、负后退；绝对值不能超过 `config().maxLinearSpeedMps` |
| `w` | rad/s | 正 = 右转、负 = 左转；不能超过 `maxAngularSpeedRadps` |

```cpp
robot.moveVelocity(0.05, 0.0);     // 0.05 m/s 直行
robot.moveVelocity(0.05, -0.2);    // 前进同时左转
robot.moveVelocity(0.0, 0.3);      // 原地右转
```

**注意**：超限会直接返回 `InvalidArgument`（不是被截断），所以自己先判断：

```cpp
const double vmax = robot.config().maxLinearSpeedMps;
if (std::abs(v) > vmax) { /* 自己限幅 */ }
```

### `stopMotion()` — 停车（保持使能）

**干什么**：发零速指令，车停下，但电机仍然使能，可以马上再发速度。

```cpp
robot.stopMotion();
```

**和急停的区别**：

| 方法 | 效果 | 恢复 |
|---|---|---|
| `stopMotion()` | 零速，仍使能 | 直接发新速度 |
| `emergencyStop()` | 立刻停 + 锁存 | `reset()` + `enable(true)` |

**注意**：停车后 `appliedLinearMps` 会沿斜坡降到 0，不是瞬间归零；
需要立即停就用急停。

### `stopMotionOnHost()` — 让主机侧也清零

**干什么**：`stopMotion()` 是从客户端发零速包，主机靠"收到零速指令"停车；
这个函数额外走一条 TCP 请求 `robot.motion.stop`，让主机主动把速度指令清零。

```cpp
robot.stopMotionOnHost();
```

**什么时候用**：怀疑 UDP 零速包没送到（网络丢包、防火墙只放了 TCP）；
或者程序要退出了，想要一个"确定性"的停车确认。

**注意**：它不替代 `stopMotion()`，正常停车还是先 `stopMotion()`。

## A.4 自动纠偏（4 个方法，下面逐个讲）

> 纠偏是**主机自己闭环**的：它用激光轮廓找焊缝中心，算偏差，直接控制底盘。
> 你只负责"启动/暂停/停止"，不要自己去发速度。

### `startAutoCorrection(settingsJson)` — 启动激光循迹

**干什么**：让主机开始"看轮廓 → 找焊缝 → 自动调方向"。

**参数**：一个 JSON 字符串，只需要写你要改的键，其余用主机默认值；
传 `"{}"` 表示全默认。

| 键 | 默认 | 说明 |
|---|---|---|
| `targetSpeedMps` | 0.005 | 循迹时的前进速度（很慢，正常） |
| `segmentLengthM` | 0.03 | 每次拟合多长一段焊缝（米），0.001~1.0 |
| `proportionalGain` | 3.0 | 偏差纠正力度，越大越"急" |
| `derivativeGain` | 0.12 | 抑制抖动 |
| `imageTimeoutMs` | 500 | 多久没收到激光帧就认为丢信号 |
| `maximumCurvatureRadPerM` | 1.50 | 转弯曲率上限 |

```cpp
robot.startAutoCorrection(R"({"targetSpeedMps":0.008,"segmentLengthM":0.04})");
```

**前置条件**（缺一个就失败）：

1. 底盘已连接；
2. 底盘已使能（`state == Enabled`）；
3. 激光相机已连接（`connectLaser(序列号)`）。

**调用后**：先回 `{"state":"preparing"}`，这只是"开始准备"；
真正跑起来要看事件 `robot.autoCorrection.started`。

```cpp
robot.setCorrectionStatusHandler([](const pa1664::CorrectionStatus& s) {
  std::printf("active=%d paused=%d phase=%s 速度=%.4f 角速度=%.4f\n",
              s.active, s.paused, s.phase.c_str(), s.linearCommandMps,
              s.angularCommandRadps);
});
```

**出错怎么办**：

| errorCode | 原因 | 处理 |
|---|---|---|
| `drive_not_connected` | 没 `connectChassis()` | 先连接 |
| `drive_not_enabled` | 没使能 | `takeControl()` |
| `laser_not_connected` | 激光相机没连 | `connectLaser("序列号")` |
| `correction_busy` | 已经在跑 | 先 stop |

### `stopAutoCorrection()` — 停止循迹

**干什么**：让主机停止纠偏闭环，交还运动控制权。

```cpp
robot.stopAutoCorrection();
robot.stopMotion();        // 必须补这一句
```

**为什么必须补 `stopMotion()`**：停止纠偏只是"不再自动调方向"，
主机不会自动把速度清零——不补这一句，车会按最后一次速度继续走。

**注意**：停止事件里带 `reason`，常见值：

| reason | 含义 |
|---|---|
| `operator_stop` | 你主动停的 |
| `operator_timeout` | 客户端长时间没响应 |
| `motion_lease_expired` | 运动租约过期（客户端掉线） |
| `udp_emergency_stop` | 收到急停 |

### `pauseAutoCorrection(paused)` — 暂停 / 继续

**干什么**：`true` 暂停（停止发速度，但保留状态）；`false` 继续。

```cpp
robot.pauseAutoCorrection(true);    // 暂停，车停住
robot.pauseAutoCorrection(false);   // 继续循迹
```

**和 stop 的区别**：

| 操作 | 状态 | 能否立刻继续 |
|---|---|---|
| `pause(true)` | 保留 tracking 状态 | 能，`pause(false)` 即可 |
| `stop()` | 会话结束，要重新 start | 要重新走一遍启动 |

**注意**：暂停期间激光仍然在工作，恢复时会用最新一帧继续。

### `requestAutoCorrectionStatus()` — 查一次状态

**干什么**：主动问一次当前纠偏状态，不用等事件。

```cpp
robot.requestAutoCorrectionStatus();
// 结果同时会走 setCorrectionStatusHandler
```

**什么时候用**：界面刷新、启动后确认是否已经在跑、
或收到 `correction_busy` 时先查一下。

## A.5 夹爪（14 个方法，下面逐个讲）

> 先记住坐标：**X1 = axis 0 / index 0**，**X2 = axis 0 / index 1**，
> **Z1 = axis 2 / index 0**，**Z2 = axis 2 / index 1**。
> Y 轴如果有电机是 axis 1，本机没有。

### `clampConnect()` — 打开夹爪总线

**干什么**：让主机去打开夹爪电机那条总线（CAN 或 RS485）。在此之前，
所有调速、定位命令都会被拒绝。

**什么时候用**：程序启动后、第一次让夹爪动之前；夹爪掉线重连后也要再调一次。

**参数**：没有。用哪条总线、什么协议、几台电机，全在主机自己的
`drive_settings.ini` 里，客户端不用传。

**调用后**：函数立刻返回 `ok=true`（只代表请求发出去了）；主机在后台连 4 台电机，
连上后推事件 `robot.clamp.connection`，里面有 `connected=true`。**等到事件才能调速。**

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.connection")
    std::printf("[夹爪] %s\n", data.c_str());
});

robot.clampConnect();
// 看到 connected=true 再往下走
```

**出错怎么办**：

| 现象 | 原因 | 处理 |
|---|---|---|
| `clamp_service_unavailable` | 主机夹爪线程还没起来 | 等 1~2 秒重试 |
| 事件里 `connected=false` | 电机没上电、总线没插、ID 配错 | 看事件里的 `message`；检查 24V 供电和 ini |
| 一直没有事件 | 请求没发出去 | 确认 `isOnline()` 是 true |

### `clampDisconnect()` — 关闭夹爪总线

**干什么**：先停全部电机，再断开总线。

**什么时候用**：程序退出前；或者需要重新改夹爪配置、重启夹爪时。

**参数**：没有。

```cpp
robot.clampStop();          // 保险起见先停
robot.clampDisconnect();
```

**注意**：断开后，之前的速度指令全部失效；下次要动必须重新 `clampConnect()`。

### `clampSetSpeed(axis, index, percent)` — 让某一台电机转

**干什么**：让指定电机按额定转速的百分比转。这是最底层的调速入口，
`x1Motor()` 之类的便捷函数最终都调它。

**参数**：

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `axis` | int | 0 / 1 / 2 | 0=X 轴，1=Y 轴，2=Z 轴 |
| `index` | int | 0 起 | 同一轴上的第几台，X1=0、X2=1 |
| `percent` | int | -100 ~ 100 | 正数正转，负数反转；100 = 额定转速 |

**注意**：正负方向最终由 ini 里该电机的 `sign` 决定，所以同一个 `+40`
在不同车上可能转向相反——这是配置问题，不是库的问题。

**调用后**：电机开始转，**不会自己停**。停有三种方式：

```cpp
robot.clampStopMotor(0, 0);   // 停这一台
robot.clampStop();            // 全停
robot.clampDisconnect();      // 停全部并断总线
```

**示例**（转 2 秒再停）：

```cpp
robot.clampConnect();
// 等 robot.clamp.connection 事件 connected=true
robot.clampSetSpeed(0, 0, 40);              // X1 正转 40%
QTimer::singleShot(2000, [&] {
  robot.clampStopMotor(0, 0);
});
```

**出错怎么办**：

| 返回 | 原因 | 处理 |
|---|---|---|
| `InvalidArgument` | axis 不是 0/1/2，或 index 超出 ini 里的电机数 | 用 `loadDriveConfig()` 看实际有几台 |
| `clamp_service_unavailable` | 没 `clampConnect()` | 先连接 |
| ok 但不动 | 电机被软限位挡住、或没上电 | 看事件 `robot.clamp.limitReached` |

### `x1Motor(p)` / `x2Motor(p)` — X 轴两台电机的快捷方式

**干什么**：等价于 `clampSetSpeed(0, 0, p)` 和 `clampSetSpeed(0, 1, p)`。
名字里的数字就是 ini 里的 `alias`（激光轮廓仪 / 探头间距）。

**什么时候用**：想让"激光相机摆动"（X1）或"探头间距"（X2）动的时候。

```cpp
robot.x1Motor(20);    // X1 正转 20%（激光相机摆动）
robot.x2Motor(-10);   // X2 反转 10%（探头间距收）
```

**注意**：`p` 是百分比，不是毫米；实际走多少毫米取决于 ini 里的 `gearRatio`。

### `z1Motor(p)` / `z2Motor(p)` — Z 轴两台电机的快捷方式

**干什么**：等价于 `clampSetSpeed(2, 0, p)` 和 `clampSetSpeed(2, 1, p)`。
对应"探头 1 升降"和"探头 2 升降"。

```cpp
robot.z1Motor(15);
robot.z2Motor(15);    // 两台要同向同速，否则探头会歪
```

**注意**：Z 轴的软限位通常是负方向（例如 `[-5707, -0.07]`），
撞到限位会收到 `robot.clamp.limitReached` 并自动停。

### `clampStopMotor(axis, index)` — 停某一台

**干什么**：只让指定那一台停，其它不动。

```cpp
robot.clampStopMotor(2, 0);   // 只停 Z1，Z2 继续
```

**什么时候用**："按住动、松开停"的按钮，松开时调它。

### `clampStop()` — 全部停

**干什么**：给所有夹爪电机发停止命令。

**什么时候用**：任何时候想立刻停；**程序异常退出前一定要调**（正常 `stop()` 也会清零运动，
但夹爪是独立总线，保险起见显式停一次）。

```cpp
robot.clampStop();
```

**注意**：它只停，不断总线；断了总线才需要重新 `clampConnect()`。

### `clampMoveToPosition(axis, index, degrees)` — 走到指定角度

**干什么**：让电机走到某个绝对角度（不是"再转多少度"）。

**参数**：

| 参数 | 说明 |
|---|---|
| `axis` / `index` | 同上，指定哪一台 |
| `degrees` | 目标角度，单位是**度**，不是百分比 |

```cpp
robot.clampMoveToPosition(2, 0, 120.0);   // Z1 走到 120°
```

**调用后**：主机下发定位命令，走完发事件 `robot.clamp.position`：

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.position")
    std::printf("[位置] %s\n", data.c_str());   // axis / index / degrees
});
```

**注意**：不是所有电机支持定位。不支持时返回 `position_unsupported`，
只能改用 `clampSetSpeed()` + 自己数时间。

### `clampRequestPosition(axis, index)` — 查某一台当前角度

**干什么**：请求主机回报这一台电机的位置。

```cpp
robot.clampRequestPosition(2, 0);
// 结果走事件 robot.clamp.position
```

**注意**：这是"查询"，不是"控制"；查询结果异步回来，不要在调用后立刻读变量。

### `clampRequestPositions()` — 查全部位置

**干什么**：一次查 4 台（或 ini 里配置的所有台）的位置。

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.clamp.position") std::printf("%s\n", data.c_str());
});
robot.clampRequestPositions();
```

**什么时候用**：界面初始化、定时刷新位置显示。

### `requestClampStatus()` — 查夹爪整体状态

**干什么**：让主机回一份夹爪状态（连接情况、每台电机状态、错误信息）。

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.clamp.status" && ok) std::printf("%s\n", data.c_str());
});
robot.requestClampStatus();
```

**注意**：即使夹爪没连接，这个查询也能回（会告诉你没连），适合做"连接前探测"。

### `requestClampSettings()` — 查夹爪配置

**干什么**：让主机回一份完整的底盘配置（夹爪只是其中一部分），
包括每台电机的协议、ID、减速比、软限位。

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.clamp.settings.get" && ok) std::printf("%s\n", data.c_str());
});
robot.requestClampSettings();
```

**什么时候用**：想知道"这台车到底配了几台电机、ID 是多少"，或者做配置界面。

**更省事的做法**：直接用 `requestDriveConfig()` + `setDriveConfigHandler`，
拿到的 `DriveConfig::clampMotors` 是解析好的结构体，不用自己解 JSON。

## A.6 摄像头（7 个方法，下面逐个讲）

> 两个通道：`slot 0` = 摄像头1，`slot 1` = 摄像头2。
> 图像走 UDP，回调里的 `data` 指针**只在回调期间有效**。

### `scanCameras()` — 枚举主机上的 USB 摄像头

**干什么**：让主机去扫一遍插在它身上的 USB 摄像头，列出设备号和名字。

**为什么必须先扫**：`connectCamera` 需要 `deviceIndex`（0、1、2…），
这个编号是主机侧枚举出来的，客户端猜不到。

**参数**：没有。

**调用后**：结果不是返回值，而是一个事件：

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.camera.devices") {
    std::printf("[摄像头列表] %s\n", data.c_str());
  }
});
robot.scanCameras();
```

**出错怎么办**：

| 现象 | 原因 | 处理 |
|---|---|---|
| 没有 `robot.camera.devices` 事件 | 主机没收到请求 | 确认 `isOnline()` |
| 列表为空 | 主机上没插 USB 摄像头，或被别的程序占用 | 在主机上打开"相机"应用看能不能出图 |

### `connectCamera(slot, deviceIndex, fps, flipH, flipV)` — 连 USB 摄像头

**干什么**：让主机打开指定 USB 摄像头，并开始往你这边推图。

**参数**：

| 参数 | 范围 | 说明 |
|---|---|---|
| `slot` | 0 / 1 | 你用哪一路显示（和物理摄像头无关，只是通道号） |
| `deviceIndex` | ≥0 | 来自 `scanCameras()` 的列表 |
| `fps` | 1~60 | 期望帧率，主机尽力而为 |
| `flipHorizontal` | bool | 左右镜像（装反了用） |
| `flipVertical` | bool | 上下镜像 |

```cpp
robot.scanCameras();                       // 先拿 deviceIndex
robot.connectCamera(0, 0, 15, true, false); // 摄像头1：设备0、15fps、左右翻转
```

**调用后**：主机会打开设备，成功后推事件 `robot.camera.connection`
（`connected=true`），然后开始出帧：

```cpp
robot.setCameraFrameHandler([](const pa1664::CameraFrame& f) {
  std::printf("cam%d %dx%d stride=%d fmt=%d\n", f.slot + 1, f.width,
              f.height, f.stride, int(f.format));
});
```

**注意**：`slot` 只是通道，不要求"slot 0 必须插第一个摄像头"；
两路可以任意对应，只要 `deviceIndex` 不同。

**出错怎么办**：

| 现象 | 原因 | 处理 |
|---|---|---|
| `invalid_argument` | slot 不是 0/1，或 deviceIndex < 0 | 用扫描结果 |
| `connected=false` + "被占用" | 主机上别的程序开着这个摄像头 | 关掉它 |
| 有事件但没帧 | fps 太高 / USB 带宽不够 | 降到 10 或换 USB 口 |

### `connectNetworkCamera(slot, url, flipH, flipV)` — 连网络摄像头

**干什么**：让主机去拉一路 RTSP/HTTP 视频流，再转发给你。

**参数**：

| 参数 | 说明 |
|---|---|
| `slot` | 0 / 1 |
| `url` | 完整地址，例如 `rtsp://192.168.1.104:554/stream1` |
| `flipH` / `flipV` | 翻转 |

```cpp
robot.connectNetworkCamera(1, "rtsp://192.168.1.104:554/stream1");
```

**注意**：

- 拉流是主机在做，所以**主机必须能访问那个 IP**（你的电脑能访问不代表主机能）；
- 地址里带账号密码时会被记进日志，注意别外发；
- 网络流延迟通常比 USB 大（编码 + 解码），对实时性要求高的场景用 USB。

### `setCameraDisplaySize(slot, width, height)` — 改下行分辨率

**干什么**：告诉主机"按这个尺寸缩好再发给我"，减少带宽和解码压力。

**参数**：`slot` 0/1；`width`、`height` 1~8192。

```cpp
robot.setCameraDisplaySize(0, 640, 480);
```

**注意**：这是**主机侧缩放**，不是改摄像头硬件分辨率；
改完只影响后续帧，已经收到的帧不变。

### `requestCameraStatus()` — 查摄像头状态

**干什么**：让主机回一份当前状态（哪路连着、什么分辨率、丢帧数）。

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.camera.status" && ok) std::printf("%s\n", data.c_str());
});
robot.requestCameraStatus();
```

**什么时候用**：界面初始化、排查"为什么没画面"。

### `disconnectCamera(slot)` — 断开某一路

**干什么**：关闭指定通道的摄像头，停止推流。

```cpp
robot.disconnectCamera(0);
```

**注意**：另一路不受影响；再次使用要重新 `connectCamera`。

### `disconnectAllCameras()` — 断开全部

**干什么**：两路一起关。

```cpp
robot.disconnectAllCameras();
```

**什么时候用**：程序退出前、切换工作模式时。

## A.7 超声采集与参数（11 个方法，下面逐个讲）

> 主机启动时**已经自动连上设备并开始采集**，所以这一组里连/断开/启动/停止
> 平时都不用调。真正要用的只有"读状态、读参数、写参数"。

### `ultrasoundConnect(address, deviceId)` — 连接超声设备

**干什么**：让主机的超声 SDK 去连板卡。

**参数**：`address` 默认 `"127.0.0.1"`（本机 SDK 服务），`deviceId` 默认 0。
正常情况下**不需要改**。

**什么时候用**：主机报 `hardwareOnline=false` 且长时间不恢复时，手动重连一次。

```cpp
robot.ultrasoundConnect();     // 一般不用调
```

**注意**：主机启动时已经自动连了，重复调通常无害但不必要。

### `ultrasoundDisconnect()` — 断开超声设备

**干什么**：断开 SDK 与板卡的连接，采集停止。

```cpp
robot.ultrasoundDisconnect();
```

**注意**：断开后再连、再启动采集，主机要走一遍重新枚举，**约 30 秒才恢复**。
不是必须的场景别调。

### `ultrasoundStartAcquisition()` — 开始采集

**干什么**：让主机开始从板卡收数据并解码。

**参数**：没有。

**什么时候用**：你自己 `ultrasoundStopAcquisition()` 过，想恢复时。

```cpp
robot.ultrasoundStartAcquisition();
```

**注意**：开始后要等 `ultrasound.status` 里 `capturing=true` 才会出帧；
设备没就绪时会排队（`startPending=true`），不是失败。

### `ultrasoundStopAcquisition()` — 停止采集

**干什么**：让主机停止采集，探头不再发波。

**参数**：没有。

```cpp
robot.ultrasoundStopAcquisition();
```

**重要提醒**：**停了之后恢复很慢**。实测主机需要重新初始化设备，
大约 30 秒 `fps` 才回到正常。所以：

- 只是想"暂时不要数据"→ 别停采集，让你的回调直接 `return` 就行；
- 只有要拔线、换探头、长时间不用时才停。

### `requestUltrasoundStatus()` — 查设备状态

**干什么**：主动问一次超声状态（也可以靠 1 Hz 的状态事件自动收）。

```cpp
robot.setUltrasoundStatusHandler([](const pa1664::UltrasoundStatus& s) {
  std::printf("服务=%d 硬件=%d 采集=%d 点数=%d 波束=%d 幅值=%d 帧=%.0f\n",
              s.serviceConnected, s.hardwareOnline, s.capturing, s.pointCount,
              s.beamCount, s.maxAmplitudeValue, s.totalFrames);
});
robot.requestUltrasoundStatus();
```

**字段含义**：

| 字段 | 含义 | false/0 说明什么 |
|---|---|---|
| `serviceConnected` | SDK 服务起来了 | 主机刚启动的几秒 |
| `hardwareOnline` | USB 板卡在线 | 线没插好、板卡没上电 |
| `capturing` | 正在采集 | 没开始，或设备掉了 |
| `captureReady` | 硬件已就绪可开始 | 正在初始化 |
| `pointCount` | 每条波束采样点数 | 本机实测 1000（不是默认 1024） |
| `beamCount` | 波束数 | 本机 1 |
| `maxAmplitudeValue` | 幅值满量程 | 本机 2048 |
| `currentGroup` | 当前工作组 | 一般 0 |
| `totalFrames` | 累计帧数 | 不增长=没出数据 |

### `ultrasoundParameterSnapshot()` — 一次读全部参数

**干什么**：让主机把 84 项参数全部读一遍给你。

**参数**：没有。结果在响应回调里：

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op != "ultrasound.parameters.snapshot" || !ok) return;
  // data 形如 {"values":{...},"errors":[],"complete":true}
  std::printf("%s\n", data.c_str());
});
robot.ultrasoundParameterSnapshot();
```

**注意**：这是 84 次 SDK 读，**采集进行中调它会和 USB 数据抢 SDK 锁**，
可能把主机超声线程堵住（表现为后续请求全部超时）。建议先停采或空闲时读。

### `ultrasoundGetParameter(method, argsJson)` — 读一个参数

**干什么**：按 SDK 方法名读单个（或几个）参数。

**参数**：

| 参数 | 格式 | 例子 |
|---|---|---|
| `method` | SDK 的 getter 名 | `"getGain"` |
| `argsJson` | JSON 数组字符串，一般是 `[组号]` | `"[0]"` |

```cpp
robot.ultrasoundGetParameter("getGain", "[0]");
// 响应： {"values":{"getGain:WzBd":13}}
```

**注意**：返回的 key 里 `:WzBd` 是 `[0]` 的 base64，所以同一个参数不同组号
会有不同的 key。

### `ultrasoundSetParameter(setMethod, argsJson, readbackMethod, readbackArgsJson)` — 写一个参数

**干什么**：写参数并自动回读，确认真的写进去了。

**参数**：

| 参数 | 说明 |
|---|---|
| `setMethod` | setter 名，如 `"setGain"` |
| `argsJson` | setter 参数，格式 `[值, 组号]`，如 `"[40,0]"` |
| `readbackMethod` | 可省略；省略时自动 `setGain → getGain` |
| `readbackArgsJson` | 可省略；省略时自动保留最后一个参数（组号） |

```cpp
robot.ultrasoundSetParameter("setGain", "[40,0]");
// 响应： {"readbackKey":"getGain:WzBd","readbackValue":40}
```

**调用后**：主机停采 → 写参数 → 回读 → 等你调 `ultrasoundParametersSettled()` 恢复采集。

**注意**：回读值和写入值不一致，说明参数名写错或值超范围，别当成功。

### `ultrasoundApplyParameters(changesJson)` — 批量写

**干什么**：一次下发多个改动，只重配一次硬件（比逐个写快、对设备更友好）。

**参数**：一个 JSON 数组字符串：

```json
[
  {"method":"setGain","args":[40,0]},
  {"method":"setRangeEnd","args":[60.0,0]}
]
```

```cpp
robot.ultrasoundApplyParameters(R"([
  {"method":"setGain","args":[40,0]},
  {"method":"setRangeEnd","args":[60.0,0]}
])");
robot.ultrasoundParametersSettled(true);
```

**注意**：数组里任意一项失败，整体会回失败原因；看响应里的 `readbacks`/`errors`。

### `ultrasoundParametersSettled(resumeCapture)` — 让参数生效并恢复采集

**干什么**：告诉主机"我改完了，去应用并恢复采集"。

**参数**：`resumeCapture=true` 恢复采集；`false` 保持停采（只在你想连续改参数时用）。

```cpp
robot.ultrasoundParametersSettled(true);
```

**为什么必须调**：写参数时主机已经停采了，不调这一句，**采集不会自己恢复**，
你会以为"参数写坏了没数据"。

**注意**：恢复不是瞬间的，等 `ultrasound.status` 里 `capturing=true` 再判断。

### `configureUltrasoundDecoder(cfg)` — 手动配置解码器

**干什么**：告诉主机"超声数据有多少点、几条波束、什么量程"，主机据此把原始包
解成 A 扫。

**参数**：`UltrasoundDecoderConfig`：

| 字段 | 怎么填 |
|---|---|
| `groupId` | 用 `status.currentGroup`（一般 0） |
| `pointCount` | 用 `status.pointCount`（本机 1000） |
| `beamCount` | 用 `status.beamCount`（本机 1） |
| `amplitudeScale` | 用 `status.maxAmplitudeValue`（本机 2048） |
| `selectedBeam` | 默认 0 |
| `scanModes` | `0x1`=只 A 扫，`0x3`=A+B，`0x7`=A+B+C |
| `rangeStart` / `rangeEnd` | 显示深度范围（mm） |

```cpp
pa1664::UltrasoundDecoderConfig cfg;
cfg.groupId = status.currentGroup;
cfg.pointCount = status.pointCount;      // 千万别写死 1024
cfg.beamCount = status.beamCount;
cfg.amplitudeScale = status.maxAmplitudeValue;
cfg.scanModes = 0x1;
cfg.rangeStart = 0.0;
cfg.rangeEnd = 50.0;
robot.configureUltrasoundDecoder(cfg);
```

**一般不用调**：库收到 `ultrasound.status` 后会自动按真实几何下发一次。
只有你要改扫描模式/量程、或者库的自动配置被关掉时才手动调。

**注意**：配错（比如 `pointCount` 写 1024 而设备是 1000）就**一帧都收不到，
而且不报错**——这是 A 扫最常见的"没数据"原因。

## A.8 超声预设 / 精度 / 标定（11 个方法，下面逐个讲）

> 这三类东西都存在**主机**上（`parameter_presets.json` + `default_config.json`），
> 掉电保留，换一台客户端不影响。

### `requestUltrasoundPresets()` — 列出所有预设

**干什么**：让主机回一份预设清单和当前选中项。

**参数**：没有。响应里有这些字段：

| 字段 | 含义 |
|---|---|
| `presets` | 数组，每项 `{id, name}` |
| `currentPreset` | 当前用的是哪个 |
| `scanPrecisionMmPerCell` | 当前预设的 mm/格 |
| `countsPerMmA` / `countsPerMmB` | 编码器换算系数 |
| `wheelDiameterMm` / `encoderResolution` | 编码器硬件参数 |
| `configBase64` | 整套硬件配置快照（base64 JSON） |

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "ultrasound.presets.list" && ok) std::printf("%s\n", data.c_str());
});
robot.requestUltrasoundPresets();
```

### `switchUltrasoundPreset(presetId)` — 切到某个预设

**干什么**：把主机上一整套参数（增益、闸门、量程、楔块…）一次性应用到设备。

**参数**：`presetId` ≥ 0，来自预设列表。

```cpp
robot.switchUltrasoundPreset(0);
```

**调用后**：主机会重配硬件，**期间采集中断几秒**，然后自动恢复；
恢复后建议重新读一次状态确认 `capturing=true`。

**注意**：切换会覆盖当前所有参数，切之前如果想保留现场值，先
`captureUltrasoundPreset()` 存一份。

### `captureUltrasoundPreset(presetId)` — 把当前参数存成预设

**干什么**：把设备现在的参数（84 项）保存到指定预设 ID 里。

**参数**：`presetId` ≥ 0。注意：**不要随便用已有 ID**，会覆盖原预设。

```cpp
robot.captureUltrasoundPreset(0);    // 用当前参数覆盖预设 0
```

**什么时候用**：调好一套参数后想长期保存（否则主机重启会被预设文件覆盖回去）。

### `requestUltrasoundScanPrecision()` — 读扫查精度

**干什么**：读"C 扫一格代表多少毫米"，存在主机预设里。

**参数**：没有。响应 `{"presetId":0,"mmPerCell":5.0}`。

```cpp
robot.requestUltrasoundScanPrecision();
// 库会把 mmPerCell 缓存下来，之后可直接 scanPrecisionMmPerCell()
```

### `setUltrasoundScanPrecision(presetId, mmPerCell)` — 写扫查精度

**干什么**：设置某个预设的网格精度。

**参数**：

| 参数 | 范围 | 说明 |
|---|---|---|
| `presetId` | ≥0 | 改哪个预设 |
| `mmPerCell` | >0 | 每格多少毫米，本机默认 5 |

```cpp
robot.setUltrasoundScanPrecision(0, 5.0);
```

**注意**：它只改预设文件，不影响正在采集的数据；换算位置时要用新值。

### `scanPrecisionMmPerCell()` — 取缓存的 mm/格

**干什么**：返回最近一次读到的精度，避免每次换算都发请求。

```cpp
const double mmPerCell = robot.scanPrecisionMmPerCell();
const double posMm = frame.cColumn * mmPerCell;
```

**注意**：没读过 `requestUltrasoundScanPrecision()` 时返回 0，
拿 0 去乘会得到 0，看起来像"位置一直是 0"。

### `requestUltrasoundEncoderCalibration()` — 读编码器标定

**干什么**：读编码器的换算参数。

**参数**：没有。响应：

```json
{"wheelDiameterMm":500,"resolution":4096,"calibrationMode":0,
 "countsPerMmA":2.6076,"countsPerMmB":2.6076}
```

| 字段 | 含义 |
|---|---|
| `wheelDiameterMm` | 测量轮直径（mm） |
| `resolution` | 编码器分辨率（每转计数） |
| `calibrationMode` | 0=按轮径算，1=手工填 counts/mm |
| `countsPerMmA/B` | A/B 轴每毫米多少计数 |

```cpp
robot.requestUltrasoundEncoderCalibration();
```

### `setUltrasoundEncoderCalibration(json)` — 写编码器标定

**干什么**：改编码器换算参数，让"计数 → 毫米"算得准。

**参数**：JSON 字符串，字段同上：

```cpp
robot.setUltrasoundEncoderCalibration(R"({
  "wheelDiameterMm": 500,
  "resolution": 4096,
  "calibrationMode": 0,
  "countsPerMmA": 2.6075945876176134,
  "countsPerMmB": 2.6075945876176134
})");
```

**注意**：写错会让 C 扫比例整体偏差（画出来的图被拉长或压扁），
改完先用一段已知长度验证一次。

### `encoderCountsPerMm(axis)` — 取缓存的 counts/mm

**干什么**：返回 A（axis 0）或 B（axis 1）轴每毫米多少计数。

```cpp
const double countsPerMm = robot.encoderCountsPerMm(0);
const double positionMm = frame.encoders[0] / countsPerMm;
```

**注意**：没读过标定前返回 0，做除法会得到 `inf`，先判断 `> 0`。

### `ultrasoundResetEncoder()` — 编码器清零

**干什么**：把当前编码器计数当作"0 点"。

```cpp
robot.ultrasoundResetEncoder();
```

**什么时候用**：开始一次新的扫查前，想让位置从 0 开始。

**注意**：清零后 `frame.encoders[]` 从 0 起算，之前的绝对位置信息就没了。

### `ultrasoundRefreshGeometry()` — 刷新几何

**干什么**：让主机重新计算"编码器计数 → 扫查位置"的几何关系
（涉及 `encoderPrecisionA/B`、`geometryRevision`）。

```cpp
robot.ultrasoundRefreshGeometry();
```

**什么时候用**：换了探头、改了编码器精度、或多组配置改过之后。

**注意**：刷新期间采集可能短暂停顿，属正常。

## A.9 本地配置镜像（9 个方法，下面逐个讲）

> 库会在 `<你的exe>\config\pa1664_profile.json` 里记一份"上次看到的配置"。
> 这个文件**只存在你自己的电脑上**，跟主机无关。

### `setProfileOptions(options)` — 改镜像行为

**干什么**：控制这个本地文件怎么用。

**参数**：`ProfileOptions`：

| 字段 | 默认 | 说明 |
|---|---|---|
| `enabled` | true | false = 完全不读写本地镜像 |
| `path` | 空 | 空 = `<exe>\config\pa1664_profile.json` |
| `autoApplyDecoder` | true | 设备就绪后自动下发解码器 |
| `mirrorRemote` | true | 远端配置变化时写本地 |
| `autoSnapshotUltrasound` | true | 设备就绪且未采集时自动读 84 项 |

```cpp
pa1664::ProfileOptions opts;
opts.enabled = false;            // 不想在本机留任何记录
robot.setProfileOptions(opts);
```

**什么时候用**：程序装在不该留配置的机器上；或者要自定义文件路径。

### `profileOptions()` — 看当前设置

**干什么**：返回上面那个结构体。

```cpp
if (robot.profileOptions().enabled) std::printf("镜像已开启\n");
```

### `profilePath()` — 看文件实际位置

**干什么**：返回 `<exe>\config\pa1664_profile.json` 的绝对路径。

```cpp
std::printf("本地记录：%s\n", robot.profilePath().c_str());
```

**注意**：这个文件里有主机的串口、RTSP 地址，**不要提交到仓库或外发**。

### `hasDriveProfile()` / `hasUltrasoundProfile()` — 有没有缓存

**干什么**：分别判断本地文件里有没有底盘配置 / 超声参数。

```cpp
std::printf("底盘=%d 超声=%d\n", robot.hasDriveProfile(),
            robot.hasUltrasoundProfile());
```

**什么时候用**：界面启动时决定"显示上次的值"还是"显示空"。

### `reloadProfile()` — 从文件重新读

**干什么**：把磁盘上的 JSON 重新读进内存（不联网）。

```cpp
robot.reloadProfile();
```

**什么时候用**：你手动改了文件、或者别的进程写了这个文件。

**注意**：读不到文件时返回 `InvalidArgument`，不是崩溃。

### `saveProfile()` — 立即写盘

**干什么**：把内存里的镜像立刻写到文件（平时是自动写的，这个用于强制落盘）。

```cpp
robot.saveProfile();
```

**什么时候用**：程序退出前想确保写进去了；或者调试时手动保存。

### `applyDecoderProfile()` — 重发保存的解码器配置

**干什么**：把本地记着的解码器配置再发给主机一次。

```cpp
robot.applyDecoderProfile();
```

**什么时候用**：A 扫收不到帧、又确认设备在线时，试一次；
或者主机的自动配置被你关掉了。

**注意**：本地没有保存过解码器配置时返回 `InvalidArgument`。

### `applyDriveProfileToHost()` — 把缓存配置推回主机

**干什么**：把本地镜像里的底盘配置（主机 ini 原文）重新下发给主机。

```cpp
robot.applyDriveProfileToHost();
```

**注意**：

- 用的是**缓存**（上次 `requestDriveConfig()` 的结果），可能已经过期，别乱推；
- 主机的 `adoptHostPorts()` 会保留自己的 COM/PCAN 口，不会被你改瞎；
- 一般不需要调，只在"主机配置被改坏了，想恢复上次备份"时用。

## A.10 外围：激光相机 / IMU / 串口（9 个方法，下面逐个讲）

> 激光相机是**自动纠偏的前置条件**；IMU 和编码器一起参与扫查轨迹；
> 串口列表用于配置界面。这三样都不影响底盘能不能动。

### `scanLaserDevices()` — 枚举激光相机

**干什么**：让主机去扫插在它身上的激光轮廓相机，列出序列号。

**为什么需要**：`connectLaser` 要序列号，序列号是主机枚举出来的。

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.laser.devices") std::printf("[激光] %s\n", data.c_str());
});
robot.scanLaserDevices();
```

**出错怎么办**：列表为空 → 相机没插、没供电、或被别的程序占用。

### `connectLaser(serialNumber)` — 连接激光相机

**干什么**：按序列号打开指定相机，开始出轮廓。

**参数**：`serialNumber`，字符串，必须和主机上那台一致
（例如 `"DB2666828"`，从 `scanLaserDevices` 的列表里拿）。

```cpp
robot.connectLaser("DB2666828");
robot.setLaserProfileHandler([](const pa1664::LaserProfileFrame& f) {
  std::printf("轮廓 %d 点 valid=%d\n", f.pointCount, f.valid);
});
```

**调用后**：推事件 `robot.laser.connection`（`connected=true`），然后开始出帧
（约 30 fps）。

**注意**：序列号写错会一直连不上；**纠偏必须它先连上**，否则
`startAutoCorrection` 返回 `laser_not_connected`。

### `disconnectLaser()` — 断开激光相机

```cpp
robot.disconnectLaser();
```

**注意**：断开后自动纠偏会立刻停止（激光是它的输入）。

### `requestLaserStatus()` — 查激光相机状态

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.laser.status" && ok) std::printf("%s\n", data.c_str());
});
robot.requestLaserStatus();
```

**什么时候用**：轮廓收不到时先查这个，确认是"没连"还是"连了但没数据"。

### `connectImu(port, baudRate, outputDivider)` — 连接 IMU

**干什么**：打开主机上的 IMU 串口，开始收姿态数据。

**参数**：

| 参数 | 范围 | 说明 |
|---|---|---|
| `port` | 串口名，如 `"COM3"` | 必须存在（可先用 `requestSerialPorts()` 查） |
| `baudRate` | 1200~4000000 | 常用 115200 |
| `outputDivider` | 1~200 | 分频：IMU 每 N 帧取 1 帧 |

```cpp
robot.connectImu("COM3", 115200, 10);
```

**调用后**：推事件 `robot.imu.connection`，成功后 IMU 数据参与扫查轨迹。

**出错怎么办**：

| 返回 | 原因 | 处理 |
|---|---|---|
| `invalid_argument` | 串口为空 / 波特率越界 / 分频越界 | 按上表范围改 |
| `connected=false` | 串口被占、线没接、波特率不对 | 换个口试，或问硬件负责人 |

### `disconnectImu()` — 断开 IMU

```cpp
robot.disconnectImu();
```

**注意**：断开后扫查轨迹会缺少姿态补偿，但底盘/超声采集不受影响。

### `requestImuStatus()` — 查 IMU 状态

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.imu.status" && ok) std::printf("%s\n", data.c_str());
});
robot.requestImuStatus();
```

### `requestPeripheralsSnapshot()` — 一次拿所有外围状态

**干什么**：让主机回一份"外围大礼包"：夹爪、摄像头、激光、IMU、纠偏的当前状态。

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.peripherals.snapshot" && ok) std::printf("%s\n", data.c_str());
});
robot.requestPeripheralsSnapshot();
```

**什么时候用**：程序刚连上时做一次全量同步，比一个个查快。

### `requestSerialPorts()` — 列出主机串口

**干什么**：让主机把本机可用串口列出来（含描述），常用于配置界面下拉框。

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string&, const std::string&,
                            const std::string& data) {
  if (op == "robot.peripherals.serialPorts" && ok)
    std::printf("%s\n", data.c_str());
});
robot.requestSerialPorts();
```

**注意**：列出来的是**主机上的串口**，不是你这台电脑的；给 IMU/夹爪/驱动轮
选口时只能用这里的名字。

## A.11 通用请求与回调（12 个方法，下面逐个讲）

### `request(operation, dataJson)` — 下发任意底层指令

**干什么**：直接发一条主机支持的 `operation`，绕过封装。

**参数**：

| 参数 | 说明 |
|---|---|
| `operation` | 指令名，见附录 B，如 `"robot.camera.scan"` |
| `dataJson` | JSON 对象字符串，没有参数写 `"{}"` |

**返回**：请求号（`uint64`）。**返回 0 表示没发出去**（没连上或缓冲满），
不是"失败原因"——原因在响应回调里。

```cpp
const std::uint64_t id = robot.request("robot.peripherals.serialPorts");
if (id == 0) std::printf("没发出去，先检查连接
");
```

**什么时候用**：附录 B 里有、但库里没封装成函数的操作（预设导入导出、
参数提交、传感器扫描等）。

### `setConnectionHandler(fn)` — 连接状态回调

**干什么**：会话连上/断开时通知你。

**回调签名**：`(bool online, const std::string& detail)`

```cpp
robot.setConnectionHandler([](bool online, const std::string& detail) {
  std::printf("%s：%s
", online ? "已连上" : "已断开", detail.c_str());
  if (online) {
    // 这里才是可以发指令的时候
  }
});
```

**注意**：断线会自动重连，重连成功会**再回调一次** `online=true`；
用标志位避免重复初始化。

### `setStatusHandler(fn)` — 遥测回调（最常用）

**干什么**：主机以约 20~50 Hz 推送底盘状态。

**回调签名**：`(const RobotStatus& status)`

```cpp
robot.setStatusHandler([](const pa1664::RobotStatus& s) {
  std::printf("state=%s 目标=%.3f 实际=%.3f 左=%.3f 右=%.3f
",
              s.stateText.c_str(), s.targetLinearMps, s.appliedLinearMps,
              s.leftWheelMps, s.rightWheelMps);
});
```

**注意**：频率很高，回调里**不要做耗时操作、不要每帧刷 UI**，
否则会把界面拖卡。建议节流到 2~5 Hz。

### `setResponseHandler(fn)` — 请求应答回调

**干什么**：每个请求的返回都从这里来（成功/失败/reject 都在这里）。

**回调签名**：
`(const std::string& operation, bool ok, const std::string& errorCode,
const std::string& errorText, const std::string& dataJson)`

```cpp
robot.setResponseHandler([](const std::string& op, bool ok,
                            const std::string& code, const std::string& text,
                            const std::string& data) {
  if (!ok) std::printf("失败 %s [%s] %s
", op.c_str(), code.c_str(),
                       text.c_str());
});
```

**注意**：`dataJson` 是 JSON 文本，用 `QJsonDocument` 或你喜欢的库解析；
不要假设它一定是对象（有些指令回空 `{}`）。

### `setEventHandler(fn)` — 主机主动上报事件

**干什么**：接收主机主动推的事件（状态变化、位置、连接、扫描结果等）。

**回调签名**：`(const std::string& event, const std::string& dataJson)`

```cpp
robot.setEventHandler([](const std::string& name, const std::string& data) {
  if (name == "robot.stateChanged")
    std::printf("[状态变化] %s
", data.c_str());
});
```

**事件清单**见附录 C。注意区分：**事件是主机推的，响应是你请求的回复**。

### `setDriveConfigHandler(fn)` — 底盘配置回调

**干什么**：`requestDriveConfig()` 的结果从这里给，已解析成结构体。

**回调签名**：`(const DriveConfig& config)`

```cpp
robot.setDriveConfigHandler([](const pa1664::DriveConfig& c) {
  if (!c.loaded) { std::printf("没拿到配置：%s
", c.error.c_str()); return; }
  robot.applyDriveConfig(c);
});
robot.requestDriveConfig();
```

**注意**：`loaded=false` 时其它字段都是默认值，别直接用。

### `setCameraFrameHandler(fn)` — 摄像头帧回调

**回调签名**：`(const CameraFrame& frame)`

```cpp
robot.setCameraFrameHandler([](const pa1664::CameraFrame& f) {
  // f.data 只在回调内有效，要保存必须 copy
});
```

**注意**：`data` 指针回调返回后立刻失效；`stride` 可能大于 `width`，
按行拷贝时要用 stride。

### `setLaserProfileHandler(fn)` — 激光轮廓回调

**回调签名**：`(const LaserProfileFrame& frame)`

```cpp
robot.setLaserProfileHandler([](const pa1664::LaserProfileFrame& f) {
  // f.coordinates 是 pointCount*3 个 float，仅在回调内有效
});
```

**注意**：约 30 fps；画图见 6.4；没连激光相机不会有任何回调。

### `setUltrasoundFrameHandler(fn)` — A 扫回调

**回调签名**：`(const UltrasoundAScanFrame& frame)`

```cpp
robot.setUltrasoundFrameHandler([](const pa1664::UltrasoundAScanFrame& f) {
  // f.beams[beam][point]，f.encoders[0..4]，f.measurements
});
```

**注意**：`beams`/`measurements` 也是回调内有效；需要留存就拷 `qint16` 数组。

### `setCorrectionStatusHandler(fn)` — 纠偏状态回调

**回调签名**：`(const CorrectionStatus& status)`

```cpp
robot.setCorrectionStatusHandler([](const pa1664::CorrectionStatus& s) {
  std::printf("active=%d phase=%s 速度=%.4f
", s.active, s.phase.c_str(),
              s.linearCommandMps);
});
```

**注意**：`phase` 是字符串（preparing/tracking/paused/stopped），别用数字比较。

### `setUltrasoundStatusHandler(fn)` — 超声状态回调

**回调签名**：`(const UltrasoundStatus& status)`

```cpp
robot.setUltrasoundStatusHandler([](const pa1664::UltrasoundStatus& s) {
  if (!s.hardwareOnline) return;      // 设备不在线时别做参数操作
  std::printf("采集=%d 点数=%d 帧=%.0f
", s.capturing, s.pointCount,
              s.totalFrames);
});
```

**注意**：约 1 Hz 推送，另有主动查询（`requestUltrasoundStatus()`）也会触发它。

### `setLogHandler(fn)` — 库内部日志

**回调签名**：`(LogLevel level, const std::string& message)`

```cpp
robot.setLogHandler([](pa1664::LogLevel level, const std::string& text) {
  if (level == pa1664::LogLevel::Error) std::printf("[ERR] %s
", text.c_str());
});
```

**注意**：不清空的话，库自己也会写 `<exe>\logs
obot_console.log`；
这个回调是给你自己接管日志用的。

## A.12 自由函数（1 个）

### `pa1664::loadDriveConfig(path)` — 读本地 ini

**干什么**：读 `drive_settings.ini`，解析成 `DriveConfig`（不联网）。

**参数**：`path`，空字符串表示 `<exe>\config\drive_settings.ini`。

```cpp
auto cfg = pa1664::loadDriveConfig();
if (!cfg.loaded) std::printf("读不到：%s
", cfg.error.c_str());
```

**和 `requestDriveConfig()` 的选择**：

| 情况 | 用哪个 |
|---|---|
| 程序跑在主机那台电脑上 | 都可以 |
| 程序在另一台电脑 | 只能 `requestDriveConfig()`（主机网络回传） |
| 只想验证 ini 格式 | `loadDriveConfig()` |

**注意**：读的是**你这台机器上的文件**，不是主机的；分布式部署时别用它当
"主机配置"。

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
| `ultrasound.scan.precision.get` | `{"presetId":0}` → `{"presetId":0,"mmPerCell":5.0}` |
| `ultrasound.scan.precision.set` | `{"presetId":0,"mmPerCell":5.0}` |
| `ultrasound.encoder.calibration.get` | `{}` → `{"wheelDiameterMm":500,"resolution":4096,"calibrationMode":0,"countsPerMmA":2.6076,"countsPerMmB":2.6076}` |
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

---

# 附录 D. 编译与运行报错速查

## D.1 编译期

| 报错 | 真正的原因 | 怎么办 |
|---|---|---|
| `C1083 无法打开源文件 pa1664_robot_api.h` | 头文件路径没加 | `.pro` 加 `INCLUDEPATH += .../include` |
| `C2039 "RobotApi" 不是 "pa1664" 的成员` | include 了别的同名头，或命名空间拼错 | 确认 `#include "pa1664_robot_api.h"`，调用写 `pa1664::RobotApi` |
| `C2065 未声明的标识符 RobotApi` | 忘了 include | 同上 |
| `C2664 无法将参数 1 从 X 转换为 Y` | 传了 `QString`/`char*` 给 `std::string` 参数 | 用 `std::string`，或 `.toStdString()` / 直接字面量 |
| `C4996 strcpy 不安全` | 用了 CRT 不安全函数 | 换 `std::string` / `snprintf` |
| `C4251 class 需要 DLL 接口` | 用动态库时的已知误报 | 头文件已用 `#pragma warning` 抑制；若仍出现，检查是否定义错宏 |

## D.2 链接期

| 报错 | 原因 | 解决 |
|---|---|---|
| `LNK2019 无法解析的外部符号 "public: ... RobotApi::..."` | 没链接 `.lib` | `LIBS += .../PA1664RobotApi.lib` |
| `LNK2019 ... __imp_?forward@RobotApi...` | 静态方式定义了 `PA1664_ROBOT_API_DLL` | 删掉该宏 |
| `LNK2019 ... loadDriveConfig` | 同上（没链接库） | 同上 |
| `LNK1104 无法打开文件 ...lib` | 路径错/文件被占 | 检查路径与文件是否存在 |
| `LNK1181 无法打开输入文件` | 路径含中文或空格 | 换纯英文路径，或用 `/` 分隔 |
| `LNK2038 RuntimeLibrary 不匹配` | 编译选项 `/MT` vs `/MD` | 工程改成 `/MD` |
| `LNK2001 无法解析的外部符号 __std_*` | 混用了不同 VS 版本编的库 | 全用 VS2022 重编 |

## D.3 启动就崩 / 找不到 DLL

| 现象 | 原因 | 解决 |
|---|---|---|
| 提示找不到 `PA1664RobotApi.dll` | 用动态库但没拷 DLL | 把 DLL 放到 exe 同目录 |
| 提示找不到 `Qt6Core.dll` 等 | Qt 的 bin 不在 PATH | 加 PATH，或 `windeployqt your.exe` |
| `0xc000007b` | 32/64 位混用 | 全部用 x64 重编 |
| 一启动就崩，栈在 `std::string` 析构 | CRT 不匹配（`/MT` 或不同 VS） | 统一 `/MD` + 同一 VS |
| 双击 exe 一闪而过 | 控制台程序没停留 | 用 `cmd` 运行，末尾 `getchar()` |

## D.4 运行期（网络与业务）

| 现象 | 含义 | 处理 |
|---|---|---|
| `NotOnline 未连接小车主机` | 会话没建好 | 等 `setConnectionHandler(true)` |
| `host.busy` | 已有客户端占用 | 关掉工作台或另一个程序 |
| `timeout` | 主机 5 秒没回 | 重试；超声参数类请求先停采 |
| `invalid_settings` | 配置项不合法 | 看 `errorText`，逐项对照 |
| `request_rejected` | 主机拒绝（状态不符） | 检查前置条件（使能/连接） |
| 断线后一直没恢复 | 网络或主机挂了 | 看主机进程，库会自动重连 |
| 中文乱码 | 控制台编码 | `chcp 65001`，源文件存 UTF-8 |

---

# 附录 E. 术语表

| 词 | 人话解释 |
|---|---|
| **主机 / Host** | 小车那台电脑上的 `PA1664RobotHost.exe`，所有硬件（CAN、摄像头、超声、激光）都接在它上面 |
| **客户端** | 你写的程序，通过网线连主机 |
| **控制权 / 会话** | 同一时刻只允许一个客户端控制，后来者收到 `host.busy` |
| **使能（Enable）** | 给轮子电机通电并保持。没使能时速度指令被忽略且不报错 |
| **租约（200 ms）** | 速度指令每 50 ms 自动重发；超过租约没新指令，主机自动停车 |
| **斜坡** | 主机把目标速度逐渐加上去，不是瞬间跳变；`applied` 就是斜坡后的实际值 |
| **A 扫** | 一条深度波形：横轴是深度，纵轴是回波幅值 |
| **B 扫** | 多条 A 扫按位置并排，形成二维剖面图 |
| **C 扫** | 在工件表面按网格取点，形成俯视图（平面图） |
| **闸门（Gate A/B/C/I）** | 在深度轴上开一个窗口，测量窗口里的回波幅值和位置 |
| **编码器** | 记录探头走了多远的光栅/磁栅，`counts` 是原始计数 |
| **counts/mm** | 每毫米多少计数，用来把编码器计数换成毫米 |
| **mm/格** | C 扫网格一格代表多少毫米 |
| **解码器配置** | 告诉主机"多少点、几条波束"，不配就收不到 A 扫，且不报错 |
| **预设（Preset）** | 主机上保存的一整套超声参数，掉电保留 |
| **profile（轮廓）** | 激光打到工件上得到的一条截面线，`(x,z)` 点集 |
| **纠偏 / 循迹** | 主机根据轮廓找焊缝中心，自动调整车头方向 |
| **媒体流** | 图像、A 扫、激光轮廓走的 UDP 通道（43102） |
| **批量流** | C 扫批次走的 TCP 通道（43103），保证顺序 |
| **HMAC** | 协议帧的签名，防止乱发指令；库内部处理，不用管 |
| **pimpl** | 库的实现细节藏在 .cpp 里，头文件只暴露指针，所以升级库不用改你的代码 |

---

# 附录 F. 上线前检查清单

## F.1 环境

- [ ] 编译器：VS2022 x64，运行时库 `/MD`
- [ ] Qt：6.8.3 MSVC2022 64bit（与库一致）
- [ ] 工程路径无中文、无空格
- [ ] 静态库方式：没有定义 `PA1664_ROBOT_API_DLL`
- [ ] 动态库方式：定义了宏，且 exe 目录有 `PA1664RobotApi.dll` + Qt 三个 DLL

## F.2 通信

- [ ] `ping` 主机通
- [ ] 主机进程在跑
- [ ] 只有你的程序在连（工作台已关）
- [ ] `setConnectionHandler` 收到 `online=true`
- [ ] `isOnline()`、`mediaReady()`、`bulkReady()` 都是 true

## F.3 安全

- [ ] 程序异常退出时车能停：靠 200 ms 租约自我验证过一次（任务管理器杀掉进程看车是否停）
- [ ] 急停按钮/接口可用：`emergencyStop()` 之后 `state == EmergencyStop`
- [ ] 软限位已配置（夹爪限位、超声行程）
- [ ] 运动指令都有配对的 `stopMotion()`
- [ ] 回调里没有耗时操作（>10 ms）

## F.4 功能

- [ ] 前进/后退/左转/右转各跑一次，`applied` 跟着变
- [ ] 夹爪：`clampConnect()` → 四个电机各动一次 → `clampStop()`
- [ ] 摄像头：能看到帧，`width/height/stride` 合理
- [ ] 超声：`hardwareOnline=true`、A 扫有帧、参数 84 项可读
- [ ] 编码器：`encoders[0]` 随扫查递增，`posA` 与实测位移一致
- [ ] 纠偏：前置条件满足时能 start，`phase` 进入 tracking
- [ ] 激光轮廓：`pointCount > 0`，画出来是一条线不是散点

## F.5 交付

- [ ] 不要把 `<exe>\config\pa1664_profile.json` 提交到仓库（含主机串口/RTSP 凭据）
- [ ] 不要把 `drive_settings.ini` 拷给第三方（跟车绑定）
- [ ] 发布包里的日志目录可写
- [ ] 断网/拔网线测试：程序不崩，恢复后自动重连
