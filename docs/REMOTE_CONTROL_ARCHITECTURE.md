# PA1664 上位机 / 下位机远程控制架构

> 文档状态：截至 2026-10-24，按当前源码整理。
>
> 代码状态：远程 Host、Workbench 远程会话、四通道传输、机器人/外设/超声服务以及远程串口枚举已在源码中落地；**尚未完成编译、链接和实车验证**。
>
> 适用范围：小车电脑作为下位机，操作电脑作为上位机。操作电脑不再通过远程桌面进入小车电脑，而是运行原布局的 `PA1664Workbench.exe`，直接控制小车电脑上的无界面 `PA1664RobotHost.exe`。
>
> 安全边界：本文描述的是应用层软件架构。软件急停不能替代独立硬线急停、断电回路和现场安全评估。

## 1. 结论先行

采用“同一套源码、两个可执行程序”的架构：

- `PA1664RobotHost.exe`：运行在小车电脑，无主窗口。独占底盘、IMU、激光、USB/网络相机、夹子电机、超声设备和自动纠偏，并持续通过四个网络通道向操作电脑提供 RPC、遥测、媒体和可靠批量数据。
- `PA1664Workbench.exe`：运行在操作电脑。保留现有 `MainWindow`、超声参数页、扫描显示、录像/回放、小车控制页、探头调节页及原有布局；在远程模式下只发送命令、接收数据并绘制界面。
- 共享协议层：两端共用 `src/common/protocol`、`src/common/transport` 和 `src/common/services`，固定消息头、长度上限、分片规则、HMAC、序号、会话和 JSON 数据格式。
- 本机模式：`PA1664Workbench.exe --local` 保留原来的单机运行方式，作为调试和回归基线，也是远程方案故障时的回退路径。

核心原则：

1. 安全相关状态机、硬件闭环和自动纠偏只在下位机运行。
2. 上位机断网、卡顿、崩溃或重绘变慢时，下位机必须依靠租约和看门狗自行停车。
3. UI 线程不直接访问硬件，不执行同步网络等待，不因等待 RPC 响应而阻塞。
4. 控制、实时运动、媒体和可靠批量数据必须物理分通道，不能共用一个 TCP 连接。
5. 同一时刻只有一个操作台拥有控制权，其他连接不能并行驱动小车。
6. 远程模式不能改变原有界面布局、页签、按钮和业务流程。

## 2. 目标拓扑

```text
                          操作电脑 / 上位机
+------------------------------------------------------------------+
| PA1664Workbench.exe                                              |
|                                                                  |
| MainWindow / ParameterPanel / ScanWidgets / RobotControlPanel     |
| ProbeAdjustmentPanel / 录像回放 / 远程超声代理                    |
|                            |                                     |
|                  RemoteWorkbenchSession                          |
|          +-----------------+------------------+                  |
|          |                 |                  |                  |
|    ControlClient     RealtimeClient     MediaClient/BulkClient   |
+----------+-----------------+------------------+------------------+
           |                 |                  |
   TCP 43100 控制      UDP 43101 实时      UDP 43102 媒体
           |                 |             TCP 43103 批量
           |                 |                  |
+----------+-----------------+------------------+------------------+
| PA1664RobotHost.exe / 小车电脑，无可见主窗口                     |
|                                                                  |
| HostApplication                                                  |
|  + ControlServer   + RealtimeServer                              |
|  + MediaServer     + BulkServer                                  |
|                            |                                     |
| RobotHostService / RobotPeripheralHostService / UltrasoundHost   |
|                            |                                     |
| 底盘线程 / 传感器线程 / 相机线程 / 夹子线程 / 纠偏线程 / 超声线程 |
|                            |                                     |
| 底盘、IMU、激光、USB/网络相机、夹子、超声 SDK、PCAN/candle CAN    |
+------------------------------------------------------------------+
```

不使用屏幕串流、VNC 或远程桌面。上位机只传输业务命令、状态和业务媒体，不传输桌面像素。

## 3. 职责边界

| 功能 | 小车电脑 Host | 操作电脑 Workbench |
|---|---|---|
| 底盘串口/CAN、使能、闭环、差速混合 | 负责 | 只发意图、显示状态 |
| 手动运动安全状态机 | 负责 | 只发速度意图和停止/急停 |
| IMU、激光、USB/网络相机采集 | 负责 | 不直接访问硬件 |
| 激光识别、自动纠偏算法 | 负责 | 只发启停、暂停和参数 |
| 夹子/探头电机控制与位置读取 | 负责 | 只发控制命令、显示位置 |
| 超声 SDK、参数回读、预设和采集 | 负责 | 显示草稿，发起参数读写 |
| 超声 A/B/C/E 扫描显示 | 发送结构化数据 | 原控件本地绘制 |
| 录像和回放 | 当前不接管 | 保留 Workbench 本机文件读写 |
| 硬件端口枚举 | 枚举小车电脑的 COM/CAN | 显示 Host 返回列表 |
| 驱动配置权威副本 | `Host/config/drive_settings.ini` | 本地保存 UI 快照，Host 是硬件执行权威 |
| 安全停车租约 | 负责 | 断线时清空运动意图 |
| 操作日志 | `Host/logs/robot_host.log` | `Workbench/logs/*.log` |

