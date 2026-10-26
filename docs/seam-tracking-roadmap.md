# 焊道跟踪优化实施路线图

## 项目概览

**目标**：解决焊道定位状态矛盾、纠偏摆动、激光中心对齐三大问题

**预计周期**：2-3周（含开发、测试、验收）

**涉及文件**：
- 新增：`seam_state_machine.h` - 状态机核心
- 修改：`laser_correction_controller.h/.cpp` - 集成状态机和自适应PID
- 修改：`robotcontrolpanel.cpp` - UI适配
- 新增：测试和文档

---

## 实施阶段

### Phase 1: 核心组件开发（Week 1）

#### 1.1 状态机实现 [2天]

**任务**：
- [ ] 创建 `seam_state_machine.h`
- [ ] 实现四级状态枚举和转换逻辑
- [ ] 添加稳定性检测（连续N帧）
- [ ] 实现Qt信号机制

**关键代码位置**：
```cpp
// seam_state_machine.h
class SeamStateMachine {
    void update(bool valid, double centerX, double confidence);
    SeamLocalizationState getState() const;
    bool canStartCorrection() const;
signals:
    void stateChanged(SeamLocalizationState);
    void seamLocated();
};
```

**验收标准**：
- 单元测试覆盖率>90%
- 模拟输入可触发完整状态链路
- 信号正确发射

---

#### 1.2 激光中心校准器 [1天]

**任务**：
- [ ] 实现 `LaserCenterCalibration` 类
- [ ] 多帧采样平均算法
- [ ] 坐标转换接口
- [ ] 重置和重新校准功能

**关键代码位置**：
```cpp
class LaserCenterCalibration {
    void calibrate(double detectedX, double laserX = 0.0);
    void calibrateFromSamples(const std::vector<double>& samples);
    double adjustToLaserFrame(double rawX) const;
    bool isCalibrated() const;
};
```

**验收标准**：
- 5帧采样平均误差<0.5mm
- 转换后坐标在±5mm范围
- 重置功能清除历史状态

---

#### 1.3 自适应PID [1.5天]

**任务**：
- [ ] 实现 `AdaptivePIDParams` 类
- [ ] 分段Kp/Kd映射算法
- [ ] 线性插值过渡
- [ ] 参数配置接口

**关键代码位置**：
```cpp
class AdaptivePIDParams {
    Params getParams(double errorMm) const;
    void setLargeErrorParams(double kp, double kd);
    void setMediumErrorParams(double kp, double kd);
    void setSmallErrorParams(double kp, double kd);
};
```

**验收标准**：
- 误差10mm使用大偏差参数
- 误差2mm使用小偏差参数
- 中间区域平滑过渡（无跳变）

---

#### 1.4 中心滤波器 [0.5天]

**任务**：
- [ ] 实现 `CenterFilter` 类
- [ ] 指数滑动平均算法
- [ ] 滤波系数可配置

**关键代码位置**：
```cpp
class CenterFilter {
    double update(double raw);
    void setAlpha(double alpha);
    void reset();
};
```

**验收标准**：
- 白噪声输入降噪>50%
- 滤波延迟<3帧
- 阶跃响应无过冲

---

### Phase 2: 集成与适配（Week 1-2）

#### 2.1 控制器集成 [2天]

**任务**：
- [ ] 在 `LaserCorrectionController` 中添加组件成员
- [ ] 改造 `processProfileDetection()` 主流程
- [ ] 集成状态机更新
- [ ] 集成激光校准触发
- [ ] 集成自适应PID计算

**关键修改位置**：
```cpp
// laser_correction_controller.h
class LaserCorrectionController {
private:
    SeamStateMachine* seamStateMachine_;
    LaserCenterCalibration centerCalibration_;
    AdaptivePIDParams adaptivePID_;
    CenterFilter centerFilter_;
    
    void processProfileDetection(const ProfileDetection& det);
};

// laser_correction_controller.cpp
void LaserCorrectionController::processProfileDetection(...) {
    // 1. 更新状态机
    seamStateMachine_->update(...);
    
    // 2. 自动校准
    if (state == Confirmed && !calibrated) {
        centerCalibration_.calibrate(...);
    }
    
    // 3. 坐标转换
    double laserX = centerCalibration_.adjustToLaserFrame(raw_x);
    
    // 4. 滤波
    double filtered = centerFilter_.update(laserX);
    
    // 5. 自适应PID纠偏
    auto params = adaptivePID_.getParams(filtered);
    double speed = params.kp * filtered + params.kd * derivative;
    applyCorrection(speed);
}
```

