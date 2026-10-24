# PA1664 双端开机启动部署

本目录把小车电脑和操作电脑的启动文件、快捷方式和登录自启动逻辑完全分开。两端各自复制一个目录即可，不需要把另一端的程序放在同一台电脑上。

```text
startup_deploy\
  robot_host\                 放到小车电脑
    app\                      完整 Host 运行文件
    config\host.psd1          Host 监听地址和四个端口
    runtime\launcher.log      启动器日志
    StartRobotHost.cmd        手动启动
    InstallAutoStart.cmd      安装桌面快捷方式 + 登录自启动
    UninstallAutoStart.cmd    卸载快捷方式和自启动

  operator_workbench\         放到操作电脑
    app\                      完整 Workbench 运行文件
    config\workbench.psd1     小车电脑 IP + 控制端口
    runtime\launcher.log      启动器日志
    StartOperatorWorkbench.cmd
    InstallAutoStart.cmd
    UninstallAutoStart.cmd
```

## 一、准备程序文件

`app` 目录必须放**完整运行环境**，不能只放一个 EXE：

1. 编译后把 `bin` 中对应程序的完整运行文件复制到目标目录的 `app`。
2. 必须包括 Qt DLL、OpenCV DLL、厂商 DLL、`plugins`、`config`、`configs` 等依赖目录。
3. `robot_host\app` 中必须有 `PA1664RobotHost.exe`。
4. `operator_workbench\app` 中必须有 `PA1664Workbench.exe`。
5. 删除或替换 `PLACE_RUNTIME_FILES_HERE.txt` 不影响启动，但正式部署时可以移除。

不要把运行目录移到临时目录。桌面和自启动快捷方式保存的是安装时的绝对路径；移动整个目录后必须重新运行一次 `InstallAutoStart.cmd`。

## 二、小车电脑部署

1. 将本目录中的 `robot_host` 整个文件夹复制到小车电脑的固定位置，例如：

   ```text
   D:\PA1664\robot_host
   ```

2. 确认目录结构为：

   ```text
   D:\PA1664\robot_host\app\PA1664RobotHost.exe
   D:\PA1664\robot_host\config\host.psd1
   D:\PA1664\robot_host\StartRobotHost.cmd
   ```

3. 按需修改 `config\host.psd1`。默认配置为：

   ```powershell
   @{
       ListenAddress = '0.0.0.0'
       ControlPort   = 43100
       RealtimePort  = 43101
       MediaPort     = 43102
       BulkPort      = 43103
   }
   ```

4. 双击 `StartRobotHost.cmd`，先做一次手动启动测试。
5. 检查 `app\logs\robot_host.log` 和 `runtime\launcher.log`。
6. 确认能正常启动后，双击 `InstallAutoStart.cmd`。

安装后会产生两个同名快捷方式：

- 当前用户桌面：`PA1664 Robot Host`；
- 当前用户登录启动目录：`PA1664 Robot Host`。

这样每次该 Windows 用户登录时，Host 会在隐藏窗口中启动。Host 自身有单实例锁，重复启动不会产生第二个实例。

## 三、操作电脑部署

1. 将本目录中的 `operator_workbench` 整个文件夹复制到操作电脑的固定位置，例如：

   ```text
   D:\PA1664\operator_workbench
   ```

2. 编辑 `config\workbench.psd1`，把 `Host` 改为小车电脑的静态 IP：

   ```powershell
   @{
       Host        = '192.168.50.20'
       ControlPort = 43100
   }
   ```

3. 确认存在：

   ```text
   D:\PA1664\operator_workbench\app\PA1664Workbench.exe
   ```

4. 双击 `StartOperatorWorkbench.cmd`，先确认能打开原有界面并连上 Host。
5. 双击 `InstallAutoStart.cmd`，安装桌面快捷方式和当前用户登录自启动。

安装后会产生：

- 桌面快捷方式：`PA1664 Operator Workbench`；
- 登录启动目录快捷方式：`PA1664 Operator Workbench`。

Workbench 启动后保持原有最大化界面和业务布局，只通过 `--remote 小车IP:43100` 进入远程模式。

## 四、开机自启动的实际含义

当前方案是**当前 Windows 用户登录后自动启动**，不是 Windows 服务在登录前启动。

这是有意设计：

- 相机、USB、PCAN/candle 和部分厂商 SDK 可能依赖交互式桌面会话；
- Windows Session 0 服务会话可能无法访问 USB 相机或厂商设备；
- 如需真正无人值守开机，应让小车电脑使用专用账号自动登录，再由登录启动目录拉起 Host；
- 不建议直接把 Host 注册为 Session 0 服务。

必须确保：

1. 安装快捷方式时使用的 Windows 用户，就是以后自动登录的同一个用户。
2. 开机自启动的快捷方式只能由当前用户使用，不需要管理员权限。
3. 小车电脑若需要无人值守，应先配置受控的 Windows 自动登录策略。
4. 软件启动不等于安全回路启动，硬线急停和断电保护必须独立有效。

## 五、手动启动、卸载和排障

手动启动：

```text
robot_host\StartRobotHost.cmd
operator_workbench\StartOperatorWorkbench.cmd
```

也可以直接点击安装后生成的桌面快捷方式。

卸载自启动：

```text
robot_host\UninstallAutoStart.cmd
operator_workbench\UninstallAutoStart.cmd
```

卸载只删除桌面和登录启动快捷方式，不停止正在运行的程序，也不删除 `app`、配置和日志。

日志位置：

- `robot_host\runtime\launcher.log`：启动器是否找到 EXE、是否发现已有进程、进程 PID 和启动退出码；
- `robot_host\app\logs\robot_host.log`：Host 网络、硬件和安全日志；
- `operator_workbench\runtime\launcher.log`：Workbench 启动器和远程目标地址；
- `operator_workbench\app\logs`：Workbench 业务日志。

常见问题：

1. **提示 Missing executable**：`app` 中没有放完整程序，或复制了错误目录。
2. **提示 exited during startup**：检查 `app\logs`，通常是 DLL、插件、端口占用或厂商 SDK 初始化失败。
3. **Workbench 打不开远程画面**：检查 `workbench.psd1` 的 IP、Host 防火墙、四个端口和 Host 日志。
4. **登录后没有自动启动**：确认安装时和登录时是同一 Windows 用户，并在任务管理器“启动应用”中确认快捷方式未被系统禁用。
5. **移动目录后快捷方式失效**：重新运行一次 `InstallAutoStart.cmd`。

## 六、快捷方式使用范围

本方案创建的都是当前用户级快捷方式，不修改系统级服务，不写注册表自启动项，不需要管理员权限。删除目录前先运行 `UninstallAutoStart.cmd`。