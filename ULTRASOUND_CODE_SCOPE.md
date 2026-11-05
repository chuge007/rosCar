# 超声代码范围文档

本文档记录了项目中所有与超声相关的代码，用于明确超声代码的边界和更新范围。

## 一、核心超声类（完全属于超声模块）

### 1. 扫描显示组件 (src/scanwidgets.h + src/scanwidgets.cpp)
**职责**：超声波扫描数据的可视化显示

#### 类列表：
- `AScanWidget` - A扫描显示组件（单点波形显示）
- `BScanWidget` - B扫描显示组件（剖面图显示）
- `CScanWidget` - C扫描显示组件（平面图显示）
- `EScanWidget` - E扫描显示组件（扇形扫描显示）
- `CScanRenderer` - C扫描渲染器（内部类）
- `CScanRenderState` - C扫描渲染状态（结构体）

**标识特征**：
- 文件包含大量 `AScan`, `BScan`, `CScan`, `EScan` 相关代码
- 处理超声波形数据显示
- 包含门控（Gate）、调色板（Palette）等超声特有概念

---

### 2. 设备控制器 (src/devicecontroller.h + src/devicecontroller.cpp)
**职责**：超声设备的控制和数据采集

#### 主要类：
- `DeviceController` - 超声设备控制器

**超声相关功能**：
- C扫描数据采集和处理
- 超声设备参数控制
- 扫描数据流管理

**标识特征**：
- 包含大量 `cScan`, `CScan` 相关代码
- 超声设备的底层控制逻辑

---

### 3. 数据包解码器 (src/packetdecoder.h + src/packetdecoder.cpp)
**职责**：超声数据包的解码

**超声相关功能**：
- E扫描数据解码
- 超声数据流解析

**标识特征**：
- 包含 `EScan`, `eScan` 数据处理

---

### 4. 扫描波形缓存 (src/scanwaveformcache.h + src/scanwaveformcache.cpp)
**职责**：超声波形数据的缓存管理

#### 主要类：
- `ScanWaveformCache` - 扫描波形缓存

**标识特征**：
- E扫描数据缓存

---

## 二、集成超声功能的混合类（部分超声功能）

### 1. 主窗口 (src/mainwindow.h + src/mainwindow.cpp)
**职责**：应用程序主窗口，集成超声显示和其他功能

#### 超声相关成员：
```cpp
QString m_ultrasoundScreenName;          // 超声屏幕名称
AScanWidget *m_aScan = nullptr;          // A扫描组件
BScanWidget *m_bScan = nullptr;          // B扫描组件
CScanWidget *m_cScan = nullptr;          // C扫描组件
EScanWidget *m_eScan = nullptr;          // E扫描组件
```

**更新原则**：
- 仅更新包含 "超声", "ultrasound", "AScan", "BScan", "CScan", "EScan" 相关代码段
- 不修改激光、相机、机器人控制等其他功能代码

**标识特征**：
- 包含 `m_aScan`, `m_bScan`, `m_cScan`, `m_eScan` 成员变量
- 超声窗口布局和显示管理

---

### 2. 参数面板 (src/parameterpanel.h + src/parameterpanel.cpp)
**职责**：参数设置面板，包含超声参数

**超声相关功能**：
- 超声设备参数配置
- 超声参数的UI显示和调整

**更新原则**：
- 仅更新超声参数相关的UI和逻辑
- 不修改其他参数类型

**标识特征**：
- 包含 "超声" 关键字

---

### 3. 参数预设存储 (src/parameterpresetstore.h + src/parameterpresetstore.cpp)
**职责**：参数预设的存储和加载

**超声相关功能**：
- E扫描参数预设

**标识特征**：
- 包含 `eScan` 相关代码

---

### 4. 产品状态显示 (src/productchrome.h + src/productchrome.cpp)
**职责**：产品扫描状态显示

**超声相关功能**：
- 超声扫描状态显示

**标识特征**：
- 包含 "超声" 关键字

---

### 5. 远程控制服务器 (src/remotecontrolserver.h + src/remotecontrolserver.cpp)
**职责**：远程控制接口

**超声相关功能**：
- 超声控制命令接口

**标识特征**：
- 包含 "ultrasound" 关键字

---

### 6. 激光校正控制器 (src/laser_correction_controller.h + src/laser_correction_controller.cpp)
**职责**：激光校正控制，与超声数据采集协调

**超声相关功能**：
- 发送 `trackingPhaseStarted()` 信号，通知超声控制器开始数据采集
- 与超声数据采集的时序协调

**更新原则**：
- 仅在需要修改超声与激光协调逻辑时更新
- 不修改激光校正的核心算法

**标识特征**：
- 包含 "ultrasound" 注释
- 包含 `eScan` 相关代码

---

### 7. 脉冲负载保护 (src/pulseloadguard.h + src/pulseloadguard.cpp)
**职责**：设备脉冲负载保护

**超声相关功能**：
- 超声模式下的负载保护

**标识特征**：
- 包含 "Ultrasonic" 关键字

---

## 三、配置和启动相关

