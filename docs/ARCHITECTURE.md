# 架构说明

```text
Qt MainWindow
 ├─ ParameterPanel ────────────── Client SDK 参数接口
 ├─ DeviceController ─────────── Connect/Capture/Callback/在线判断
 │    └─ QByteArray 原始帧
 ├─ PacketDecoder ─────────────── 组偏移、声束、闸门测量、帧尾
 │    ├─ AScanWidget
 │    ├─ EScanWidget
 │    └─ CScanWidget
 ├─ FrameRecorder ─────────────── .pa16raw + 配套 JSON
 └─ FramePlayer ───────────────── 离线回放
```

## 状态定义

- SDK 未连接：`Client::Connect()` 尚未成功。
- SDK 已连接：只说明本机 Combo 服务通道可用，不能证明 USB 仍连接。
- 等待硬件帧：已发出采集命令，但尚未收到回调。
- USB 硬件在线：1.5 秒内收到过目标 `deviceId` 的非空数据帧。

这一区分专门解决厂商示例在拔掉 USB 后仍可能提示 `127.0.0.1` 连接成功的问题。

## 运动模块接入点

后续加入爬行小车时建议增加以下三层：

1. `IMotionTransport`：串口、CAN 或 TCP 的字节传输。
2. `MotionController`：速度、方向、启停、里程和急停状态机。
3. `ScanCoordinator`：以编码器或位置触发采集，把运动坐标与超声帧组成检查记录。

超声 `DeviceController` 不直接依赖运动模块；急停和失联策略由 `ScanCoordinator` 管理。这样可以分别做 USB 超声台架测试和运动台架测试。

