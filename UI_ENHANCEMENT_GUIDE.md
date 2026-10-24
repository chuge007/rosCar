# UI显示增强建议

## 当前问题

用户看到的UI信息与内部状态不同步：
- **轮廓面板**显示"本项焊道候选"
- **纠偏面板**显示"焊道等待定位"
- **用户困惑**：明明检测到了，为什么不能纠偏？

## 解决方案：统一显示焊道状态

### 1. 在轮廓显示面板显示完整状态

**当前显示**：
```
基线+肩部: 本项焊道候选
```

**改进后**：
```
基线+肩部: 焊道已定位 ✓ (校准偏移: -1.58mm)
或
基线+肩部: 焊道确认中... (正在校准激光中心)
或
基线+肩部: 焊道候选 (置信度: 0.85)
```

**实现代码**（推测在UI更新函数中）：
```cpp
QString stateText = status.seamStateDescription;
if (status.laserCenterCalibrated) {
    stateText += QString(" ✓ (偏移: %1mm)").arg(status.laserCenterOffset, 0, 'f', 2);
} else if (status.seamState == SeamLocalizationState::Confirmed) {
    stateText += " (正在校准...)";
} else if (status.seamState == SeamLocalizationState::Candidate) {
    stateText += QString(" (置信度: %1)").arg(status.confidence, 0, 'f', 2);
}
ui->seamStateLabel->setText(stateText);
```

### 2. 纠偏面板状态同步

**当前显示**：
```
等待输入: 焊道等待定位; 置信度 0.00; 线速度 0.0 mm/s; 角速度 0.00 deg/s
```

**改进后应该直接使用 `status.seamStateDescription`**：
```cpp
QString correctionStatus;
if (!status.gapValid) {
    // 使用状态机的描述，而不是硬编码"焊道等待定位"
    correctionStatus = QString("等待输入: %1; 置信度 %2")
        .arg(status.seamStateDescription)
        .arg(status.confidence, 0, 'f', 2);
} else {
    correctionStatus = QString("焊道已定位; 置信度 %1; 线速度 %2 mm/s; 角速度 %3 deg/s")
        .arg(status.confidence, 0, 'f', 2)
        .arg(status.linearCommandMps * 1000.0, 0, 'f', 1)
        .arg(qRadiansToDegrees(status.angularCommandRadps), 0, 'f', 2);
}
```

### 3. 添加状态转换提示

在状态转换时显示Toast通知：

```cpp
// 在状态机状态改变时
connect(laserController, &LaserCorrectionController::seamStateChanged,
        this, [this](SeamLocalizationState oldState, SeamLocalizationState newState) {
    QString message;
    switch (newState) {
        case SeamLocalizationState::Candidate:
            message = "检测到焊道候选，正在确认...";
            break;
        case SeamLocalizationState::Confirmed:
            message = "焊道确认，正在校准激光中心...";
            break;
        case SeamLocalizationState::Located:
            message = "焊道定位完成，可以启动纠偏！";
            showSuccessToast(message);
            return;
        case SeamLocalizationState::NotDetected:
            message = "焊道丢失";
            break;
    }
    showInfoToast(message);
});
```

## 视觉反馈增强

### 1. 状态指示灯

在UI上添加一个状态指示器：

```
🔴 NotDetected    (红色圆点)
🟡 Candidate      (黄色圆点，闪烁)
🟠 Confirmed      (橙色圆点，快速闪烁)
🟢 Located        (绿色圆点，常亮)
```

### 2. 进度条

在Confirmed→Located转换期间显示校准进度：

```
激光中心校准: [███░░] 3/5 samples
```

### 3. 焊道中心可视化

在图像叠加层显示：
- **红色虚线**：激光中心（x=0）
- **蓝色虚线**：当前焊道中心
- **绿色箭头**：校准偏移方向和大小

```cpp
// 在图像渲染时
QPainter painter(&image);
painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
painter.drawLine(laserCenterX, 0, laserCenterX, height);  // 激光中心

if (status.gapValid) {
    int seamCenterX = laserCenterX + status.laserCenterOffset * pixelsPerMm;
    painter.setPen(QPen(Qt::blue, 2, Qt::DashLine));
    painter.drawLine(seamCenterX, 0, seamCenterX, height);  // 焊道中心
    
    // 绘制偏移箭头
    painter.setPen(QPen(Qt::green, 2));
    painter.drawLine(seamCenterX, height/2, laserCenterX, height/2);
    drawArrow(&painter, seamCenterX, height/2, laserCenterX, height/2);
}
```