远程模式下，`MainWindow` 向 `RobotControlPanel` 和 `ProbeAdjustmentPanel` 注入 `RemoteWorkbenchSession`，因此这些面板不会在小车电脑之外创建本地底盘、IMU、激光、相机或夹子控制器。

## 4. 四通道总表

| 通道 | 默认端口 | 传输 | 主要职责 | 可靠性 | 应用层认证 |
|---|---:|---|---|---|---|
| 控制/RPC | 43100 | TCP | 握手、能力协商、请求/响应、事件、快照 | 可靠有序 | 当前无 TLS、无控制帧 HMAC |
| 实时运动 | 43101 | UDP | 心跳、运动意图、遥测、急停、ACK | 允许丢包，新值覆盖 | HMAC-SHA256 截断 16 字节 |
| 媒体 | 43102 | UDP | 超声显示、激光图像/点云、IMU、相机、纠偏状态 | 允许丢包/丢帧 | HMAC-SHA256 截断 16 字节 |
| 可靠批量 | 43103 | TCP | 当前用于完整超声扫描批次 | 可靠有序 | HMAC-SHA256 截断 16 字节 |

所有端口可由 Host 命令行参数修改，并在控制握手时通过 `HelloAck` 动态告知 Workbench。Workbench 启动参数中的端口只用于连接 43100，其余端口以 `HelloAck` 为准。

## 5. 控制通道：TCP 43100

### 5.1 用途

控制通道负责低频、需要确认的动作和状态：

- 版本、能力协商和会话建立；
- 底盘连接、断开、使能、复位、清报警；
- `robot.snapshot`、`robot.settings.get/update`；
- IMU、激光、USB/网络相机连接/断开和扫描；
- 夹子配置、连接、点动、位置和停止；
- 自动纠偏启动、暂停、停止和状态；
- `robot.peripherals.snapshot`、`robot.peripherals.serialPorts`；
- 超声设备连接、采集启停、参数、预设、编码器标定；
- Host 主动事件，例如 `host.ready`、`host.busy`、`robot.safetyStop`、参数失效和批量积压。

### 5.2 线格式与限制

- 魔数 `PA16`，协议版本 `1`，控制帧头 **50 字节**。
- 控制帧 JSON 载荷上限 **1 MiB**，对应 `kMaxControlPayloadBytes`。
- 控制消息类型：`Hello`、`HelloAck`、`Request`、`Response`、`Event`、`Ping`、`Pong`、`Error`。
- 每个方向使用严格递增序号；序号回退会关闭会话。
- 控制请求使用 `requestId` 关联响应，不在 UI 线程里同步等待。
- Workbench 最多同时保留 **2048** 个待响应请求，单请求超时 **5 秒**。
- Host 最多接受 **8** 个控制 TCP 连接。
- Host 握手超时 **5 秒**；Workbench 自行握手超时 **3 秒**。
- 控制 socket 启用 `LowDelayOption` 和 `KeepAliveOption`。
- Workbench 发送积压超过 2 MiB 时拒绝新请求；Host 读/写缓冲也有硬上限，持续积压会关闭连接。
- ControlClient 注册成功后每 **250 ms** 发 Ping。Workbench 超过 **1 秒**未收到任何控制数据即重连；Host 超过 **2 秒**未收到控制数据则关闭该会话。

### 5.3 握手和单控制者

1. Workbench 发送 `Hello`，包含客户端标识和协议版本，此时 `sessionId=0`。
2. Host 校验协议版本，生成 32 字节随机会话密钥和 `sessionId`。
3. Host 在 `HelloAck` 中明文返回 `sessionId`、Base64 编码的 `sessionToken`、四个端口、心跳/运动参数和能力列表。
4. Host 尝试同时取得底盘控制权和外设控制权。
5. 若已有操作台持有控制权，Host 发送 `host.busy`，约 250 ms 后关闭新会话；不会并行接受第二个运动控制者。
6. 取得控制权后，Host 发送 `host.ready`，携带底盘和外设快照。

### 5.4 安全事实

当前控制通道 **不是 TLS**，控制 JSON、事件、快照和 32 字节 `sessionToken` 都在 TCP 中明文传输。UDP/TCP 批量通道的 HMAC 只能证明消息持有该会话密钥，不能提供保密性，也不能阻止局域网窃听者获得密钥后伪造数据。

因此：

- 只允许部署在可信、隔离、受防火墙保护的控制局域网；
- TCP 43100 和 43103 不得直接暴露到公网；
- 若现场网络不可信，必须在业务协议外增加 WireGuard、IPsec 等受控 VPN，或后续为控制通道引入 TLS 1.3 和证书/证书固定；
- 不能把当前 `sessionToken` 方案描述为强用户身份认证或端到端加密。