### 1. 主程序入口 (src/main.cpp)
**超声相关功能**：
- 超声设备配置加载
- 超声配置目录管理 (`configs/ultrasound_device_0`)
- 超声配置文件验证和修复

**更新原则**：
- 仅在需要修改超声配置加载逻辑时更新

**标识特征**：
- 包含 `ultrasound_config`, `ultrasound_device_0` 等配置路径

---

### 2. USB引导 (src/usbbootstrap.h + src/usbbootstrap.cpp)
**超声相关功能**：
- E扫描相关的USB设备初始化

**标识特征**：
- 包含 `escan` 关键字

---

### 3. 帧解码工作线程 (src/framedecoderworker.h + src/framedecoderworker.cpp)
**超声相关功能**：
- C扫描和E扫描数据解码

**标识特征**：
- 包含 `cScan`, `CScan`, `EScan` 关键字

---

## 四、次要相关文件

### 1. 焊缝轮廓检测器 (src/profile_weld_detector.h)
**超声相关功能**：
- 可能使用E扫描数据进行焊缝检测

**标识特征**：
- 包含 `escan` 关键字（小写）

---

## 五、第三方依赖（只读，不修改）

### 1. vendor/phaselink/include/basic_def.h
**内容**：
- 定义了 `UltrasonicMicroscopy` 扫描模式枚举

**原则**：
- **不修改第三方库代码**
- 仅作为参考

---

## 六、超声代码识别规则

### 关键字列表：
```
核心关键字：
- 中文：超声
- 英文：ultrasound, ultrasonic, Ultrasonic
- 扫描类型：AScan, aScan, BScan, bScan, CScan, cScan, EScan, eScan

相关概念：
- Gate（门控）
- Waveform（波形）
- Amplitude（幅度）
- Beam（波束）
- Scan（扫描）
```

### 识别方法：
1. **文件级别**：文件名或文件内容包含大量超声关键字
2. **类级别**：类名包含 Scan, Ultrasound 等关键字
3. **函数级别**：函数名或注释包含超声关键字
4. **变量级别**：变量名包含 scan, ultrasound 等关键字

---

## 七、更新策略

### 1. 完全更新（整个文件）
适用于核心超声类：
- `src/scanwidgets.h` + `src/scanwidgets.cpp`
- `src/scanwaveformcache.h` + `src/scanwaveformcache.cpp`
- `src/packetdecoder.h` + `src/packetdecoder.cpp` (超声部分)

### 2. 部分更新（代码段）
适用于混合功能文件：
- `src/mainwindow.h` + `src/mainwindow.cpp` - 仅更新超声相关代码段
- `src/devicecontroller.h` + `src/devicecontroller.cpp` - 仅更新超声相关方法
- `src/parameterpanel.h` + `src/parameterpanel.cpp` - 仅更新超声参数部分

### 3. 谨慎更新（协调逻辑）
适用于系统集成文件：
- `src/main.cpp` - 仅在需要修改配置加载时更新
- `src/laser_correction_controller.h` + `src/laser_correction_controller.cpp` - 仅在需要修改协调逻辑时更新

### 4. 不更新
- `vendor/` 目录下的所有文件
- 与超声无关的文件（激光、相机、机器人控制等）

### 5. 性能优化保护原则 ⚡
**重要**：超声计算是性能敏感模块，更新时必须保留已有的优化：

#### 需要保护的优化类型：
1. **内存优化**
   - 预分配的缓冲区（避免频繁分配）
   - 对象池和复用机制
   - 智能指针的使用策略
   - 数据结构的紧凑布局

2. **计算优化**
   - SIMD指令优化
   - 查找表（LUT）的使用
   - 缓存友好的数据访问模式
   - 避免重复计算的缓存结果

3. **线程和并发优化**
   - 多线程数据处理
   - 无锁数据结构
   - 线程池的使用
   - 减少锁竞争的设计

4. **渲染优化**
   - OpenGL/QPainter的高效使用
   - 双缓冲/三缓冲机制
   - 脏区域更新（避免全屏刷新）
   - 纹理和着色器优化

5. **数据流优化**
   - 零拷贝数据传递
   - 批量处理而非逐个处理
   - 异步IO操作
   - 流水线处理架构

#### 识别性能优化代码的特征：
```cpp
// 内存预分配
buffer.reserve(expectedSize);

// 对象复用
if (recyclePool.isEmpty()) { /* 创建新对象 */ }
else { /* 复用已有对象 */ }

// 批量处理
for (int i = 0; i < frames.size(); i += batchSize) { /* 批处理 */ }

// 缓存计算结果
if (!cached) { result = heavyComputation(); cached = true; }

// 条件性更新
if (dirty) { render(); dirty = false; }

// 注释标记
// PERFORMANCE: ...
// OPTIMIZATION: ...
// TODO: optimize ...
// Fast path for ...
```

#### 更新代码时的检查清单：
- [ ] 是否保留了预分配的缓冲区大小？
- [ ] 是否保留了对象复用机制？
- [ ] 是否保留了批量处理逻辑？
- [ ] 是否保留了缓存和脏标记？
- [ ] 是否保留了多线程处理结构？
- [ ] 是否保留了条件性更新逻辑？
- [ ] 新增代码是否遵循相同的优化模式？

