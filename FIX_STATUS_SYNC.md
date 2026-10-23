# 【关键修复】焊道定位状态与纠偏同步问题

## 问题现象

**核心矛盾**：
- **UI显示**："本项焊道候选"（Candidate状态）
- **纠偏系统**：显示"焊道等待定位"，无法启动纠偏
- **结果**：机器人停车，无法自动纠偏

## 根本原因

焊道定位状态机有三个阶段：
1. **Candidate（候选）**：首次检测到焊道
2. **Confirmed（确认中）**：稳定检测，进行激光中心校准
3. **Located（已定位）**：校准完成，可以纠偏

**旧代码问题**：
- `status_.gapValid` 在检测到焊道时立即设置为true（1375行）
- 但状态机还停留在 `Candidate` 或 `Confirmed` 阶段
- 纠偏启动条件 `canStartCorrection()` 只在 `Located` 状态返回true
- **导致状态不一致**：检测有效但不能纠偏

## 解决方案

### 1. 统一状态判断点（核心修复）

**laser_correction_controller.cpp 第1979-2019行**：

```cpp
// ========== 更新焊道状态机 ==========
seamStateMachine_.update(detection.valid, adjustedGapLateralM * 1000.0, detection.confidence);
auto seamState = seamStateMachine_.getState();

// 激光中心自动校准（在Confirmed状态）
if (seamState == SeamLocalizationState::Confirmed && !centerCalibration_.isCalibrated()) {
    calibrationSamples_.push_back(rawGapLateralM * 1000.0);
    if (calibrationSamples_.size() >= 5) {
        double avgCenter = sum / calibrationSamples_.size();
        centerCalibration_.calibrate(avgCenter);
        seamStateMachine_.notifyCalibrationComplete();
        emit calibrationCompleted(centerCalibration_.getOffset());
    }
}

// ========== 统一状态同步点 ==========
// 【关键】将状态机的状态同步到status，并只在Located状态设置gapValid=true
status_.seamState = seamState;
status_.seamStateDescription = seamStateMachine_.getStateDescription();
status_.laserCenterCalibrated = centerCalibration_.isCalibrated();
status_.laserCenterOffset = centerCalibration_.getOffset();

if (seamState == SeamLocalizationState::Located) {
    // 校准完成且稳定，可以纠偏
    status_.gapValid = detection.valid;
} else {
    // Candidate/Confirmed 状态：检测到焊道但还在校准中，不能纠偏
    status_.gapValid = false;
}
```

**关键点**：
- ✅ 只有 `Located` 状态才设置 `gapValid=true`
- ✅ UI显示的状态和纠偏判断的状态完全同步
- ✅ 移除了旧的立即设置逻辑，改为状态机统一管理

### 2. 应用激光中心校准偏移

**laser_correction_controller.cpp 第1874-1882行**：

```cpp
// 原始相机坐标系的焊道中心位置
double rawGapLateralM = kCameraLateralToVehicleSign *
                        (detection.absoluteCenterRatio - 0.5) *
                        settings_.imageLateralSpanM;

// 应用激光中心校准偏移（将相机坐标转换到激光坐标）
double adjustedGapLateralM = centerCalibration_.adjustToLaserFrame(rawGapLateralM * 1000.0) / 1000.0;

status_.gapLateralM = adjustedGapLateralM;
```

**作用**：
- 将焊道从相机坐标系平移到激光坐标系
- 实现"把激光内的焊道移到激光中心"的需求
- 后续纠偏计算使用的都是校准后的坐标

### 3. 在Status中添加状态信息

**laser_correction_controller.h 第130-137行**：

```cpp
// 焊道定位状态机相关
SeamLocalizationState seamState = SeamLocalizationState::NotDetected;
QString seamStateDescription;
bool laserCenterCalibrated = false;
double laserCenterOffset = 0.0;  // 激光中心偏移（mm）
```

**用途**：
- UI可以显示当前状态："NotDetected/Candidate/Confirmed/Located"
- 显示校准偏移量，方便调试
- 用户可以看到状态转换过程

## 工作流程

```
焊道首次检测
    ↓
【Candidate】候选状态
    ↓ (稳定检测3帧，置信度>0.6)
【Confirmed】确认中
    ↓ (收集5个样本，计算平均中心)
激光中心校准完成
    ↓
【Located】已定位
    ↓
gapValid = true，可以纠偏
```

## 预期效果

### 之前（有问题）
```
[UI显示] 本项焊道候选
[纠偏系统] 焊道等待定位，置信度 0.00
[结果] 无法启动纠偏，停车
```

### 修复后
```
[UI显示] 本项焊道候选
[纠偏系统] 焊道候选，置信度 0.85，正在校准...
[2秒后] 激光中心校准完成，offset=-1.58mm
[UI显示] 本项焊道已定位
[纠偏系统] 焊道已定位，置信度 0.88，线速度 10.0 mm/s
[结果] 顺滑纠偏
```

## 其他优化（已在seam_state_machine.h中实现）

1. **自适应PID参数**：
   - 大偏差（>3mm）：Kp=2.5, Kd=6.0 快速纠正
   - 中等偏差（1-3mm）：Kp=1.5, Kd=4.0 平衡响应
   - 小偏差（<1mm）：Kp=0.8, Kd=2.5 平滑保持

2. **指数平滑滤波**：
   - 快速响应模式（偏差>5mm）：alpha=0.4
   - 平衡模式（2-5mm）：alpha=0.25
   - 稳定模式（<2mm）：alpha=0.15

3. **激光中心校准**：
   - 收集5个稳定样本
   - 计算平均值作为偏移
   - 自动转换坐标系

## 测试验证

1. **启动纠偏**：
   - 放置焊道在相机视野内
   - 观察UI显示状态变化：NotDetected → Candidate → Confirmed → Located
   - 确认"已定位"后，点击"启动自动纠偏"

2. **观察纠偏过程**：
   - 检查日志中的 `[LASER.CALIBRATION]` 输出
   - 确认校准偏移值合理（通常-2mm ~ 2mm）
   - 观察纠偏曲线是否平滑，无大幅震荡

3. **验证状态同步**：
   - UI显示"本项焊道已定位"时，纠偏系统应显示"焊道已定位，置信度 XX"
   - 不应再出现"等待焊道定位"的情况

## 关键文件修改清单

| 文件 | 修改行数 | 主要改动 |
|------|---------|---------|
| `src/laser_correction_controller.cpp` | 1874-2019 | 应用校准偏移、统一状态同步 |
| `src/laser_correction_controller.h` | 130-137 | 添加状态字段 |
| `src/seam_state_machine.h` | 全部 | 状态机、PID、滤波器（已存在）|

## 注意事项

1. **不要跳过状态**：状态机必须按 Candidate → Confirmed → Located 顺序执行
2. **校准只进行一次**：在Confirmed状态完成后，后续不再重新校准
3. **坐标系统一**：所有纠偏计算使用校准后的激光坐标系
4. **UI与内核同步**：通过status字段确保UI显示与内核状态一致

---

**修复完成日期**：2026-10-23  
**问题严重度**：CRITICAL（无法纠偏，机器人停车）  
**修复验证**：需要实际焊道测试