## 6. 实时通道：UDP 43101

### 6.1 用途

- 操作员心跳；
- 手动运动速度意图；
- 底盘遥测；
- 软件急停；
- 实时 ACK/恢复事件。

媒体帧不走该通道，避免大帧阻塞运动数据。

### 6.2 线格式和调度

- 实时头 **44 字节**，后附 **16 字节** HMAC-SHA256 截断标签。
- 单个 UDP 数据报上限 **1200 字节**，避免常见以太网 MTU 下的 IP 分片。
- 包类型：`Heartbeat`、`MotionSetpoint`、`Telemetry`、`EmergencyStop`、`Ack`。
- Workbench 的实时客户端位于独立 `WorkbenchRealtimeUdp` 线程，线程优先级为高，发送定时器为 `Qt::PreciseTimer`。
- 运动激活时每 **50 ms** 复投一次最近速度意图。
- 默认运动租约 **200 ms**，允许范围 50～1000 ms。
- 操作员心跳间隔 **250 ms**。
- Host 使用独立 **25 ms** 精确看门狗检查租约和会话超时。
- 会话超过 **1000 ms** 无有效包即判定超时。

`setMotion()`、`clearMotion()`、`start()`、`stop()` 和 `emergencyStop()` 通过 queued invocation 投递到实时线程，GUI 不直接写 socket，也不阻塞等待网络。

### 6.3 运动租约

1. 操作员持续运动时，上位机每 50 ms 复投同一目标速度。
2. Host 每次接受新运动包时刷新该会话的租约到期时间。
3. 超过租约而未收到新包，Host 触发 `motionLeaseExpired`：
   - 停止自动纠偏；
   - 停止夹子运动；
   - 底盘下发停止；
   - 发送 `robot.safetyStop` 事件。
4. 超过 1 秒无心跳，Host 再触发 `operator_timeout` 安全停车。
5. 链路恢复只恢复会话和状态显示，不自动重新使能、不自动恢复自动纠偏、不重放旧运动命令。

### 6.4 防重放和发送方绑定

- 每个实时包带 `sessionId` 和单调递增 `sequence`。
- 只有 HMAC 校验通过且属于已注册会话的包才会被接受。
- 会话第一次有效包会绑定源 IP/UDP 端口；之后地址或端口变化会被拒绝。
- 序号回退或重复包会被拒绝。
- 急停包在 Workbench 端连续发送 5 次，并同时通过 TCP 请求 `robot.emergencyStop`，但最终安全仍依赖硬线急停。

## 7. 媒体通道：UDP 43102

### 7.1 线格式

- 媒体头 **58 字节**，后附 **16 字节** HMAC-SHA256 截断标签。
- 单个数据报最大 **1200 字节**，因此每片有效载荷约 **1126 字节**。
- 单个完整媒体消息上限 **8 MiB**。
- 单消息最多 **8192 片**。
- 分片重组超时 **1500 ms**。
- Workbench 媒体心跳间隔 **500 ms**。

### 7.2 当前流 ID

| streamId | 内容 |
|---:|---|
| `0x0100 + groupId` | 超声实时显示帧 |
| `0x0201` | 激光灰度图 |
| `0x0202` | 激光点云 |
| `0x0203` | IMU 样本 |
| `0x0300 + slot` | USB/网络相机显示帧 |
| `0x0401` | 自动纠偏状态媒体 |

### 7.3 传输策略

- 每个流独立重组，按 `streamId` 保存当前消息。
- 同一流收到更新 `messageId` 时，立即丢弃旧消息的未完成分片，只保留最新消息，即 latest-wins。
- 媒体不重传；丢失分片超过 1.5 秒即丢弃。
- 不因缺帧阻塞运动、心跳或控制 RPC。
- Host 收到媒体会话第一个包时，必须先通过 HMAC，才记录该会话的返回地址；未认证数据不能抢占或改变媒体目的地址。
- 媒体发送端在收到认证通过的操作员媒体心跳后，才认为媒体对端可用。

### 7.4 当前限制

媒体重组和大图像/超声载荷解码仍在 Workbench 的 GUI 侧路径上。运动/心跳已隔离到独立高优先级线程，但超大媒体载荷或大量解码仍可能造成界面卡顿，并间接影响与控制 TCP 的交互。生产验收必须测量最大帧、CPU 占用和 UI 卡顿；若超标，应把媒体重组/JPEG/超声载荷解码迁移到专用媒体工作线程，只向 GUI 投递最新图像。

## 8. 可靠批量通道：TCP 43103

### 8.1 线格式

- 线头 **28 字节**：
  - magic：4 字节；
  - version：2 字节；
  - kind：2 字节；
  - sessionId：8 字节；
  - sequence：8 字节；
  - payloadBytes：4 字节。
