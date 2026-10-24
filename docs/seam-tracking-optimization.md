# 焊道跟踪与纠偏优化方案

## 问题分析

根据日志和界面截图，识别出三个关键问题：

### 1. 状态显示矛盾
**现象**：
- 界面显示"基线+肩部：本项焊道候选"
- 纠偏日志显示"等待输入：焊道等待定位"
- 用户点击"启动自动纠偏"后继续等待

**根本原因**：
- 焊道检测器已标记焊道为`候选`状态（detected but not confirmed）
- 纠偏控制器需要`已定位`状态（located and confirmed）才能启动
- 状态机缺少从候选到定位的自动转换逻辑

### 2. 纠偏摆动过大
**日志数据**（center_x波动）：
```
-1578.299 → -1578.827 → -1578.224 → -1578.704 → -1578.857 → -1579.051
```
波动范围：约0.8mm，相当于±0.4mm摆动

**当前参数**：
- 纠偏速度：10.0 mm/s
- 分段长度：20.0 mm
- Kp：2.00（比例增益过大）
- Kd：5.00（微分增益过大）

**问题**：PID参数偏高导致过度响应，产生振荡

### 3. 激光中心对齐
**测量数据**：
- 焊道中心位置：约-1579mm（camera坐标系）
- 激光扫描中心：0mm
- 偏移量：~1579mm

**需求**：将检测到的焊道平移到激光视野中心

## 优化方案

### 方案一：状态机重构（推荐）

#### 1.1 引入三级定位状态
```cpp
enum class SeamLocalizationState {
    NotDetected,      // 未检测
    Candidate,        // 候选（检测到但未确认）
    Confirmed,        // 已确认（连续N帧稳定）
    Located          // 已定位（可用于纠偏）
};
```

#### 1.2 状态转换逻辑
```
NotDetected → Candidate: 检测器首次发现焊道特征
Candidate → Confirmed:   连续3帧位置稳定（<2mm漂移）
Confirmed → Located:     建立模板并完成中心校准
Located → Candidate:     检测丢失或置信度下降
```

#### 1.3 实现要点
- `Candidate`状态：显示"本项焊道候选"，纠偏按钮灰色
- `Confirmed`状态：显示"焊道已确认"，纠偏按钮可用但警告
- `Located`状态：显示"焊道已定位"，纠偏按钮高亮可启动

#### 1.4 代码修改位置
```cpp
// laser_correction_controller.cpp
void LaserCorrectionController::updateSeamState(const ProfileDetection& detection) {
    if (!detection.valid) {
        seamState_ = SeamLocalizationState::NotDetected;
        return;
    }
    
    // 候选 → 确认
    if (seamState_ == SeamLocalizationState::Candidate) {
        if (isCenterStable(detection.centerX, 3, 0.002)) {  // 3帧，2mm阈值
            seamState_ = SeamLocalizationState::Confirmed;
            emit seamConfirmed();
        }
    }
    
    // 确认 → 定位
    if (seamState_ == SeamLocalizationState::Confirmed) {
        if (templateEstablished_ && centerCalibratedToLaser_) {
            seamState_ = SeamLocalizationState::Located;
            emit seamLocated();
        }
    }
}
```

### 方案二：自适应PID参数

#### 2.1 动态参数调整
根据偏差大小和运动状态分段调整PID：

| 偏差范围 | Kp | Kd | 场景 |
|---------|----|----|------|
| > 10mm | 2.5 | 3.0 | 大偏差快速纠正 |
| 5~10mm | 1.5 | 2.0 | 中等偏差稳定逼近 |
| 2~5mm  | 1.0 | 1.5 | 小偏差精细调整 |
| < 2mm  | 0.5 | 1.0 | 接近中心平滑保持 |

#### 2.2 实现代码
```cpp
struct AdaptivePIDParams {
    double getKp(double error_mm) const {
        double abs_err = std::abs(error_mm);
        if (abs_err > 10.0) return 2.5;
        if (abs_err > 5.0)  return 1.5;
        if (abs_err > 2.0)  return 1.0;
        return 0.5;
    }
    
    double getKd(double error_mm) const {
        double abs_err = std::abs(error_mm);
        if (abs_err > 10.0) return 3.0;
        if (abs_err > 5.0)  return 2.0;
        if (abs_err > 2.0)  return 1.5;
        return 1.0;
    }
};

double LaserCorrectionController::computeCorrectionSpeed(
    double centerError, double derivative) {
    AdaptivePIDParams params;
    double kp = params.getKp(centerError);
    double kd = params.getKd(centerError);
    
    double correction = kp * centerError + kd * derivative;
    return std::clamp(correction, -config_.maxSpeed, config_.maxSpeed);
}
```