**验收标准**：
- 编译通过无警告
- 现有功能不受影响
- 新组件正确调用

---

#### 2.2 UI适配 [1.5天]

**任务**：
- [ ] 修改 `robotcontrolpanel.cpp` 状态显示
- [ ] 添加状态颜色标识
- [ ] 修改"启动纠偏"按钮使能逻辑
- [ ] 添加校准偏移量显示
- [ ] 添加PID参数配置界面

**UI改动清单**：

```cpp
// robotcontrolpanel.cpp

// 1. 连接信号
void RobotControlPanel::setupConnections() {
    connect(laserController_, &LaserController::seamStateChanged,
            this, &RobotControlPanel::onSeamStateChanged);
    connect(laserController_, &LaserController::calibrationCompleted,
            this, &RobotControlPanel::onCalibrationCompleted);
}

// 2. 状态显示更新
void RobotControlPanel::onSeamStateChanged(SeamLocalizationState state, QString desc) {
    ui->lblSeamStatus->setText(desc);
    
    switch (state) {
    case NotDetected:
        ui->lblSeamStatus->setStyleSheet("color: gray;");
        ui->btnStartCorrection->setEnabled(false);
        break;
    case Candidate:
        ui->lblSeamStatus->setStyleSheet("color: orange;");
        ui->btnStartCorrection->setEnabled(false);
        break;
    case Confirmed:
        ui->lblSeamStatus->setStyleSheet("color: yellow;");
        ui->btnStartCorrection->setEnabled(false);
        break;
    case Located:
        ui->lblSeamStatus->setStyleSheet("color: green; font-weight: bold;");
        ui->btnStartCorrection->setEnabled(true);
        break;
    }
}

// 3. 校准完成提示
void RobotControlPanel::onCalibrationCompleted(double offset) {
    ui->lblCalibrationOffset->setText(QString("%1 mm").arg(offset, 0, 'f', 2));
    QMessageBox::information(this, "校准完成",
        QString("激光中心校准完成\n偏移量: %1 mm").arg(offset, 0, 'f', 2));
}
```

**新增UI控件**（在激光自动纠偏面板）：

```
焊道状态: [本项焊道候选]  (颜色：橙色)
校准偏移: [-1578.6 mm]

┌─ 自适应PID配置 ──────────┐
│ 大偏差 Kp: [2.5] Kd: [3.0]│
│ 中偏差 Kp: [1.5] Kd: [2.0]│
│ 小偏差 Kp: [0.5] Kd: [1.0]│
│                           │
│ [ 应用配置 ] [ 重置默认 ] │
└───────────────────────────┘

[ 启动自动纠偏 ] [ 停止纠偏 ] [ 重新校准 ]
```

**验收标准**：
- 状态文本和颜色正确映射
- 按钮使能逻辑符合状态机
- 参数修改可实时生效
- UI布局美观不重叠

---

#### 2.3 日志增强 [0.5天]

**任务**：
- [ ] 添加状态转换日志
- [ ] 添加校准过程日志
- [ ] 添加自适应PID参数日志
- [ ] 优化日志格式（便于解析）

**日志示例**：
```
[LASER.STATE] 焊道状态变更: 未检测到焊道 → 本项焊道候选
[LASER.CALIBRATION] 开始采样 (1/5): center_x = -1578.3 mm
[LASER.CALIBRATION] 开始采样 (2/5): center_x = -1579.1 mm
...
[LASER.CALIBRATION] 校准完成: offset = -1578.6 mm
[LASER.CONTROL] error=15.2mm kp=2.5 kd=3.0 speed=10.0mm/s
[LASER.CONTROL] error=6.8mm kp=1.8 kd=2.5 speed=5.2mm/s
[LASER.CONTROL] error=2.1mm kp=1.0 kd=1.5 speed=1.5mm/s
```