- 头后是载荷，再后是 **16 字节** HMAC-SHA256 截断标签。
- 单帧载荷上限 **32 MiB**。
- Workbench 端写高水位 **8 MiB**；Host 端发送 socket 积压超过 **8 MiB** 时暂不继续发送。
- Host 待发 Bulk 队列上限 **256 MiB**；溢出时发送 `ultrasound.bulkOverflow` 并停止采集。
- Bulk 客户端断线后每 **1 秒**重连，连接后每 **1 秒**发送 HMAC 心跳。

### 8.2 当前实际用途

当前可靠批量通道只接入完整超声扫描批次 `UltrasoundScanBatch`。协议枚举中虽然存在录像、配置和 JSON 事件类型，但并未完整接入机器人录像、断点续传、文件清单或 SHA-256 完整性校验。

当前也 **不广告 `robot.bulk.v1`**。`robot.bulk.v1` 只是保留常量，不代表机器人录像/大文件功能已经完成。

### 8.3 可靠性边界

Bulk 的 TCP/HMAC/序号提供单连接传输可靠性，但当前没有：

- 文件级断点续传；
- 跨进程持久化 spool；
- 分块清单；
- SHA-256 文件校验；
- 机器人录像自动落盘和补传；
- Host 重启后的传输恢复。

不能把这些能力写成已完成项。

## 9. 线程与阻塞控制

### 9.1 Host 线程

| 线程/对象 | 优先级 | 职责 |
|---|---|---|
| Qt 主事件循环 | 普通 | Control/Realtime/Media/Bulk Server、HostApplication、服务请求路由 |
| `RobotDriveControl` | 高 | 底盘闭环、差速混合、电机通信 |
| `RobotSensors` | 高 | IMU、激光相机和传感器采集 |
| `RobotPeripheralMedia` | 高 | 外设显示媒体编码/发布 |
| `RobotUsbCameras` | 普通 | USB/网络相机采集 |
| `RobotClampMotors` | 低 | 夹子电机通信 |
| `RobotAutoCorrection` | 高 | 激光纠偏状态机和输出 |
| `RobotPortEnumeration` | 低 | Host 小车电脑的串口、candle 和 PCAN 枚举；不接触运动控制 |
| `UltrasoundHostControl` | 从 Host 主线程独立 | 超声 RPC 和 SDK 控制 |
| `UltrasoundDecoder` | 高 | 超声原始帧解码 |
| `UltrasoundUplink` | 普通 | 超声显示/扫描数据编码 |

关键设计：运动闭环、传感器、相机、夹子和纠偏不在 GUI/Host 主事件循环中执行。Host 本身无 UI，不存在主窗口重绘阻塞。

端口枚举已与主事件循环隔离：`robot.peripherals.serialPorts` 在主线程只登记任务，`QSerialPortInfo`、candleLight 和 PCAN-Basic 的扫描在低优先级 `RobotPortEnumeration` 线程执行，结果再通过 queued invocation 回到主线程。因此驱动扫描变慢时，不会直接占住 Host 主事件循环中的实时看门狗、UDP 心跳入口或运动 RPC。同一时刻只允许一个枚举任务；同一会话的并发刷新会合并到该轮扫描并分别应答，断线/重连/关停后通过代次号丢弃过期结果。

仍需注意：四个网络 Server 和其余部分外设 RPC 的入口仍在 Host 主事件循环。其他同步设备调用、大载荷编解码或主线程排队过长仍可能延迟看门狗；实车必须做 25～50 ms 节拍下的压力测量，不能只根据线程划分推断实时性。

Host 停机时先请求底盘急停并锁停，再依次释放外设和超声设备，最后停止四个网络监听；这些等待只发生在进程退出路径，不允许出现在运动控制、心跳或 RPC 的常规热路径。底盘急停与释放作为同一个 queued invocation 在 `RobotDriveControl` 线程内顺序执行，Host 主线程最多等待 3 秒。传感器、相机、夹子、纠偏和媒体编码的停止会先全部投递到各自 worker，再逐个有界等待：正常最多 10 秒，超时后请求中断并再等 2 秒，仍未退出才终止该线程。超声关停通过 queued invocation 投递到 `UltrasoundHostControl`，内部解码/编码线程分别限时 5 秒，外层总等待 20 秒；正常完成后由 `shutdownComplete` 退出超声线程。后台停机路径已不再使用无超时的 `BlockingQueuedConnection`。底盘、外设或厂商 SDK 卡死时，超时后的 `terminate()` 只是进程退出保底，不能当作安全恢复手段，因此现场仍必须以硬线急停和可断电重启作为最终安全保底。

### 9.2 Workbench 线程

| 线程/对象 | 职责 |
|---|---|
| GUI 主线程 | 窗口、原有控件、ControlClient、MediaClient、BulkClient、RPC 响应分发 |
| `WorkbenchRealtimeUdp` 高优先级线程 | 心跳、运动复投、急停、遥测接收 |

关键约束：

