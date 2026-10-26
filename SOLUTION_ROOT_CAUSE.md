# 根因分析：候选焊道却等待定位

## 问题现象
- UI显示："本项焊道候选"（Candidate状态）
- 日志显示："焊道等待定位"，纠偏不启动
- 矛盾：用户看到检测到了焊道，但系统拒绝纠偏

## 根本原因

### 1. 状态机设计缺陷
`seam_state_machine.h` 定义了4个状态：
```
NotDetected → Candidate → Confirmed → Located
```

关键门槛在第283-288行：
```cpp
case SeamLocalizationState::Confirmed:
    // 确认 → 定位：激光中心已校准
    if (calibrationCompleted_) {
        transitionTo(SeamLocalizationState::Located);
        emit seamLocated();
    }
    break;
```

**问题**：`calibrationCompleted_` 标志需要外部调用 `notifyCalibrationComplete()` 才置true。

### 2. 校准信号从未触发
全代码搜索：
```bash
grep -rn "notifyCalibrationComplete" src/
# 结果：0处调用！
```

**结论**：状态机永远卡在 `Confirmed` 状态，无法进入 `Located`。

### 3. 纠偏启动检查
`seam_state_machine.h:333`：
```cpp
bool canStartCorrection() const {
    return currentState_ == SeamLocalizationState::Located;
}
```

`laser_correction_controller.cpp:1554`：
```cpp
if (seamState != SeamLocalizationState::Located) {
    status_.gapValid = false;
    // 不允许纠偏
}
```

**结论**：只有Located状态才能纠偏，但系统永远到不了Located。

---

## 完整因果链

```
检测到焊道(valid=true, confidence>0.1)
  ↓
状态机：NotDetected → Candidate
  ↓
连续3帧稳定(中心位置标准差<2mm)
  ↓
状态机：Candidate → Confirmed  ✅ 日志可能显示"焊道已确认(校准中)"
  ↓
等待 calibrationCompleted_ = true  ❌ 这个标志永远不会被置位
  ↓
状态机：**卡在Confirmed，无法进入Located**
  ↓
laser_correction_controller.cpp:1554行判断：seamState != Located
  ↓
gapValid = false，纠偏不启动
  ↓
日志显示："焊道等待定位"（虽然UI显示"候选"）
```

---

## 用户观察到的症状

### 症状1：UI显示"候选"
- `seam_state_machine.h:320` 返回 `"本项焊道候选"`
- 实际状态可能是 `Candidate` 或 `Confirmed`（描述文本不够精确）

### 症状2：纠偏不启动
- 日志："等待输入: 焊道等待定位"
- 原因：`canStartCorrection() == false`

### 症状3：焊道明明检测到了
- `detection.valid = true`
- `confidence = 0.xxx`（图中显示0.00是因为UI未更新）
- 焊道轮廓清晰可见（蓝色/绿色线条）

---

## 根本矛盾
旧代码设计了一个"激光中心自动校准"概念，但：
1. ✅ 定义了LaserCenterCalibration类
2. ✅ 定义了calibrationCompleted_标志
3. ❌ **从未实现自动校准触发逻辑**
4. ❌ **从未调用notifyCalibrationComplete()**

结果：**完整的状态机，残缺的实现**，焊道永远无法"定位"。

---

## 解决方案（已实现在前面的文档中）

### 方案A：简化状态机（推荐）
删掉Confirmed状态和校准门槛：
```
NotDetected → Candidate(稳定3帧) → Located
```

### 方案B：完整实现自动校准
在Confirmed状态下：
1. 采集N帧焊道中心
2. 计算平均值作为激光坐标系原点
3. 调用`notifyCalibrationComplete()`
4. 自动进入Located

---

## 验证清单
- [ ] 编译通过
- [ ] 启动检测，焊道从Candidate快速进Located（<1秒）
- [ ] Located状态下，UI显示"焊道已定位"
- [ ] 纠偏立即启动，日志无"等待定位"
- [ ] 纠偏平滑（Kp自适应：大偏差2.5，小偏差0.5）
- [ ] 焊道中心自动对齐激光中心（centerCalibration生效）
