# PA-1664 远程控制

LanRemoteQt 作为 `PA1664Workbench` 的远程桌面组件运行。远程画面、键鼠、文件传输仍使用原有通道；PA-1664 设备命令使用独立的低频业务通道。

## 启动方式

1. 在 PA-1664 工作台中选择“工具 → 打开远程桌面”。
2. 被控端在 LanRemoteQt 中勾选“允许远程超声功能控制”。
3. 正常建立远程桌面连接；控制端的“超声控制台”按钮随后可用。

开发阶段也可以直接运行 `LanRemoteQt.exe`。程序会自动查找同目录、上级目录或 PATH 中的 `PA1664Workbench.exe`；也可通过“PA-1664 工作台路径...”手工选择。启动白名单只接受这个文件名。

## PA-1664 控制台

控制台已经移除旧项目专用的 DPR500 和运动平台页，改为当前小车超声项目的接口：

- 查看工作台、SDK、USB 硬件、采集、帧数、点数、声束和工作组状态；
- 指定服务地址和设备 ID，连接或断开 SDK；
- 开始/停止采集以及编码器复位；
- 读取和修改工作组、增益、声程起止、工件声速、采样点、PRF、TX/RX 通道；
- 写参数时自动停止采集、整批推送，并按写入前状态恢复；
- 启动或置前远端 PA-1664 工作台。

开始采集和参数整批写入均要求控制端二次确认，远端还会重新校验 SDK 连接状态和参数范围。

## 进程与安全边界

远程桌面保持 Qt 5/MSVC 2019 独立进程，PA-1664 工作台保持 Qt 6/MSVC 2022 独立进程，避免 Qt 与 FFmpeg/libyuv 的 ABI 和 DLL 搜索路径冲突。两者通过 `PA1664Workbench-Control-v1` 本机命名管道通信；管道使用 `QLocalServer::UserAccessOption`，仅允许同一 Windows 用户访问，不监听 TCP/UDP。

协议为每行一个紧凑 JSON 对象，单条上限 2 MiB。网络侧只转发以下固定操作：

- `ultrasound.capabilities.get`
- `ultrasound.state.get`
- `ultrasound.configuration.get`
- `ultrasound.configuration.patch`
- `ultrasound.command.execute`
- `ultrasound.app.status`
- `ultrasound.app.launch`
- `ultrasound.app.deploy.prepare`

设备命令仅支持 `device.connect`、`device.disconnect`、`acquisition.start`、`acquisition.stop`、`encoder.reset` 和 `workbench.activate`，不能传入任意程序或命令行。

## 构建与部署

- PA-1664 工作台：Qt 6.8.3 / MSVC 2022 x64，运行 `D:\car\PA1664Workbench\build.cmd`。
- 远程桌面：Qt 5.15.2 / MSVC 2019 x64，运行本目录的 `build-msvc.bat`。
- 工作台部署脚本会把远程桌面发布目录复制到 `bin\RemoteDesktop`。也可以通过环境变量 `LANREMOTEQT_DIST_DIR` 指定其他发布目录。

软件包远程部署只登记压缩包中的 `PA1664Workbench.exe`。采集卡驱动、USB 驱动和需要 UAC 的系统服务不会被静默安装。