- 所有控制请求都是异步 `request()`，返回 `requestId`，不阻塞等待结果。
- 运动意图通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递，不跨线程调用 socket。
- 在线路断开时上位机发送 `clearMotion()`，随后 Host 的 200 ms 租约和 1 秒会话超时提供双重停车保护。
- GUI 线程上的媒体重组/解码仍是待优化点，见第 7.4 节。

## 10. 能力协商

Host 当前广告以下能力：

```text
robot.control.v1
robot.telemetry.v1
robot.settings.v1
robot.sensors.v1
robot.media.v1
ultrasound.control.v1
ultrasound.parameters.v1
ultrasound.media.v1
ultrasound.bulk.v1
```

`robot.bulk.v1` 仅在代码中保留名称，Host 不广告，Workbench 不应依赖它。

UI 必须通过 `hasCapability()` 决定是否启用可选操作，不能假设所有 Host 构建都实现全部消息族。当前远程串口刷新要求 `robot.sensors.v1`。

## 11. RPC 与服务分布

| 前缀/操作 | 服务 | 说明 |
|---|---|---|
| `robot.connect/disconnect/enable/reset/clearAlarm` | `RobotHostService` | 底盘生命周期 |
| `robot.snapshot/settings.get/settings.update` | `RobotHostService` | 状态与配置 |
| `robot.emergencyStop/estop/motion.stop/stop` | Host 路由到双重安全停车 | UDP 和 TCP 双路径 |
| `robot.imu.*` | `RobotPeripheralHostService` | IMU 扫描、连接、状态 |
| `robot.laser.*` | `RobotPeripheralHostService` | 激光设备与扫描 |
| `robot.camera.*` | `RobotPeripheralHostService` | 相机扫描、连接、状态 |
| `robot.clamp.*` | `RobotPeripheralHostService` | 夹子配置、连接、点动、位置 |
| `robot.autoCorrection.*` | `RobotPeripheralHostService` | 远端启停/暂停，算法在 Host |
| `robot.peripherals.*` | `RobotPeripheralHostService` | 外设快照、Host 串口/CAN 枚举 |
| `robot.sensors.*` | `RobotPeripheralHostService` | 传感器扫描/快照 |
| `ultrasound.*` | `UltrasoundHostService` | 设备、采集、参数、预设、编码器标定 |

## 12. 上位机界面保持不变的实现方式

### 12.1 启动模式

Workbench 默认进入远程模式。连接目标按以下优先级确定：

1. `--remote <host[:port]>`
2. `--remote=<host[:port]>`
3. 环境变量 `PA1664_REMOTE_HOST`
4. `QSettings` 中保存的 `remote/endpoint`
5. 默认 `127.0.0.1:43100`

示例：

```powershell
.\PA1664Workbench.exe --remote 192.168.50.20:43100
```

本机模式：

```powershell
.\PA1664Workbench.exe --local
```

### 12.2 UI 注入

- 远程模式创建 `RemoteWorkbenchSession`，把同一对象注入 `MainWindow`。
- `RobotControlPanel` 在存在远程会话时不创建本地底盘/IMU/激光/相机控制器。
- `ProbeAdjustmentPanel` 在存在远程会话时不创建本地夹子控制器。
- 远程超声参数及媒体通过 `UltrasoundParameterService`、`RemoteUltrasoundSource` 适配。
- 窗口布局、页签、按钮、扫描控件和回放控件仍由原有类创建。
- 远程快照到达后更新原控件状态，不增加新的业务页面。

### 12.3 远程串口枚举

远程模式下，“刷新串口”必须枚举小车电脑硬件，不能枚举操作电脑：

- Workbench 请求 `robot.peripherals.serialPorts`。
- Host 在独立的低优先级 `RobotPortEnumeration` 线程枚举 `QSerialPortInfo`、`candleLight` 和 `PCAN-Basic`。
- 主线程不等待硬件扫描；完成后的结果通过 queued invocation 返回并响应原请求。
- 同一时刻只执行一个枚举；同一会话的并发刷新合并到该轮任务并分别应答，不重复扫描。
- 最多合并 16 个请求，超过上限返回 `port_enumeration_queue_full`，防止异常客户端无限排队。
- 会话断开、重连或 Host 关停后，通过枚举代次号丢弃过期结果，不向新会话误发响应。
- 返回字段：

```json
{
  "ports": [
    {
      "id": "COM3",
      "label": "COM3",
      "description": "USB Serial Port",
      "kind": "serial"
    },
    {
      "id": "pcan:51",
      "label": "PCAN_USBBUS1",
      "description": "PCAN-USB",
      "kind": "can"
    }
  ],
  "pcanError": ""
}
```

- 底盘左轮、右轮和 IMU 只显示 `kind=serial`。
- CAN/SLCAN 下拉框显示 `serial + can`。
- 夹子配置显示 Host 返回的全部串口/CAN 适配器。
- 刷新时保留当前选择；当前选择为空时回退到已保存配置；旧配置即使暂时未枚举到也保留为可显示项，避免静默改端口。
- 小车控制页和探头调节页的刷新按钮都走同一远程语义。

