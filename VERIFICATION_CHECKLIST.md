# 【快速验证清单】焊道定位与纠偏同步修复

## ✅ 代码修改验证

### 1. 核心修改点检查

- [x] **laser_correction_controller.cpp:1874-1882**  
  应用激光中心校准偏移，生成 `adjustedGapLateralM`

- [x] **laser_correction_controller.cpp:1979-2019**  
  状态机更新后统一同步status，只在Located状态设置gapValid=true

- [x] **laser_correction_controller.h:130-137**  
  添加 `seamState`, `seamStateDescription`, `laserCenterCalibrated`, `laserCenterOffset` 字段

### 2. 关键逻辑确认

```cpp
// ✅ 使用原始坐标进行校准（避免循环）
calibrationSamples_.push_back(rawGapLateralM * 1000.0);

// ✅ 应用校准后的坐标给状态机
seamStateMachine_.update(detection.valid, adjustedGapLateralM * 1000.0, detection.confidence);

// ✅ 只在Located状态允许纠偏
if (seamState == SeamLocalizationState::Located) {
    status_.gapValid = detection.valid;
} else {
    status_.gapValid = false;
}
```

## ✅ 编译验证

```bash
cd D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmp
# 如果使用Qt Creator，直接点击"构建"按钮
# 如果使用命令行：
make clean && make
```

**期望结果**：
- ✅ 无编译错误
- ✅ 无链接错误
- ⚠️ 可能有警告（未使用的变量等），可忽略

## ✅ 运行时验证

### 第一步：启动程序，放置焊道

1. 启动机器人程序
2. 打开"SDK 实时轮廓与焊道定位"面板
3. 将焊道放置在相机视野内（最好偏离中心，方便观察校准效果）

**观察点**：
- [ ] 焊道被检测到（蓝色/绿色轮廓线）
- [ ] 状态显示："本项焊道候选"
- [ ] 纠偏面板显示："焊道等待定位"或"焊道候选" ✅

### 第二步：等待状态转换

**约2-3秒后应该看到**：
- [ ] 状态变为："本项焊道确认中"（Confirmed）
- [ ] 控制台输出：`[LASER.CALIBRATION] 激光中心校准完成 offset=XXX mm`
- [ ] 状态变为："本项焊道已定位"（Located）
- [ ] 纠偏面板显示："焊道已定位，置信度 0.XX" ✅

**如果卡在"候选"状态**：
- 检查置信度是否 > 0.6
- 检查是否连续3帧检测到焊道
- 移动焊道位置，让其更清晰

### 第三步：启动纠偏

1. 点击"启动自动纠偏"按钮

**观察点**：
- [ ] 立即开始纠偏（不再显示"等待焊道定位"）✅
- [ ] 纠偏面板显示实时数据：
  - 线速度：10.0 mm/s
  - 角速度：X.XX deg/s
  - 置信度：0.XX
- [ ] 机器人开始移动，跟随焊道 ✅

### 第四步：观察纠偏效果

**焊道偏离中心时**：
- [ ] 机器人能顺滑转向纠正
- [ ] 无大幅摆动或震荡
- [ ] 纠偏曲线平滑（查看X/Z轮廓图）

**焊道接近中心时**：
- [ ] 纠偏量减小，速度降低
- [ ] 保持平稳跟随
- [ ] 焊道逐渐移向激光中心（红色竖线位置）

## ⚠️ 常见问题排查

### 问题1：仍然显示"等待焊道定位"

**可能原因**：
- 状态机未正确更新
- `status_.gapValid` 仍然被旧逻辑设置

**检查**：
```bash
# 搜索所有设置gapValid的地方
grep -n "gapValid.*=" src/laser_correction_controller.cpp
```

**期望结果**：
- 只有一处设置：在状态机更新后的统一同步点
- 如果有其他地方设置，需要移除或注释

### 问题2：校准偏移量异常（>5mm）

**可能原因**：
- 焊道位置不稳定
- 检测精度问题

**解决**：
- 调整相机焦距和位置
- 增加校准样本数（5→10）
- 检查 `imageLateralSpanM` 参数（默认0.20m）

### 问题3：纠偏震荡或摆动

**可能原因**：
- PID参数不匹配
- 滤波器响应过快

**调试**：
```cpp
// 在小车配置中调整：
纠偏速度：10.0 mm/s → 8.0 mm/s（降低）
Kp：2.0 → 1.5（降低比例增益）
Kd：5.0 → 4.0（降低微分增益）
```

## ✅ 日志验证关键字

运行时应该看到以下日志：

```
[LASER.DETECTION] 焊道状态: Candidate → Confirmed
[LASER.CALIBRATION] 激光中心校准完成 offset=-1.58mm samples=5
[LASER.DETECTION] 焊道状态: Confirmed → Located
[LaserCorrection] 焊道定位完成，可以启动纠偏
```

## ✅ 最终确认

**修复成功的标志**：
1. ✅ UI显示"本项焊道已定位"时，纠偏系统同步显示"焊道已定位"
2. ✅ 点击"启动自动纠偏"立即生效，不会停车
3. ✅ 焊道中心逐渐向激光中心（0位置）移动
4. ✅ 纠偏过程平滑，无大幅震荡

---

**验证人员**：___________  
**验证日期**：___________  
**验证结果**：[ ] 通过  [ ] 失败（原因：_________）
