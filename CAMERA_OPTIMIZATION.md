# 摄像头延迟优化方案

> **2026-10-23 低延迟热修复**：默认采集路径已改为进程内低延迟模式。网络摄像头改用 OpenCV/FFmpeg 独立线程，始终丢弃旧帧、只显示最新帧；USB 摄像头优先 320x240 原生格式，缩放改为快速插值。下面 v2.0 的进程隔离子进程方案保留为可选方案/历史设计，不属于当前默认路径。

## 概述

为了解决 USB 和网络摄像头延迟过大的问题,实现了**进程隔离方案**。摄像头采集和图像处理现在运行在独立的进程中,不会影响主进程的运动控制和超声传感器。

## 架构变更

### 之前
```
主进程 (PA1664Workbench)
├─ UI 主线程
│  └─ 摄像头图像缩放 (阻塞 UI)
├─ USB 摄像头线程 (采集)
├─ 工作线程 (图像处理)
├─ 运动控制线程
└─ 超声传感器线程
```

### 现在
```
主进程 (PA1664Workbench)          摄像头服务进程 (PA1664CameraServer)
├─ UI 主线程                      ├─ USB Camera 0 线程
├─ 运动控制线程                   ├─ USB Camera 1 线程
├─ 超声传感器线程                 ├─ Network Camera 0 线程
└─ [IPC 客户端]                   ├─ Network Camera 1 线程
      ↓                           └─ [IPC 服务端]
   QLocalSocket 通信                    ↓
      ↑_____________________________|
              传输压缩后的 JPEG
```

## 优化措施

### 1. **进程隔离** (最关键)
- 摄像头运行在独立进程 `PA1664CameraServer.exe`
- 完全隔离计算资源,不影响运动控制
- 进程崩溃不影响主进程稳定性

### 2. **源头降分辨率**
- USB 摄像头优先选择 640x480 格式
- 网络摄像头强制缩放到 640x360
- 减少数据传输量

### 3. **降低帧率**
- 默认 15 FPS (原来 30 FPS)
- 减少 CPU 占用

### 4. **UI 线程零处理**
- 主进程直接显示接收到的图像
- 不再进行任何缩放或转换

### 5. **网络流低延迟配置**
- 最小缓冲区 (50ms)
- 激进的帧丢弃策略
- 自动重连机制

### 6. **高效传输**
- JPEG 压缩 (质量 75)
- 通过 `QLocalSocket` 传输
- 序列化开销小

## 当前默认路径（2026-10-23 热修复）

- 主程序默认使用 `CameraProcessMode::InProcess`，不再自动启动 `PA1664CameraServer.exe`。
- USB 摄像头优先选择原生 320x240（评分目标 15 FPS），并在工作线程中使用快速插值缩放。
- USB 帧请求已显式注册为 Qt 元类型，避免跨线程队列连接静默丢帧。
- 网络摄像头不再使用 `QMediaPlayer`，改由 `NetworkStreamReader` 在独立线程中通过 OpenCV/FFmpeg 读取；消费者未确认上一帧时只解码新帧，不建立显示队列。
- UI 只在收到新帧时确认消费；窗口 resize 重绘不会释放采集背压。
- 主程序目录必须存在 `opencv_videoio_ffmpeg500_64.dll`（`opencv.pri` 和 `deploy-runtime.ps1` 已配置部署）。

IP 摄像头仍延迟较高时，应优先在摄像头端切换到低码率子码流；低延迟优先时可测试 UDP（`rtsp_transport;udp`），但弱网下稳定性低于当前 TCP。

### USB / 网络来源选择（2026-10-23 修复）

- 每个摄像头槽位现在有独立的“连接方式”：`USB 摄像头` 或 `网络摄像头 (RTSP/HTTP)`。
- 自动连接和手动连接只使用当前选择的来源。网络地址不再隐式覆盖 USB 设备。
- 旧配置没有 `cameraSource` 时，为避免改变既有网络摄像头配置，仍会按“URL 非空则使用网络”迁移；升级后请在界面中为需要 USB 的槽位选择 `USB 摄像头`。
- USB 槽位需要先扫描并选择设备；网络槽位需要填写完整 RTSP/HTTP 地址。

### 问题: 配置过 IP 地址后 USB 摄像头不连接

**原因**: 旧版只要网络地址非空就优先网络连接，USB 分支不会执行，因此日志中只有网络重连，没有 USB 扫描/连接事件。

**处理**:
1. 打开“摄像头配置”。
2. 将对应槽位的“连接方式”改为“USB 摄像头”。
3. 扫描并选择 USB 设备，勾选自动连接后点击连接。
4. 网络摄像头不可达时，先将该槽位切回 USB，或改为其它可用的网络地址。

## 构建说明

### Windows
```cmd
# 构建摄像头服务器
qmake PA1664CameraServer.pro
nmake release

# 构建主程序
qmake PA1664Workbench.pro
nmake release

# 或使用批处理脚本一键构建
build_all.bat
```

### 输出文件
- `bin/PA1664CameraServer.exe` - 摄像头服务进程
- `bin/PA1664Workbench.exe` - 主程序

**重要**: 运行主程序时,`PA1664CameraServer.exe` 必须在同一目录或系统 PATH 中。

## 使用说明