### 12.4 配置所有权

- Host 读取自身程序目录 `config/drive_settings.ini` 中的权威驱动配置。
- Workbench 保留本机 UI 快照，但通过 `robot.snapshot` / `robot.settings.get` 获取 Host 配置。
- 用户在 UI 修改并提交后，由 Host 保存并执行。
- 远程刷新串口只能选择 Host 返回的端口 ID，不能用操作电脑的 `COMx` 覆盖小车配置。

## 13. 安全与故障矩阵

| 场景 | 预期行为 |
|---|---|
| Workbench 关闭/崩溃 | UDP 心跳停止；200 ms 运动租约到期；1 秒会话超时；Host 安全停车并停自动纠偏 |
| Workbench 网络断开 | 上位机清空运动意图；Host 看门狗独立触发停车 |
| 控制 TCP 断开但 UDP 尚存 | Workbench 进入降级/重连并清运动；Host 关闭原会话后移除 UDP/Bulk/媒体会话 |
| 第二个 Workbench 连接 | 收到 `host.busy` 后关闭；不获得实时会话密钥 |
| 旧 UDP 包重放 | 序号不递增或 HMAC 无效则丢弃 |
| 媒体丢片/拥塞 | 当前消息超时丢弃，较新消息覆盖旧消息；不影响运动 |
| Bulk 积压 | Host 达 256 MiB 上限时停采集并上报溢出，不无限增长 |
| Host 重启 | 默认不自动使能底盘、不自动启动自动纠偏；需人工确认 |
| 上位机重连 | 重新同步快照；不重放旧危险命令、不自动恢复运动 |
| 软件急停 | UDP 连续发送并同时发 TCP 急停；仍不能替代硬线急停 |
| Host 进程崩溃 | 依赖控制器自身失联保护及硬线急停；应用层无法保证 |

硬线急停必须是最终安全措施，且独立于 Wi-Fi、Windows、Qt 和本软件进程。

## 14. 网络和主机部署

### 14.1 网络要求

- 小车电脑固定 IP，操作电脑固定 IP 或 DHCP 保留。
- 首选千兆有线；Wi-Fi 仅作为明确验证过的现场方案。
- 独立控制网段，不与办公网、互联网混用。
- 关闭 Windows 网卡节能、USB 选择性挂起和系统睡眠。
- 若无线部署，使用独立 5 GHz AP，固定信道，关闭客户端省电。
- 建议所有设备启用 NTP 同步，便于日志比对；安全判断不依赖两台电脑的绝对时钟。

### 14.2 防火墙

只允许指定操作电脑或控制网段访问：

- TCP 43100：控制；
- UDP 43101：实时；
- UDP 43102：媒体；
- TCP 43103：可靠批量。

示意命令，执行前把 IP/网段和规则名改为现场值：

```powershell
$peer = '192.168.50.10'
New-NetFirewallRule -DisplayName 'PA1664 Control TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 43100 -RemoteAddress $peer
New-NetFirewallRule -DisplayName 'PA1664 Realtime UDP' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 43101 -RemoteAddress $peer
New-NetFirewallRule -DisplayName 'PA1664 Media UDP' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 43102 -RemoteAddress $peer
New-NetFirewallRule -DisplayName 'PA1664 Bulk TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 43103 -RemoteAddress $peer
```

禁止使用 `RemoteAddress Any` 把端口开放到不可信网络。

### 14.3 Host 启动

Host 命令行：

```text
PA1664RobotHost.exe
  --listen 0.0.0.0
  --control-port 43100
  --realtime-port 43101
  --media-port 43102
  --bulk-port 43103
```

要求四个端口互不相同。Host 会在程序目录创建：

- `logs/robot_host.log`：追加日志；
- `robot_host.lock`：单实例锁，超时 15 秒，防止同时启动两个 Host。

### 14.4 Windows 自动启动

推荐在**已登录的交互桌面会话**中隐藏启动 Host，而不是直接把 Host 安装为 Session 0 Windows 服务。相机 SDK、USB 设备、PCAN/candle 和部分厂商驱动可能要求交互用户会话。

工程提供独立的双端启动目录：

```text
startup_deploy\robot_host
startup_deploy\operator_workbench
```

小车电脑：

1. 将完整 Host 运行文件放入 `startup_deploy\robot_host\app`；
2. 先双击 `StartRobotHost.cmd` 验证手动启动；
3. 再双击 `InstallAutoStart.cmd`，为当前 Windows 用户创建桌面快捷方式和“登录时启动”目录快捷方式；
4. 如需无人值守开机，让小车电脑使用该专用账号自动登录。

操作电脑同样使用 `startup_deploy\operator_workbench`，并在 `config\workbench.psd1` 中配置小车 IP。两端的启动器都会写 `runtime\launcher.log`。完整步骤见 `startup_deploy\README.md`。

