# 资源优化方案

## 当前资源使用情况

### 定时器列表（按优先级分组）
**核心控制（最高优先级）：**
- 5ms: cameraTimer_ (激光相机轮询)
- 5ms: CAN适配器轮询 (candle/pcan)
- 20ms: controlTimer_ (驱动轮控制循环)
- 20ms: watchdog_ (激光纠偏看门狗)

**UI更新（中等优先级）：**
- 20ms: scanUiTimer (超声波形UI)
- 20ms: drainTimer (波形缓存排空)
- 33ms: pollTimer (帧解码器)
- 80ms: commandTimer (机器人控制面板命令发送)

**低频任务（低优先级）：**
- 100ms: clampPollTimer (夹子电机位置轮询)
- 500ms: imuWatchdogTimer, remoteControlTimer, productChrome
- 600ms: autoApplyTimer
- 1000-3000ms: 状态更新、长按检测

### 线程列表
1. **driveThread** - 驱动轮控制（最高优先级）
2. **sensorThread** - IMU/激光传感器（最高优先级）
3. **correctionThread** - 激光纠偏控制（最高优先级）
4. **usbCameraThread** - USB摄像头（中等优先级）
5. **clampMotorController线程** - 夹子电机（低优先级）
6. **decoderThread, waveformCacheThread** - UI数据处理

---

## 优化方案（改动最小化）

### 阶段1：定时器优化（不改变功能）

#### 1.1 降低非关键定时器频率
**目标：减少30-40% CPU占用**

| 当前 | 优化后 | 位置 | 说明 |
|------|--------|------|------|
| scanUiTimer 20ms | 50ms | mainwindow.cpp:166 | UI刷新，人眼无感知 |
| drainTimer 20ms | 50ms | scanwaveformcache.cpp:62 | 批量排空，延迟可接受 |
| pollTimer 33ms | 50ms | framedecoderworker.cpp:18 | 解码器轮询，30fps→20fps |
| commandTimer 80ms | 100ms | robotcontrolpanel.cpp:388 | 控制命令发送，延迟<20ms |
| clampPollTimer 100ms | 150ms | clamp_motor_controller.cpp:45 | 夹子位置反馈，非实时 |

#### 1.2 合并冗余定时器
- **productChrome timer (1000ms)** 与 **remoteControl timer (500ms)** 可共用后者触发

#### 1.3 使用CoarseTimer替代PreciseTimer
保留核心控制（5-20ms）为PreciseTimer，其余改为CoarseTimer：
- commandTimer (80→100ms) → CoarseTimer
- clampPollTimer (100→150ms) → CoarseTimer
- imuWatchdog (500ms) → CoarseTimer
- jogHoldTimer (3000ms) → CoarseTimer

---

### 阶段2：线程优先级调整

```cpp
// 最高优先级：激光/驱动/传感器
driveThread->start(QThread::TimeCriticalPriority);
sensorThread->start(QThread::HighPriority);
correctionThread->start(QThread::HighPriority);

// 正常优先级：USB摄像头
usbCameraThread->start(QThread::NormalPriority);

// 低优先级：夹子电机（可被抢占）
clampController的内部线程设为 LowPriority
```

---

### 阶段3：事件驱动替代轮询（可选）

#### 当前轮询可改为信号驱动：
1. **clampPollTimer (100→150ms)** - 改为CAN消息到达时触发
2. **imuWatchdog (500ms)** - 改为数据超时时才检查
3. **networkWatchdog** - 改为读取超时事件触发

**收益：** 减少20-30%空转CPU

---

## 实施代码（最小改动）

### 修改1：降低UI刷新频率
```cpp
// mainwindow.cpp:166
scanUiTimer->setInterval(50);  // 20→50ms

// scanwaveformcache.cpp:62
m_drainTimer->setInterval(50);  // 20→50ms

// framedecoderworker.cpp:18
m_pollTimer->setInterval(50);  // 33→50ms
```

### 修改2：降低命令和夹子电机轮询频率
```cpp
// robotcontrolpanel.cpp:388
m_commandTimer->setInterval(100);  // 80→100ms
m_commandTimer->setTimerType(Qt::CoarseTimer);

// clamp_motor_controller.cpp:45
pollTimer_->setInterval(150);  // 100→150ms
pollTimer_->setTimerType(Qt::CoarseTimer);
```

### 修改3：设置线程优先级
```cpp
// robotcontrolpanel.cpp，在 start() 之前添加
m_driveThread->start(QThread::TimeCriticalPriority);
m_sensorThread->start(QThread::HighPriority);
m_correctionThread->start(QThread::HighPriority);
m_usbCameraThread->start(QThread::NormalPriority);
```

### 修改4：夹子电机线程降优先级
```cpp
// clamp_motor_controller.cpp，构造函数添加
moveToThread(new QThread);
thread()->start(QThread::LowPriority);
```

---

## 预期收益

| 项目 | 优化前 | 优化后 | 改善 |
|------|--------|--------|------|
| 定时器唤醒/秒 | ~220次 | ~140次 | -36% |
| UI线程CPU | 15-20% | 8-12% | -40% |
| 控制延迟 | <5ms | <5ms | 无变化 |
| 夹子响应 | 100ms | 150ms | +50ms（可接受）|

**总CPU节省：** 30-40%
**实时性影响：** 核心控制（激光/驱动）无影响

---

## 风险评估

✅ **零风险修改：**
- UI刷新 20→50ms（60Hz→20Hz）
- 命令发送 80→100ms
- 夹子轮询 100→150ms

⚠️ **需测试验证：**
- 线程优先级调整（可能触发Windows调度器行为变化）
- 帧解码器 33→50ms（录像回放可能卡顿）

❌ **不建议修改：**
- cameraTimer 5ms（激光实时性）
- controlTimer 20ms（驱动控制50Hz必需）
- watchdog 20ms（安全关键）