#### 2.3 滤波增强
添加指数滑动平均滤波减少噪声：

```cpp
class CenterFilter {
    double alpha_ = 0.3;  // 滤波系数
    double filtered_ = 0.0;
    
public:
    double update(double raw) {
        filtered_ = alpha_ * raw + (1 - alpha_) * filtered_;
        return filtered_;
    }
};
```

### 方案三：激光中心自动校准

#### 3.1 校准流程
```
1. 检测焊道获取初始center_x（camera坐标）
2. 计算偏移量：offset = detected_center_x - laser_center_x
3. 后续检测自动应用偏移：adjusted_x = raw_x - offset
4. 纠偏控制器使用adjusted_x作为误差输入
```

#### 3.2 实现代码
```cpp
class LaserCenterCalibration {
    double cameraToLaserOffset_ = 0.0;
    bool calibrated_ = false;
    
public:
    void calibrate(double detectedCenterX, double laserCenterX = 0.0) {
        cameraToLaserOffset_ = detectedCenterX - laserCenterX;
        calibrated_ = true;
        qInfo() << "激光中心已校准，偏移量:" << cameraToLaserOffset_ << "mm";
    }
    
    double adjustToLaserFrame(double rawCameraX) const {
        if (!calibrated_) return rawCameraX;
        return rawCameraX - cameraToLaserOffset_;
    }
    
    bool isCalibrated() const { return calibrated_; }
};

// 使用示例
void LaserCorrectionController::processDetection(const ProfileDetection& det) {
    if (!centerCalibration_.isCalibrated() && det.valid && det.confidence > 0.8) {
        centerCalibration_.calibrate(det.centerX);
        centerCalibratedToLaser_ = true;
    }
    
    double adjustedCenter = centerCalibration_.adjustToLaserFrame(det.centerX);
    double error = adjustedCenter;  // 目标是0
    
    // 使用error进行PID纠偏控制
}
```

## 完整集成方案

### 实施步骤

#### Step 1: 状态机重构
```cpp
// laser_correction_controller.h
class LaserCorrectionController : public QObject {
private:
    SeamLocalizationState seamState_ = SeamLocalizationState::NotDetected;
    LaserCenterCalibration centerCalibration_;
    AdaptivePIDParams adaptivePID_;
    CenterFilter centerFilter_;
    
    std::vector<double> recentCenters_;
    int confirmationFrames_ = 0;
    
    bool isCenterStable(double newCenter, int minFrames, double tolerance);
    void updateSeamState(const ProfileDetection& detection);
};
```

#### Step 2: 检测流程改造
```cpp
void LaserCorrectionController::onProfileDetected(const ProfileDetection& detection) {
    // 1. 更新状态机
    updateSeamState(detection);
    
    // 2. 自动校准激光中心（仅首次）
    if (seamState_ == SeamLocalizationState::Confirmed && 
        !centerCalibration_.isCalibrated()) {
        centerCalibration_.calibrate(detection.centerX);
    }
    
    // 3. 坐标转换
    double laserFrameCenter = centerCalibration_.adjustToLaserFrame(detection.centerX);
    
    // 4. 滤波
    double filteredCenter = centerFilter_.update(laserFrameCenter);
    
    // 5. 纠偏控制（仅Located状态）
    if (seamState_ == SeamLocalizationState::Located && correctionActive_) {
        double error = filteredCenter;  // 目标中心=0
        double derivative = (error - lastError_) / dt;
        
        double kp = adaptivePID_.getKp(error);
        double kd = adaptivePID_.getKd(error);
        double speed = kp * error + kd * derivative;
        
        speed = std::clamp(speed, -config_.maxSpeed, config_.maxSpeed);
        applyCorrection(speed);
        
        lastError_ = error;
    }
    
    // 6. 日志输出
    logDetection(detection, seamState_, laserFrameCenter, filteredCenter);
}
```

