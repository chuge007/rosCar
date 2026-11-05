# 摄像头服务故障排查

## 问题: 重启后第一次连接失败

### 症状
- 重启电脑后第一次启动程序,摄像头无法连接
- 关闭程序后再次打开,摄像头可以正常工作
- 日志中出现 "Failed to create shared memory" 错误

### 原因
摄像头服务使用共享内存进行进程间通信。如果上次程序异常退出(崩溃、强制关闭等),共享内存段可能没有被正确释放。

### 解决方法

#### 方法 1: 自动修复(推荐)
程序已经内置了自动清理逻辑,多次尝试启动程序通常能自动解决。

#### 方法 2: 重启程序
1. 完全关闭 PA1664Workbench
2. 等待 5 秒
3. 重新启动程序

#### 方法 3: 手动清理(Windows)
如果问题依然存在,可以手动清理共享内存:

```cmd
# 1. 关闭所有 PA1664Workbench 和 PA1664CameraServer 进程
taskkill /F /IM PA1664Workbench.exe
taskkill /F /IM PA1664CameraServer.exe

# 2. 等待 3 秒让系统清理资源
timeout /t 3

# 3. 重新启动程序
start PA1664Workbench.exe
```

#### 方法 4: 重启系统
最后的手段,重启 Windows 会清理所有共享内存段。

### 预防措施

1. **正常退出程序**
   - 使用 "File → Exit" 菜单
   - 不要通过任务管理器强制结束

2. **等待退出完成**
   - 关闭主程序后,等待 2-3 秒
   - 确保 PA1664CameraServer.exe 也已退出

3. **检查进程状态**
   ```cmd
   tasklist | findstr PA1664
   ```
   应该没有任何输出

### 日志分析

#### 正常启动日志
```
Camera server started with IPC key: PA1664Camera
Created shared memory PA1664Camera_slot0 size 4194304
Created shared memory PA1664Camera_slot1 size 4194304
SERVER_READY
```

#### 共享内存冲突日志
```
Shared memory PA1664Camera_slot0 already exists, forcing cleanup
Found existing shared memory PA1664Camera_slot0 - removing
Created shared memory PA1664Camera_slot0 size 4194304
```

#### 严重错误日志
```
Failed to create shared memory PA1664Camera_slot0 after aggressive cleanup: Permission denied
Please manually delete the shared memory segment or reboot
```
这种情况需要重启系统。

## 问题: 摄像头延迟过大

### 症状
- 摄像头画面延迟超过 500ms
- 视频卡顿或掉帧严重
- CPU 占用率过高

### 解决方法

#### 1. 降低分辨率
编辑配置文件,将摄像头分辨率降低到 320x240:

```ini
[Camera]
usbCameraDeviceIndex0=0
usbCameraFps0=10
usbCameraResolution0=320x240  # 添加这一行
```

#### 2. 降低帧率
将默认 FPS 从 15 降到 10 或更低:

```ini
usbCameraFps0=10
usbCameraFps1=10
```

#### 3. 禁用镜像翻转
如果不需要翻转画面,禁用它可以减少处理时间:

```ini
usbCameraFlipHorizontal0=false
usbCameraFlipVertical0=false
```

#### 4. 网络摄像头优化
- 使用有线连接代替 Wi-Fi
- 降低网络摄像头的码率
- 检查网络带宽是否充足

#### 5. 检查 CPU 负载
```cmd
# 查看摄像头服务的 CPU 占用
tasklist /V | findstr PA1664CameraServer
```

如果 CPU 占用超过 50%,考虑:
- 关闭其他不必要的程序
- 升级硬件配置
- 只使用一个摄像头

## 问题: 摄像头服务无法启动

### 症状
- 主程序日志显示 "Failed to start camera server"
- 找不到 PA1664CameraServer.exe

### 解决方法

#### 1. 检查文件是否存在
```cmd
dir "C:\Path\To\Your\App\bin\PA1664CameraServer.exe"
```

#### 2. 检查 Qt 依赖
确保以下 DLL 在同一目录:
- Qt6Core.dll
- Qt6Network.dll
- Qt6Multimedia.dll

#### 3. 手动启动测试
```cmd
cd C:\Path\To\Your\App\bin
PA1664CameraServer.exe --ipc-key TestKey
```

如果看到 "SERVER_READY",说明服务可以正常运行。

#### 4. 检查权限
- 确保程序有读写权限
- 尝试以管理员身份运行

## 问题: 画面显示异常

### 症状
- 画面倒置或镜像
- 颜色不正确
- 画面撕裂

### 解决方法

#### 1. 调整翻转设置
```ini
usbCameraFlipHorizontal0=true   # 水平翻转
usbCameraFlipVertical0=true     # 竖直翻转
```

#### 2. 检查摄像头驱动
更新到最新的摄像头驱动程序。

#### 3. 测试摄像头
使用 Windows 自带的"相机"应用测试摄像头是否正常。

## 联系支持

如果以上方法都无法解决问题,请收集以下信息并联系技术支持:

1. **系统信息**
   - Windows 版本
   - CPU 和内存配置
   - 摄像头型号

2. **日志文件**
   - 主程序日志
   - 摄像头服务日志

3. **复现步骤**
   - 详细描述问题出现的条件
   - 是否可以稳定复现

4. **错误截图**
   - 错误对话框
   - 日志输出