这种方案不写系统服务、不写注册表自启动项、不需要管理员权限。必须保证安装快捷方式时的 Windows 用户与以后自动登录用户一致。若必须使用 Windows 服务，服务只能负责守护，并应在交互会话中启动 Host，不能在 Session 0 直接打开 USB/相机。

启动失败时优先检查 `runtime\launcher.log`、`app\logs\robot_host.log`、设备管理器和 `robot_host.lock` 锁文件。

### 14.5 Workbench 启动

```powershell
.\PA1664Workbench.exe --remote 192.168.50.20:43100
```

远程桌面软件不再参与控制链。Workbench 可以像原软件一样最大化运行在操作电脑。

操作电脑也可以使用 `startup_deploy\operator_workbench\StartOperatorWorkbench.cmd` 手动启动，或用 `InstallAutoStart.cmd` 安装当前用户登录自启动。Host 地址保存在 `config\workbench.psd1`，移动目录后应重新安装快捷方式。

## 15. 日志和排障

### 15.1 Host

- 标准输出和 `logs/robot_host.log` 同时记录。
- 重点日志前缀：
  - `CONTROL_SESSION_OPEN/CLOSE`
  - `CONTROL_SESSION_BUSY`
  - `REALTIME_REJECT`
  - `MEDIA_PEER_READY`
  - `MEDIA_REJECT`
  - `BULK_BACKLOG_OVERFLOW`
  - `ROBOT_CONNECTION`
  - `ROBOT_PERIPHERAL`
  - `ROBOT_PORT_ENUMERATION`
  - `ULTRASOUND`
  - `*_SHUTDOWN_TIMEOUT`

### 15.2 Workbench

- `logs/robot_console.log`
- `logs/camera_connection.log`
- `logs/profile_weld_detection.log`
- Workbench 启动时会清理旧 SDK 日志，便于把一次运行日志集中分析。

### 15.3 常见问题顺序

1. 查看 Host 是否在运行、锁文件是否残留、四个端口是否监听。
2. 核对 Workbench 的 `REMOTE_MODE` 目标地址和 Host 日志中的 `CONTROL_SESSION_OPEN`。
3. 核对握手能力列表是否包含预期能力。
4. 检查 Windows 防火墙和网段。
5. 检查 PCAN/candle 驱动及 Host 串口返回中的 `pcanError`。
6. 检查实时 HMAC/序号拒绝、媒体 peer 和 Bulk 溢出日志。
7. 若网络恢复后状态异常，先断开再重连 Workbench，不要直接恢复自动纠偏。

## 16. 已修改/关键文件

### 16.1 共享协议与传输

- `src/common/protocol/protocol_types.h`
- `src/common/protocol/control_codec.*`
- `src/common/protocol/realtime_codec.*`
- `src/common/protocol/media_codec.*`
- `src/common/protocol/bulk_codec.*`
- `src/common/transport/control_server.*`
- `src/common/transport/control_client.*`
- `src/common/transport/realtime_channel.*`
- `src/common/transport/media_channel.*`
- `src/common/transport/bulk_channel.*`
- `src/common/services/remote_workbench_session.*`
- `src/common/services/robot_json_codec.*`
- `src/common/services/workbench_data_codec.*`

### 16.2 小车 Host

- `src/backend/main.cpp`
- `src/backend/host_application.*`
- `src/backend/robot_host_service.*`
- `src/backend/robot_peripheral_host_service.*`
- `src/backend/ultrasound_host_service.*`
- `src/backend/ultrasound_parameter_rpc.*`
- `PA1664RobotHost.pro`

远程串口枚举位于 `RobotPeripheralHostService::handleRequest()`，返回 `robot.peripherals.serialPorts`。

### 16.3 操作电脑 Workbench

- `src/main.cpp`
- `src/mainwindow.*`
- `src/robotcontrolpanel.*`
- `src/probeadjustmentpanel.*`
- `src/robot/clamp_motor_widgets.*`
- `src/services/remote_ultrasound_source.*`
- `src/services/ultrasound_parameter_service.*`
- `PA1664Workbench.pro`

本轮对串口相关 UI 的处理：

- 远程模式不再调用本机 `QSerialPortInfo::availablePorts()`；
- 远程刷新请求 Host 端口列表；
- 底盘/IMU/相机相关串口选择保留 Host 端口 ID；
- 夹子控件新增 `setAvailablePorts()`，使用独立 ID、显示名和 tooltip 填充；
- “探头调节”页同样使用远程端口结果；
- 本机 `--local` 模式仍保留原来的本地端口刷新行为。

### 16.4 已知修复