#### 特别注意的文件（性能关键）：
- `scanwidgets.cpp` - 高频渲染，注意保留脏区域更新和缓存
- `devicecontroller.cpp` - 数据采集主路径，注意保留批处理和缓冲
- `packetdecoder.cpp` - 数据解码热路径，注意保留预分配和零拷贝
- `framedecoderworker.cpp` - 多线程处理，注意保留线程池和任务队列
- `CScanRenderer` - 渲染器，注意保留OpenGL优化和纹理管理

---

## 八、使用方法

### 当你要更新超声代码时：

1. **明确需求**：确定要修改的超声功能（A扫描、B扫描、C扫描、E扫描、设备控制等）

2. **查找文件**：根据本文档找到对应的文件和类

3. **检查现有优化**：
   - 阅读目标代码，识别性能优化特征
   - 记录需要保留的优化机制
   - 理解优化的目的和效果

4. **确定范围**：
   - 如果是核心超声文件（如 scanwidgets.cpp），可以整体修改
   - 如果是混合文件（如 mainwindow.cpp），只修改包含超声关键字的代码段

5. **实现新功能**：
   - 在保留优化的基础上添加新功能
   - 新代码遵循相同的优化模式
   - 避免引入性能回退

6. **验证边界**：
   - 搜索文件中的超声关键字，确认修改范围
   - 不要修改其他功能的代码
   - 确认性能优化仍然有效

7. **测试影响**：
   - 修改后检查是否影响非超声功能
   - 确保激光、相机、机器人等功能不受影响
   - 如可能，测试性能是否保持或改进

### 示例指令：
```
用户："只更新超声方面的代码，添加XXX功能，保留性能优化"

AI应该：
1. 读取 ULTRASOUND_CODE_SCOPE.md
2. 确定需要修改的文件
3. 检查文件中的性能优化代码
4. 在保留优化的基础上添加新功能
5. 只修改超声相关的代码
6. 不触碰激光、相机等其他模块的代码
```

### 性能优化实例：
```cpp
// ❌ 错误：破坏了预分配优化
// 原代码：buffer.reserve(1024); buffer.append(data);
// 修改后：QVector<int> buffer; buffer.append(data);  // 丢失了预分配

// ✅ 正确：保留预分配优化
// 原代码：buffer.reserve(1024); buffer.append(data);
// 修改后：buffer.reserve(2048); buffer.append(data); buffer.append(newData);

// ❌ 错误：破坏了批处理优化
// 原代码：for (auto frame : frames) processFrame(frame);
// 修改后：for (auto frame : frames) { processFrame(frame); saveFrame(frame); }  // 串行处理

// ✅ 正确：保留批处理优化
// 原代码：processBatch(frames);
// 修改后：processBatch(frames); saveBatch(frames);  // 批量处理

// ❌ 错误：破坏了脏标记优化
// 原代码：if (dirty) { render(); dirty = false; }
// 修改后：render();  // 每次都渲染，丢失了条件更新

// ✅ 正确：保留并扩展脏标记
// 原代码：if (dirty) { render(); dirty = false; }
// 修改后：if (dirty || newDataArrived) { render(); dirty = false; newDataArrived = false; }
```

---

## 九、快速参考表

| 文件 | 超声占比 | 更新策略 | 主要内容 |
|------|----------|----------|----------|
| `scanwidgets.h/cpp` | 100% | 完全更新 | A/B/C/E扫描显示组件 |
| `scanwaveformcache.h/cpp` | 100% | 完全更新 | 扫描波形缓存 |
| `devicecontroller.h/cpp` | 70% | 部分更新 | 超声设备控制 |
| `mainwindow.h/cpp` | 30% | 部分更新 | 超声组件集成 |
| `parameterpanel.h/cpp` | 20% | 部分更新 | 超声参数UI |
| `packetdecoder.h/cpp` | 50% | 部分更新 | 超声数据解码 |
| `framedecoderworker.h/cpp` | 40% | 部分更新 | 超声数据解码线程 |
| `main.cpp` | 5% | 谨慎更新 | 超声配置加载 |
| `laser_correction_controller.h/cpp` | 5% | 谨慎更新 | 与超声协调 |
| `remotecontrolserver.h/cpp` | 10% | 部分更新 | 超声远程控制 |
| `vendor/*` | - | 不更新 | 第三方库 |

---

## 十、版本信息

- **创建日期**：2026-11-04
- **项目**：PA1664WorkbenchTmp2
- **文档版本**：1.0
- **最后更新**：2026-11-04

---

## 十一、注意事项

⚠️ **重要提醒**：
1. 本项目是 Qt 项目，修改代码后**不要自动运行编译**
2. 修改超声代码时，注意检查是否影响其他模块的集成点
3. 第三方库 (vendor/) 代码只读，不要修改
4. 混合文件中，超声代码通常通过成员变量名（如 `m_aScan`）或关键字容易识别
5. 如有疑问，优先搜索文件中的超声关键字确认范围

---

**文档结束**
