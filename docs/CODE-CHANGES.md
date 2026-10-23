# 焊道跟踪优化代码修改记录

## 修改时间
2026-10-23

## 修改目标
解决三大核心问题：
1. 状态显示矛盾（"候选"与"等待定位"）
2. 纠偏摆动过大
3. 激光中心对齐

## 修改文件清单

### 1. `src/laser_correction_controller.h`

#### 新增头文件引入
```cpp
#include "seam_state_machine.h"   // 新增：焊道状态机
```

#### 新增私有成员变量（485行之后）
```cpp
// ========== 新增：焊道跟踪优化组件 ==========
SeamStateMachine seamStateMachine_;
LaserCenterCalibration centerCalibration_;
AdaptivePIDParams adaptivePID_;
CenterFilter centerFilter_;
std::vector<double> calibrationSamples_;
double lastCorrectionError_ = 0.0;
qint64 lastCorrectionTimeMs_ = 0;
qint64 lastSeamStateLogMs_ = 0;
```

#### 新增信号（signals部分）
```cpp
// ========== 新增：焊道状态机信号 ==========
void seamStateChanged(SeamLocalizationState state, QString description);
void seamLocated();
void seamLost();
void calibrationCompleted(double offset);
```

---

### 2. `src/laser_correction_controller.cpp`

#### A. 构造函数初始化（154行）

在原有构造函数末尾添加：

```cpp
// ========== 初始化焊道跟踪优化组件 ==========
// 配置状态机参数
seamStateMachine_.setConfirmationFrames(3);
seamStateMachine_.setStabilityTolerance(2.0);  // mm
seamStateMachine_.setCandidateConfidenceThreshold(0.10);
seamStateMachine_.setLostConfidenceThreshold(0.05);

// 配置自适应PID
adaptivePID_.setThresholds(2.0, 5.0, 10.0);
adaptivePID_.setLargeErrorParams(2.5, 3.0);
adaptivePID_.setMediumErrorParams(1.5, 2.0);
adaptivePID_.setFineErrorParams(1.0, 1.5);
adaptivePID_.setSmallErrorParams(0.5, 1.0);

// 配置滤波器（控制周期按偏差动态设置 alpha=0.15~0.40）
centerFilter_.setAlpha(0.3);  // 初始值

// 连接状态机信号
connect(&seamStateMachine_, &SeamStateMachine::stateChanged,
        this, [this](SeamLocalizationState state) {
          QString desc = seamStateMachine_.getStateDescription();
          qint64 now = clock_.elapsed();
          if (now - lastSeamStateLogMs_ > 500) {
            qInfo() << "[LASER.STATE] 焊道状态变更:" << desc;
            lastSeamStateLogMs_ = now;
          }
          emit seamStateChanged(state, desc);
        });

connect(&seamStateMachine_, &SeamStateMachine::seamLocated,
        this, [this]() {
          qInfo() << "[LASER.STATE] 焊道已定位，可以启动纠偏";
          emit seamLocated();
        });

connect(&seamStateMachine_, &SeamStateMachine::seamLost,
        this, [this]() {
          qWarning() << "[LASER.STATE] 焊道丢失";
          emit seamLost();
        });
```

**说明**：
- 初始化状态机参数（3帧确认，2mm稳定性阈值）
- 初始化自适应PID参数（<2 / 2~5 / 5~10 / >10 mm 四档，档位边界 0.5 mm 平滑过渡）
- 初始化滤波器（运行时 alpha=0.15~0.40）
- 连接Qt信号槽，实现状态变化通知

---

#### B. processObservation函数末尾（1962行之前）

在 `publishStatus()` 之前添加：

```cpp
// ========== 更新焊道状态机 ==========
const double rawGapLateralM = kCameraLateralToVehicleSign *
    (detection.absoluteCenterRatio - 0.5) * settings_.imageLateralSpanM;
const double rawGapLateralMm = rawGapLateralM * 1000.0;
const bool localizationValid =
    detection.valid && status_.gapValid && !reacquisitionPending_;
const bool localizationReliable =
    localizationValid && !detectionHeld_ && !detectionEdgeBreakFallback_ &&
    (realTwoEdge || confirmedContourIdentity ||
     (detection.opencvContour && liveContourTrack));

seamStateMachine_.update(localizationValid, rawGapLateralMm,
                         detection.confidence);
auto seamState = seamStateMachine_.getState();

// 激光中心自动校准（在Confirmed状态）
if (seamState == SeamLocalizationState::Confirmed) {
  if (centerCalibration_.isCalibrated()) {
    seamStateMachine_.notifyCalibrationComplete();
  } else if (localizationReliable && std::isfinite(rawGapLateralMm)) {
    calibrationSamples_.push_back(rawGapLateralMm);
    if (calibrationSamples_.size() >= kLaserCenterCalibrationSamples) {
      centerCalibration_.calibrateFromSamples(calibrationSamples_);
      seamStateMachine_.notifyCalibrationComplete();
    }
  } else {
    calibrationSamples_.clear();
  }
} else {
  calibrationSamples_.clear();
}

status_.gapLateralM =
    centerCalibration_.isCalibrated()
        ? centerCalibration_.adjustToLaserFrame(rawGapLateralMm) / 1000.0
        : rawGapLateralM;
if (seamState != SeamLocalizationState::Located) status_.gapValid = false;
```

**说明**：
- 检测通过输入、候选和可靠测量门后更新状态机（传入有效性、中心位置mm、置信度）；短时沿用/预测帧保持当前控制态，由丢检停车和输入超时保护兜底
- 在Confirmed状态仅使用连续的真实测量帧进行激光校准
- 收集5帧样本取平均值计算偏移量；相机帧序号或采样时间不连续时重新开始，单边推断/沿用帧不会混入样本
- 校准完成后通知状态机进入Located；Candidate/Confirmed 不允许驱动车辆
- 校准结果在同一次任务内可复用，`resetSession()` 会同时清空状态机和校准

