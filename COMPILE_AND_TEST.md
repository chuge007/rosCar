# 编译与测试清单

## 一、修复内容总结

### 1. 编译错误修复（3类）
| 文件 | 错误 | 修复方式 | 状态 |
|------|------|---------|-----|
| `robot_usb_camera_controller.cpp` | `kUsbCameraChannelCount`未声明 | 在头文件加`#include "camera_server_client.h"` | ✅ |
| `laser_correction_controller.cpp` | `adjustedGapLateralM`未声明 | 删除2013-2037行重复逻辑，1545行改用`rawGapLateralM` | ✅ |
| `robot_usb_camera_controller.cpp` | `scanDevices()`不存在 | 删除第80行`client_->scanDevices()`调用 | ⏳待确认行号 |

### 2. 状态机简化（根因修复）
| 修改项 | 说明 |
|-------|------|
| **enum SeamLocalizationState** | 删除`Confirmed`状态，简化为3状态：`NotDetected → Candidate → Located` |
| **update()状态转换** | `Candidate`稳定后直接进`Located`，删除校准门槛 |
| **信号删除** | 删除`seamConfirmed()`信号 |
| **成员变量删除** | 删除`calibrationCompleted_`标志 |
| **描述文本** | `NotDetected`："焊道等待定位"，`Located`："焊道已定位" |

---

## 二、在QtCreator中编译

### Step 1: 打开项目
```
文件 → 打开文件或项目 → PA1664Workbench.pro
```

### Step 2: 清理构建目录
```
构建 → 清除全部
```

### Step 3: 重新构建
```
构建 → 重新构建项目
```

### Step 4: 检查编译输出
查看"编译输出"面板，应该看到：
```
✅ 0 错误
⚠️  可能有警告（忽略未使用变量警告）
```

如果仍有错误：
1. **kUsbCameraChannelCount** → 检查`robot_usb_camera_controller.cpp`是否包含了`camera_server_client.h`
2. **adjustedGapLateralM** → 检查`laser_correction_controller.cpp:1545`是否改成了`rawGapLateralM`
3. **scanDevices()** → 检查`robot_usb_camera_controller.cpp`的`onDisconnect()`函数内是否删掉了`client_->scanDevices()`

---

## 三、运行时验证

### A. 状态转换验证

#### 测试1：启动检测，观察状态
```
预期：焊道检测有效 → Candidate → 稳定3帧 → Located（1-2秒内完成）
日志关键词：
  [SeamStateMachine] State: NotDetected → Candidate
  [SeamStateMachine] State: Candidate → Located
  [SeamStateMachine] Seam located
```

#### 测试2：UI显示一致性
```
状态          | UI显示文本         | 纠偏是否启动
--------------|--------------------|-------------
NotDetected   | "焊道等待定位"       | ❌ 否
Candidate     | "本项焊道候选"       | ❌ 否
Located       | "焊道已定位"         | ✅ 是
```

#### 测试3：信号触发
```cpp
connect(&seamStateMachine_, &SeamStateMachine::seamLocated,
        this, [](){ qDebug() << "✅ seamLocated信号已触发"; });
```

预期：状态变Located时立即打印。

---

### B. 纠偏平滑性验证

#### 测试4：Kp自适应（已在前面文档实现）
```
初始偏差 50mm → Kp=2.5 → 快速收敛
偏差 < 20mm → Kp=0.5 → 平滑跟踪
```

观察：
- 纠偏启动时（大偏差）速度快，无振荡
- 稳定后（小偏差）跟随平滑，无抖动

#### 测试5：激光中心校准（已在前面文档实现）
```
焊道原始中心：-1579mm（rawGapLateralM）
激光坐标系： 0mm（adjustedGapLateralM）
```

验证方法：
1. 日志查看`rawGapLateralM`和`adjustedGapLateralM`的值
2. 纠偏目标应该是adjustedGapLateralM（校准后的）
3. 最终稳态时，焊道中心应在激光十字线中心

---

## 四、失败场景测试

### 测试6：焊道丢失恢复
```
操作：手动遮挡相机 → 检测丢失 → 松开遮挡
预期：
  Located → Candidate → Located（快速恢复）
日志：
  [SeamStateMachine] Seam lost
  [SeamStateMachine] State: Located → Candidate
  [SeamStateMachine] State: Candidate → Located
```

### 测试7：置信度低触发丢失
```
操作：移动工件使焊道部分离开视野
预期：
  confidence < 0.05 → Located → Candidate
  置信度恢复 > 0.10 → Candidate → Located
```

---

## 五、日志验证要点

### 正常启动纠偏日志示例
```
[Frame 100] Detection: valid=true, center=-0.0015m, confidence=0.35
[SeamStateMachine] State: NotDetected → Candidate
[Frame 101] Detection: valid=true, center=-0.0014m, confidence=0.38
[Frame 102] Detection: valid=true, center=-0.0016m, confidence=0.42
[SeamStateMachine] State: Candidate → Located
[SeamStateMachine] Seam located
[LaserCorrection] Seam state: 焊道已定位
[LaserCorrection] Gap valid: true, lateral=-0.0015m
[LaserCorrection] Adaptive Kp=2.5 (large error mode)
[LaserCorrection] Correction started, speed=10.0mm/s
```

### 异常情况日志（应该消失）
```
❌ 不应再出现：
  "等待输入: 焊道等待定位"（Located状态下）
  "calibrationCompleted_ = false"（已删除该标志）
  "State: Confirmed → ..."（已删除Confirmed状态）
```

---

## 六、性能指标

| 指标 | 目标值 | 测量方法 |
|------|--------|---------|
| 检测到焊道→Located | < 2秒（3帧@5ms） | 日志时间戳差 |
| 纠偏启动延迟 | 0ms（Located立即启动） | `gapValid`标志变化时间 |
| 稳态纠偏频率 | 每帧（5ms/帧） | 日志计数 |
| 纠偏平滑度（Kd=5） | 标准差 < 1mm | 统计10秒内位置变化 |

---

## 七、回滚方案

如果新代码导致意外问题：

### 快速回滚
```bash
git diff HEAD src/seam_state_machine.h > patch_state_machine.diff
git diff HEAD src/laser_correction_controller.cpp > patch_controller.diff

# 出问题时：
git checkout HEAD -- src/seam_state_machine.h
git checkout HEAD -- src/laser_correction_controller.cpp
```

### 分阶段验证
1. **先验证编译**：只修编译错误，不改状态机 → 编译通过
2. **再验证状态机**：简化3状态 → 检测定位快速
3. **最后验证纠偏**：Kp自适应+中心校准 → 平滑无抖动

---

## 八、验证完成标志

- [ ] QtCreator编译通过，0错误
- [ ] 启动程序，日志无崩溃
- [ ] 焊道检测到后 < 2秒进Located状态
- [ ] UI显示"焊道已定位"，纠偏立即启动
- [ ] 纠偏平滑，无大幅摆动（Kp自适应生效）
- [ ] 焊道中心最终对齐激光中心（中心校准生效）
- [ ] 遮挡测试：丢失后快速恢复Located
- [ ] 日志无"焊道等待定位"（Located状态下）

**全部通过 → 修复成功！**