**验收标准**：
- 关键事件均有日志
- 日志可用正则表达式解析
- 日志量适中（100ms级别间隔）

---

### Phase 3: 测试与优化（Week 2）

#### 3.1 单元测试 [1.5天]

**任务**：
- [ ] 编写状态机单元测试
- [ ] 编写校准器单元测试
- [ ] 编写PID参数单元测试
- [ ] 编写滤波器单元测试

**测试框架**：Qt Test 或 Google Test

**覆盖率目标**：
- 状态机：>95%
- 校准器：>90%
- PID/滤波：>85%

**示例测试**：
```cpp
// test_seam_state_machine.cpp
TEST(SeamStateMachine, StateTransition) {
    SeamStateMachine sm;
    
    // 初始状态
    EXPECT_EQ(sm.getState(), SeamLocalizationState::NotDetected);
    
    // 检测到候选
    sm.update(true, -1578.0, 0.12);
    EXPECT_EQ(sm.getState(), SeamLocalizationState::Candidate);
    
    // 连续3帧稳定 → 确认
    for (int i = 0; i < 3; i++) {
        sm.update(true, -1578.0 + i*0.001, 0.12);
    }
    EXPECT_EQ(sm.getState(), SeamLocalizationState::Confirmed);
    
    // 校准完成 → 定位
    sm.notifyCalibrationComplete();
    sm.update(true, -1578.0, 0.12);
    EXPECT_EQ(sm.getState(), SeamLocalizationState::Located);
}
```

---

#### 3.2 功能测试 [2天]

按照 `seam-tracking-test-plan.md` 执行：
- [ ] TC-001: 状态机基本转换
- [ ] TC-002: 激光中心自动校准
- [ ] TC-003: 自适应PID大偏差
- [ ] TC-004: 自适应PID小偏差
- [ ] TC-005: 滤波效果验证
- [ ] TC-006: 焊道丢失恢复
- [ ] TC-007: 参数配置持久化
- [ ] TC-008: 多焊道类型适应性

**测试输出**：
- 测试报告Excel
- 关键Case的日志文件
- 纠偏曲线图（用Python脚本生成）

---

#### 3.3 性能优化 [1天]

**优化点**：
- [ ] 滤波器延迟优化（降低alpha）
- [ ] PID参数微调（根据测试反馈）
- [ ] 状态转换阈值调整
- [ ] 日志输出频率优化（减少I/O）

**基准对比**：

| 指标 | 优化前 | 优化后 | 改善 |
|------|-------|-------|------|
| 小偏差RMS | 0.4mm | 0.15mm | 62% ↓ |
| 定位时间 | 8s | 6s | 25% ↓ |
| CPU占用 | 18% | 12% | 33% ↓ |

---

### Phase 4: 验收与部署（Week 3）

#### 4.1 集成测试 [1天]

**任务**：
- [ ] 完整流程测试（从开机到纠偏）
- [ ] 异常场景测试（断电、急停、相机丢失）
- [ ] 多次重复测试（验证稳定性）

**场景覆盖**：
- 正常启动 → 检测 → 校准 → 纠偏 → 停止
- 纠偏中断 → 恢复 → 重新定位
- 切换焊道类型
- 参数实时修改

---

#### 4.2 现场试运行 [2天]

**地点**：生产线或试验场地

**任务**：
- [ ] 真实焊道上验证功能
- [ ] 收集操作人员反馈
- [ ] 记录任何异常现象
- [ ] 性能数据采集

**反馈收集**：
```
操作员: 张三
日期: 2026-10-XX

1. 状态显示是否清晰？ ☑ 是  ☐ 否
   备注：颜色区分明显，易懂

2. 纠偏效果是否满意？ ☑ 是  ☐ 否
   备注：摆动明显减少

3. 操作是否便捷？ ☑ 是  ☐ 否
   备注：自动校准省去手动设置

4. 遇到的问题：
   - 窄焊道偶尔需要等待较久（可接受）

5. 改进建议：
   - 增加音频提示定位完成
```

---

#### 4.3 文档完善 [1天]

**任务**：
- [ ] 用户操作手册
- [ ] 开发者维护文档
- [ ] 故障诊断指南
- [ ] 视频教程（录屏）