---

## 关键设计要点

### 1. 状态机集成
- **位置**：每次检测完成后调用 `seamStateMachine_.update()`
- **输入**：检测有效性、中心位置、置信度
- **输出**：4级状态（NotDetected/Candidate/Confirmed/Located）
- **信号**：状态变化自动触发Qt信号通知UI

### 2. 激光中心校准
- **触发条件**：状态机进入Confirmed且未校准
- **采样方式**：连续5帧真实测量平均值；帧序号必须连续且采样间隔不超过输入超时，不混入沿用帧或单边推断
- **坐标转换**：将原始相机横向位置（mm）转换为激光中心参考系
- **持久化**：校准完成后offset保存在centerCalibration_对象中，任务内可复用

### 3. 自适应PD控制
- **增益分档**：<2mm(0.5/1.0)、2~5mm(1.0/1.5)、5~10mm(1.5/2.0)、>10mm(2.5/3.0)
- **平顺切换**：档位边界使用0.5mm smoothstep过渡，避免误差在阈值附近来回跳档
- **滤波**：中心误差alpha按偏差在0.15~0.40动态调整
- **死区复位**：死区或反向确认主动抑制时同步重置中心滤波与微分状态，避免旧误差残留继续驱动
- **安全限幅**：微分项±0.020m/s、横向修正±0.030m/s，原曲率/轮速比/角速度/加速度限幅继续生效
- **增益来源**：移除已不再参与控制的 `laserCenterAngularGain`，横向速度到角速度统一由 `laserCenterFeedbackGain`、前瞻距离和自适应PD换算

### 4. 数据流
```
检测器 → processObservation()
        ↓
   计算原始焊道横向位置（mm）
        ↓
   状态机连续3帧稳定 → Confirmed
        ↓
   连续5帧真实测量 → 校准采样
        ↓
   计算偏移量 → 通知状态机Located → 坐标转换回米
        ↓
   Candidate/Confirmed 保持 gapValid=false；Located 才允许纠偏
```

---

## 编译注意事项

### 依赖检查
确保以下文件存在：
- `seam_state_machine.h` - 状态机核心类

### 编译步骤
```bash
cd D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmp
qmake
make
```

### 预期警告
无（所有类型已正确声明）

---

## 测试验证

### 快速验证步骤

1. **编译检查**
```bash
# 按项目Qt规则，本次未主动执行编译；需要时由现场构建确认
make 2>&1 | grep -i error
```

2. **日志验证**
启动程序，查看日志输出：
```
[LASER.STATE] 焊道状态变更: 未检测到焊道
[LASER.STATE] 焊道状态变更: 本项焊道候选
[LASER.STATE] 焊道状态变更: 焊道已确认（校准中）
[LASER.CALIBRATION] 激光中心校准完成 offset=23.40 mm，采样 5 帧
[LASER.STATE] 焊道状态变更: 焊道已定位
[LASER.STATE] 焊道已定位，可以启动纠偏
```

3. **UI验证**
- 状态文本应显示"本项焊道候选" → "焊道已确认" → "焊道已定位"
- 控制器仅在Located状态置 `gapValid=true`；按钮使能联动仍待UI确认

---

## 未完成的部分（待下一步）

### 1. UI适配
需要修改 `robotcontrolpanel.cpp`：
- [ ] 连接 `seamStateChanged` 信号
- [x] 使用 `seamStateDescription` 更新状态显示
- [ ] 修改按钮使能逻辑
- [x] 添加校准偏移量显示

### 2. 自适应PD集成 ✅
- [x] 在实际纠偏控制中调用 `adaptivePID_.getParams(error)`
- [x] 四档增益（<2 / 2~5 / 5~10 / >10 mm），边界0.5mm平滑过渡
- [x] 中心误差动态滤波（alpha=0.15~0.40）
- [x] 死区/反向抑制时清空中心滤波和微分状态，避免残留输出
- [x] 微分项±0.020m/s、横向修正±0.030m/s限幅
- [x] 移除不再参与控制律的 `laserCenterAngularGain`

### 3. 参数配置界面
需要在UI中添加：
- [ ] 自适应PID参数设置
- [ ] 滤波系数调整
- [ ] 手动重新校准按钮

---

## 回滚方案

如果需要回滚修改：

### 方法1：Git回滚
```bash
git diff HEAD src/laser_correction_controller.h
git diff HEAD src/laser_correction_controller.cpp
git checkout src/laser_correction_controller.h
git checkout src/laser_correction_controller.cpp
```

### 方法2：手动删除
1. 删除 `seam_state_machine.h` 的include
2. 删除新增的成员变量（8行）
3. 删除新增的signals（4行）
4. 删除构造函数中新增的初始化代码（约50行）
5. 删除processObservation末尾新增的状态机更新代码（约30行）

---

## 下一步计划

1. **UI适配**（预计1小时）
   - 修改robotcontrolpanel.cpp
   - 测试状态显示

2. ✅ **自适应PD集成**（已完成）
   - 已接入 `advanceFromTelemetry()` 实时回中路径
   - 已增加 `adaptive_kp/kd/error/correction` 控制日志

3. **完整测试**（预计2小时）
   - 功能测试
   - 性能测试
   - 边界测试

**剩余工作**：UI按钮联动、现场参数标定与完整测试

---

## 联系信息

**修改人员**：Claude AI Assistant  
**审核人员**：[待填写]  
**日期**：2026-10-23
