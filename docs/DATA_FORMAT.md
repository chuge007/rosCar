# 采集数据格式

本说明来自厂商示例源码，并已用 PA-1664 真机数据链路验证。

## 整帧

SDK 回调签名：

```cpp
void callback(const char *data, int length, int deviceId);
```

厂商示例规定回调缓冲区由接收方 `delete[]`。`DeviceController` 先复制为 `QByteArray`，再释放厂商缓冲区，并用 Qt 队列切回界面线程。

启用的工作组依次拼接在一帧中。单个工作组长度（以 16 位 word 计）：

```text
beamCount × (pointCount + 16)
```

其中每个 beam 前 `pointCount` 个 word 是有符号 A 扫采样，后 16 个 word（32 字节）是闸门测量结果。整帧最后 32 字节为帧尾。

## 每声束测量值

32 字节测量区包含 Gate A/B/C/I 的幅值和位置。当前程序完整解析 A/B/C/I；厂商结构中还预留 D/E 字段。

幅值的显示归一化分母来自 `MaxAmplitude`：

- 1600%：2048
- 800%：4096
- 400%：8192
- 200%：16384

## 帧尾（32 字节）

- 帧错误标志、Scanner IO、多帧编号
- 12 位帧号、编码器触发/同步错误
- 编码器 A/B/C/D/E：五个有符号 32 位值
- 64 位时间戳

## 多工作组偏移

查看组 `g` 时，组起始偏移必须是它之前所有启用组长度之和：

```text
offsetWords = Σ previousBeamCount × (previousPointCount + 16)
```

旧示例的部分 C 扫路径没有一致应用该偏移。新项目统一由 `DeviceController::refreshGeometry()` 计算，再交给 `PacketDecoder` 做边界检查和解析。