## 完整UI布局建议

```
┌─────────────────────────────────────────────────┐
│  SDK 实时轮廓与焊道定位                         │
├─────────────────────────────────────────────────┤
│  纠偏显示方式: [XZ ▼]  [建立/管理模板]          │
│  □ 记录检测计算日志  定位算法: [基线+肩部(默认)▼]│
├─────────────────────────────────────────────────┤
│  X/Z 轮廓                                       │
│  ┌───────────────────────────────────────────┐  │
│  │                                           │  │
│  │  [焊道轮廓图]                             │  │
│  │  红线=激光中心  蓝线=焊道中心             │  │
│  │  绿箭头=校准偏移                          │  │
│  │                                           │  │
│  └───────────────────────────────────────────┘  │
│  🟢 焊道已定位 (校准偏移: -1.58mm)              │
│                                                 │
│  SDK #226447 | 3177/3200 | 5 ms                │
├─────────────────────────────────────────────────┤
│  激光自动纠偏                                   │
│  ┌───────────────────────────────────────────┐  │
│  │ [启动自动纠偏]    [停止自动纠偏]         │  │
│  └───────────────────────────────────────────┘  │
│  ✓ 焊道已定位; 置信度 0.88; 线速度 10.0 mm/s;│
│    角速度 0.15 deg/s                            │
│                                                 │
│  纠偏参数请在"小车配置"中设置。                 │
│  ┌───────────────────────────────────────────┐  │
│  │ 纠偏速度 (mm/s)  [10.0           ] ●     │  │
│  │ 分段长度 (mm)    [20.0           ] ●     │  │
│  │ Kp              [2.00            ] ●     │  │
│  │ Kd              [5.00            ] ●     │  │
│  └───────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

## 实现优先级

### P0 - 立即实现（已完成）
- [x] 状态机与gapValid同步
- [x] 激光中心校准偏移应用
- [x] Status结构添加状态字段

### P1 - 高优先级（建议本周完成）
- [ ] 纠偏面板显示改为使用 `status.seamStateDescription`
- [ ] 添加状态指示灯（🟢🟡🔴）
- [ ] 在轮廓面板显示校准偏移量

### P2 - 中优先级（可选）
- [ ] 状态转换Toast通知
- [ ] 校准进度条
- [ ] 焊道中心可视化（红/蓝虚线+绿色箭头）

### P3 - 低优先级（锦上添花）
- [ ] 历史曲线：焊道偏移随时间变化
- [ ] 纠偏轨迹记录和回放
- [ ] 参数自动调优建议

## UI文件查找

需要修改的UI文件可能在：
```bash
# 搜索包含"焊道等待定位"的文件
grep -r "焊道等待定位" --include="*.cpp" --include="*.ui"

# 搜索包含"本项焊道"的文件
grep -r "本项焊道" --include="*.cpp" --include="*.ui"

# 搜索LaserCorrectionStatus的使用
grep -r "LaserCorrectionStatus" --include="*.cpp" --include="*.h"
```

**常见位置**：
- `src/robot_usb_camera_controller.cpp` - 可能包含UI更新逻辑
- `src/main_window.cpp` 或 `src/control_panel.cpp` - 主界面控制
- `ui/*.ui` - Qt Designer界面文件

## 调试建议

### 添加详细日志

```cpp
// 在status更新后添加
qDebug() << "[UI.STATE]"
         << "seamState=" << static_cast<int>(status.seamState)
         << "gapValid=" << status.gapValid
         << "calibrated=" << status.laserCenterCalibrated
         << "offset=" << status.laserCenterOffset;
```

### 监控状态转换

```cpp
// 保存上一次的状态，检测变化
static SeamLocalizationState lastState = SeamLocalizationState::NotDetected;
if (status.seamState != lastState) {
    qInfo() << "[STATE.CHANGE]"
            << stateToString(lastState) << "→" << stateToString(status.seamState);
    lastState = status.seamState;
}
```

---

**文档版本**：v1.0  
**更新日期**：2026-10-23  
**适用版本**：PA1664WorkbenchTmp  