- Bulk 线头常量已修正为 **28 字节**。此前错误使用 44 字节会和实际编码布局不一致，导致 Bulk 解码不可用。
- `RemoteWorkbenchSession` 析构时先异步停止实时客户端，再退出并等待 `WorkbenchRealtimeUdp` 线程；仅在 5 秒超时后强制终止，避免线程仍在运行时销毁 Qt 对象。
- Host 串口/CAN 枚举已移入低优先级 `RobotPortEnumeration` 线程；响应仍由 Host 主线程按原 RPC 协议发出，Workbench 接口和界面行为不变。
- Host 停机顺序改为先急停并锁停底盘，再释放外设、超声和网络；底盘急停/释放改为在 `RobotDriveControl` 线程内顺序执行，主线程最多等待 3 秒，外设 worker 停止先批量投递再逐个有界等待，超声 `shutdown()` 改由完成信号结束专用线程，主线程最多等待 20 秒。后台停机路径不再使用无超时的 `BlockingQueuedConnection`，超时后的 `terminate()` 仅作为进程退出保底。

## 17. 尚未完成的生产化工作

以下项目必须完成实机验证后才能视为量产完成：

1. 用项目对应 Qt/编译器构建 `PA1664RobotHost`、`PA1664Workbench` 和 `PA1664CameraServer`。
2. 在目标小车电脑验证超声 SDK、激光 SDK、USB 相机、网络相机、PCAN、candle 和夹子串口。
3. 验证四个端口在有线、无线、交换机重启、IP 冲突和防火墙下的行为。
4. 验证断网后 200 ms 租约和 1 秒会话超时能实际停车。
5. 验证自动纠偏、夹子点动、激光显示、两路相机、超声扫描和 Bulk 批次同时运行。
6. 测量 Host 主事件循环是否被其他驱动扫描或大媒体发送拖慢，并在真实 PCAN 慢驱动场景验证 `RobotPortEnumeration` 不造成 25 ms 看门狗、UDP 心跳或运动 RPC 延迟；慢驱动本身尚未实机验证。
7. 测量 Workbench GUI 在 8 MiB 媒体消息和高频图像解码下的卡顿。
8. 完成 8 小时连续运行和 100 次重连测试。
9. 若现场网络不可信，引入 TLS/VPN 和更强的身份认证。
10. 为配置、日志、录像和升级建立版本化及回滚流程。

## 18. 验收清单

### 18.1 协议与安全

- [ ] 错误 magic/version/长度被拒绝，不崩溃。
- [ ] HMAC 篡改、重复序号和序号回退被拒绝。
- [ ] 非注册会话无法占用实时/媒体/Bulk 通道。
- [ ] 第二个操作台只能收到 busy，不能获得控制权。
- [ ] 断网、断 UDP、断控制 TCP 分别触发预期降级和停车。
- [ ] 急停 UDP 和 TCP 双路径均有效。
- [ ] 硬线急停独立有效。

### 18.2 业务功能

- [ ] 远程模式界面布局与本机模式一致。
- [ ] 底盘连接、使能、点动、停止正常。
- [ ] IMU、激光图像、点云显示正常。
- [ ] 两路 USB/网络相机显示正常。
- [ ] 夹子连接、点动、限位、探头调节正常。
- [ ] 自动纠偏启动、暂停、停止正常，断链自动停止。
- [ ] 超声参数读写、预设、采集、扫描显示、Bulk 批次正常。
- [ ] 远程串口/CAN 枚举在两张页面正常，且不会显示操作电脑端口。
- [ ] `--local` 模式功能不回归。

### 18.3 性能

- [ ] 连续运动 30 分钟无租约误停车。
- [ ] 网络抖动和 1%～5% 丢包下界面可用。
- [ ] 媒体拥塞时运动控制延迟不上升。
- [ ] Bulk 传输时运动和控制通道不被饿死。
- [ ] PCAN/candle 枚举耗时超过 1 秒时，运动心跳、急停和 25 ms 看门狗节拍仍不受影响。
- [ ] 两个页面同时刷新端口只触发一轮枚举，两个请求都得到结果；断线/重连后不出现旧枚举结果串会话响应。
- [ ] GUI CPU、内存和延迟有实测记录。
- [ ] Host 8 小时运行无句柄泄漏、无线程退出超时。

## 19. 回滚方案

1. 保留改造前完整备份；当前工作区不是 Git 仓库，不能用 `git diff/status` 作为回滚依据。
2. 现场保留可运行的旧版单机程序和配置。
3. 远程异常时用 `PA1664Workbench.exe --local` 回到原本机模式。
4. Host 停止后确认底盘未使能，再通过原软件或硬线急停保证安全。
5. 不删除旧配置文件，不覆盖唯一硬件参数备份。
6. 版本升级前记录 COM/CAN 端口、相机序列号、激光序列号、超声配置和标定值。

## 20. 当前架构一句话总结

小车电脑负责真实硬件、实时闭环、安全状态机、自动纠偏和媒体采集；操作电脑保留原桌面软件和原交互，只通过 43100 异步 RPC、43101 带租约的实时 UDP、43102 最新帧媒体 UDP 和 43103 可靠批量 TCP 使用这些能力。运动与心跳由上位机独立高优先级线程持续发送，Host 用 HMAC、序号、200 ms 租约和 1 秒会话超时自主停车。当前代码已按此边界落地，但尚未编译和实车验证，且控制通道没有 TLS，只能部署在可信隔离网络或外加 VPN。