**用户手册目录**：
```
1. 系统概述
2. 快速开始
   2.1 开机准备
   2.2 焊道检测
   2.3 启动纠偏
3. 参数配置
   3.1 自适应PID设置
   3.2 滤波系数调整
   3.3 重新校准
4. 故障排查
   4.1 无法检测焊道
   4.2 纠偏不稳定
   4.3 校准偏差过大
5. 常见问题FAQ
```

---

#### 4.4 最终验收 [0.5天]

**验收会议议程**：
1. 功能演示（20分钟）
2. 测试报告汇报（15分钟）
3. 性能指标对比（10分钟）
4. 问题与解决方案说明（15分钟）
5. 签署验收单

**验收标准清单**：
- [ ] 所有TC通过率≥95%
- [ ] 核心性能指标达标
- [ ] 用户手册完整
- [ ] 代码通过审查
- [ ] 无阻塞性bug

---

## 里程碑与交付物

| 时间 | 里程碑 | 交付物 |
|------|--------|--------|
| Week 1 Day 5 | 核心组件完成 | seam_state_machine.h, 单元测试报告 |
| Week 2 Day 3 | 集成完成 | 集成后的代码, UI截图 |
| Week 2 Day 5 | 测试完成 | 测试报告, 性能曲线图 |
| Week 3 Day 3 | 现场试运行 | 试运行日志, 操作员反馈 |
| Week 3 Day 5 | 最终验收 | 用户手册, 验收签字单 |

---

## 风险管理

### 风险识别

| ID | 风险描述 | 概率 | 影响 | 应对措施 |
|----|---------|------|------|---------|
| R-01 | 状态机与现有代码冲突 | 中 | 高 | 预留1天调试时间 |
| R-02 | 自适应PID调参困难 | 高 | 中 | 准备多组参数预案 |
| R-03 | 滤波延迟影响实时性 | 低 | 中 | 可动态调整alpha |
| R-04 | 窄焊道定位失败 | 中 | 中 | 增加确认帧数降低误判 |
| R-05 | 现场环境干扰 | 低 | 高 | 提前现场踩点测试 |

### 应急预案

**如果Week 2测试不达标**：
1. 降级方案：仅集成状态机（修复UI矛盾）
2. 推迟方案：延长1周重新调参
3. 分阶段方案：先上线状态机，PID优化下版本

---

## 资源需求

### 人力
- 主程序员 1人（全程）
- UI开发 0.5人（Week 1-2）
- 测试工程师 1人（Week 2-3）

### 设备
- 开发环境：Windows PC + Qt Creator
- 测试环境：爬行机器人 + 激光相机
- 测试焊道：标准V型、窄V型、U型槽各1组

### 工具
- 版本控制：Git
- 测试框架：Qt Test
- 数据分析：Python + Matplotlib
- 文档：Markdown + Typora

---

## 成功标准

### 技术指标
✅ 定位成功率 >95%  
✅ 小偏差RMS <0.2mm  
✅ 激光校准精度 ±2mm  
✅ 长时间稳定运行 >30min  
✅ CPU占用 <15%

### 用户体验
✅ 状态显示清晰，无歧义  
✅ 纠偏启动逻辑明确  
✅ 操作步骤简化（自动校准）  
✅ 反馈及时（3秒内响应）

### 项目管理
✅ 按时交付（±3天容忍度）  
✅ 无阻塞性bug遗留  
✅ 文档完整可用  
✅ 代码可维护性良好

---

## 后续规划

### 短期优化（1-2个月）
- [ ] 根据现场反馈微调参数
- [ ] 增加多焊道模板切换功能
- [ ] 优化窄焊道检测算法

### 中期扩展（3-6个月）
- [ ] 支持曲线焊道跟踪
- [ ] 增加预测性纠偏（基于历史轨迹）
- [ ] 集成深度学习焊道识别

### 长期愿景（1年+）
- [ ] 全自动焊道识别与学习
- [ ] 多机器人协同纠偏
- [ ] 云端数据分析与优化

---

**路线图版本**: v1.0  
**创建时间**: 2026-10-23  
**项目负责人**: [姓名]  
**审核人**: [姓名]