### 自动模式 (推荐)
默认情况下,`RobotUsbCameraController` 会自动尝试启动进程隔离模式:

```cpp
// 默认使用 IsolatedProcess 模式
RobotUsbCameraController* controller = new RobotUsbCameraController(parent);
```

如果摄像头服务启动失败,会自动回退到进程内模式。

### 手动指定模式
```cpp
// 强制使用进程内模式
RobotUsbCameraController* controller = new RobotUsbCameraController(
    parent,
    CameraProcessMode::InProcess
);

// 强制使用进程隔离模式
RobotUsbCameraController* controller = new RobotUsbCameraController(
    parent,
    CameraProcessMode::IsolatedProcess
);
```

## 性能提升

### 预期效果
- **延迟**: 降低 50-70%
  - 网络摄像头: ~500ms → ~150ms
  - USB 摄像头: ~200ms → ~60ms

- **CPU 占用**: 主进程降低 20-40%
  - 摄像头处理移到独立进程
  - 主进程只负责显示

- **稳定性**: 大幅提升
  - 摄像头故障不影响运动控制
  - 进程隔离提高容错性

## 调试

### 查看摄像头服务日志
摄像头服务的输出会通过 `QProcess` 转发到主进程的日志系统:

```cpp
// 在 robot_app_logger.cpp 中查看
// 标签: CAMERA_SERVER.*
```

### 手动启动摄像头服务
```cmd
# 调试模式
PA1664CameraServer.exe --debug

# 测试单个摄像头
PA1664CameraServer.exe --test-usb 0
```

## 故障排查

### 问题: 摄像头服务启动失败
**症状**: 日志显示 "Failed to start camera server"

**解决方法**:
1. 确认 `PA1664CameraServer.exe` 在正确位置
2. 检查是否缺少 Qt 依赖 DLL
3. 查看服务进程的 stderr 输出

### 问题: 画面仍然延迟
**可能原因**:
1. 网络摄像头带宽不足
2. USB 摄像头硬件限制
3. IPC 通信瓶颈

**进一步优化**:
- 降低分辨率到 320x240
- 降低帧率到 10 FPS
- 调整 JPEG 压缩质量

### 问题: 自动回退到进程内模式
**检查**:
1. 构建是否包含 `PA1664CameraServer`
2. 可执行文件是否在搜索路径
3. 进程启动权限是否足够

## 进一步优化空间

如果延迟仍不满意,可以考虑:

1. **共享内存传输** (零拷贝)
   - 替换 `QLocalSocket`
   - 使用 `QSharedMemory`
   - 复杂度更高,但延迟最低

2. **硬件加速**
   - 使用 GPU 进行图像处理
   - Qt Multimedia 的硬件解码

3. **更低的分辨率**
   - 320x240 或 160x120
   - 适用于简单的视觉定位

4. **跳帧策略**
   - 只处理最新的一帧
   - 丢弃队列中的旧帧

## 文件清单

### 新增文件
- `src/camera_server.h/cpp` - 独立摄像头服务进程
- `src/camera_server_client.h/cpp` - 主进程的 IPC 客户端
- `PA1664CameraServer.pro` - 摄像头服务构建配置
- `build_all.bat` - 一键构建脚本
- `CAMERA_OPTIMIZATION.md` - 本文档

### 修改文件
- `src/robot_usb_camera_controller.h/cpp` - 支持进程隔离模式
- `PA1664Workbench.pro` - 添加客户端源文件

### 保留文件 (回退用)
- `src/camera_frame_processor.h/cpp` - 进程内工作线程

## 版本历史

- **v2.1** (2026-10-23): 修复 USB / 网络来源隐式抢占
  - 每个槽位显式选择 USB 或网络来源
  - 旧配置按非空网络 URL 兼容迁移
- **v2.0** (2026-10-23): 进程隔离方案
  - 独立的摄像头服务进程
  - QLocalSocket IPC 通信
  - 自动回退机制

- **v1.0** (之前): 进程内优化
  - 工作线程处理图像
  - 网络流低延迟配置
  - 源头降分辨率

## 摄像头连接专用日志

- 文件位置：`<程序目录>/logs/camera_connection.log`（每次启动清空）。
- 记录范围：`CAMERA.CONNECTION / DISCOVERY / SDK / CONFIG / ACQUISITION / PAYLOAD`、`USB_CAMERA.*`，以及所有摄像头类别的 `WARN/ERROR`；不记录高频的 `CAMERA.PROFILE` 和 `CAMERA.UI`。
- 启动首条 `event=camera_environment` 会列出运行用户、程序目录、OpenCV 版本、FFmpeg 后端 DLL 的搜索目录、程序目录内实际的 OpenCV DLL 列表和加载结果。若为 `ffmpeg_backend=MISSING`，说明部署目录缺少 `opencv_videoio_ffmpeg*_64.dll`，网络摄像头一定无法打开。
- DLL 会依次搜索程序目录、`bin/release/debug/runtime/x64` 等子目录、`OPENCV_FFMPEG_DLL_DIR` 和 `PATH`；找到后自动把 `OPENCV_FFMPEG_DLL_DIR` 指向实际所在目录。
- 网络地址中的 `password=`、`pass=`、`pwd=` 以及 `user:pass@` 会自动脱敏，可直接发送该文件用于排查。