#### Step 3: UI反馈优化
```cpp
// robotcontrolpanel.cpp
void RobotControlPanel::updateSeamStatus(SeamLocalizationState state) {
    switch (state) {
    case SeamLocalizationState::NotDetected:
        lblSeamStatus->setText("未检测到焊道");
        btnStartCorrection->setEnabled(false);
        break;
        
    case SeamLocalizationState::Candidate:
        lblSeamStatus->setText("本项焊道候选");
        btnStartCorrection->setEnabled(false);
        lblSeamStatus->setStyleSheet("color: orange;");
        break;
        
    case SeamLocalizationState::Confirmed:
        lblSeamStatus->setText("焊道已确认（校准中）");
        btnStartCorrection->setEnabled(false);
        lblSeamStatus->setStyleSheet("color: yellow;");
        break;
        
    case SeamLocalizationState::Located:
        lblSeamStatus->setText("焊道已定位");
        btnStartCorrection->setEnabled(true);
        lblSeamStatus->setStyleSheet("color: green; font-weight: bold;");
        break;
    }
}
```

### 参数配置界面增强

建议在"激光自动纠偏"面板添加以下选项：

```
┌─ 激光自动纠偏 ────────────────────────┐
│ 纠偏模式：○ 固定PID  ● 自适应PID     │
│                                        │
│ 【固定PID模式】                        │
│ Kp:  [1.0 ] 范围 0.1~5.0              │
│ Kd:  [1.5 ] 范围 0.1~10.0             │
│                                        │
│ 【自适应PID模式】                      │
│ 大偏差Kp: [2.5] 小偏差Kp: [0.5]       │
│ 大偏差Kd: [3.0] 小偏差Kd: [1.0]       │
│                                        │
│ 纠偏速度 (mm/s): [8.0 ] 范围 1~20     │
│ 分段长度 (mm):   [25.0] 范围 1~30   │
│                                        │
│ ☑ 启用中心滤波  滤波系数: [0.3]       │
│ ☑ 自动激光中心校准                    │
│                                        │
│ [ 启动自动纠偏 ] [ 停止自动纠偏 ]     │
└────────────────────────────────────────┘
```

### 预期效果

#### 状态清晰度
- 用户能明确看到从"候选"到"确认"到"定位"的进度
- "启动纠偏"按钮仅在真正ready时可用

#### 纠偏平滑度
- 大偏差快速响应，小偏差精细调整
- 中心位置波动从±0.4mm降低到±0.1mm
- 避免过冲和振荡

#### 操作便利性
- 首次检测到焊道自动完成激光中心校准
- 无需手动设置偏移量
- 纠偏轨迹以激光中心为参考，逻辑直观

## 测试验证

### 测试Case 1: 状态机转换
```
步骤：
1. 启动相机和激光
2. 移动到焊道附近
3. 观察界面状态变化：未检测 → 候选 → 确认 → 定位
4. 验证"启动纠偏"按钮在"已定位"时才可用

预期：状态转换平滑，UI反馈及时
```

### 测试Case 2: 自适应PID
```
步骤：
1. 从大偏差位置（>10mm）启动纠偏
2. 记录纠偏轨迹和速度曲线
3. 对比固定PID和自适应PID的摆动幅度

预期：自适应模式下接近中心时速度自动降低，摆动<0.2mm
```

### 测试Case 3: 激光中心校准
```
步骤：
1. 记录检测器输出的raw center_x（camera坐标）
2. 启动纠偏，观察日志中adjusted center_x
3. 验证adjusted值在0附近小幅波动

预期：校准后焊道中心映射到激光坐标系0点，纠偏逻辑简化
```

## 风险与应对

### 风险1: 状态转换误判
**场景**：环境噪声导致候选焊道频繁出现/消失
**应对**：增加确认帧数（3→5帧），提高稳定性阈值（2mm→3mm）

### 风险2: 自适应PID抖动
**场景**：偏差在阈值边界反复跳跃
**应对**：添加迟滞区间，Kp切换时使用渐变过渡

### 风险3: 激光中心校准偏差
**场景**：首次检测时焊道本身就偏离目标位置
**应对**：提供手动重新校准接口，或在多次检测后取平均值

## 总结

本方案通过三层优化：
1. **状态机重构** - 解决"候选"与"等待定位"矛盾
2. **自适应PID** - 消除纠偏摆动，提升跟踪平滑度
3. **中心校准** - 简化坐标转换，统一激光与相机视野

实施后预期：
- 纠偏启动逻辑清晰，用户操作顺畅
- 跟踪精度提升至±0.1mm量级
- 代码结构优雅，便于后续扩展（如多焊道切换、模板学习）

---
*文档版本: v1.0*  
*创建时间: 2026-10-23*

