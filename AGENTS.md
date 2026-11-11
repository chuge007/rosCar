<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **rosCar** (43849 symbols, 56190 relationships, 300 execution flows). Use the GitNexus MCP tools to understand code, assess impact, and navigate safely.

> Index stale? Run `node .gitnexus/run.cjs analyze` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? `npx gitnexus analyze` (npm 11 crash → `npm i -g gitnexus`; #1939).

## Always Do

- **MUST run impact analysis before editing any symbol.** Before modifying a function, class, or method, run `impact({target: "symbolName", direction: "upstream"})` and report the blast radius (direct callers, affected processes, risk level) to the user.
- **MUST run `detect_changes()` before committing** to verify your changes only affect expected symbols and execution flows. For regression review, compare against the default branch: `detect_changes({scope: "compare", base_ref: "main"})`.
- **MUST warn the user** if impact analysis returns HIGH or CRITICAL risk before proceeding with edits.
- When exploring unfamiliar code, use `query({search_query: "concept"})` to find execution flows instead of grepping. It returns process-grouped results ranked by relevance.
- When you need full context on a specific symbol — callers, callees, which execution flows it participates in — use `context({name: "symbolName"})`.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method without first running `impact` on it.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit changes without running `detect_changes()` to check affected scope.

## Resources

| Resource | Use for |
|----------|---------|
| `gitnexus://repo/rosCar/context` | Codebase overview, check index freshness |
| `gitnexus://repo/rosCar/clusters` | All functional areas |
| `gitnexus://repo/rosCar/processes` | All execution flows |
| `gitnexus://repo/rosCar/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
|------|---------------------|
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

# 仓库认知（本地事实，2026-11-11 按实际文件核对）

本仓库是 **PA1664 爬壁机器人控制系统**，纯 C++（Qt 6.8.3 + MSVC 2022，qmake 工程，C++17），上位机/下位机分离。所有改动只落在本目录内，`PA1664WorkbenchTmp2` 只读。

## 三个可执行目标

| 目标 | 工程文件 | 入口 | 输出 |
|------|----------|------|------|
| `PA1664Workbench`（操作端 GUI） | `PA1664Workbench.pro` | `src/main.cpp` | `bin\PA1664Workbench.exe`（bin 即运行目录，无需复制） |
| `PA1664RobotHost`（小车端后台服务） | `PA1664RobotHost.pro` | `src/backend/main.cpp` | `bin\PA1664RobotHost.exe`，需推到 `startup_deploy\robot_host\app\` 与小车端 `C:\Users\a\Desktop\robot_host\app\` |
| `PA1664CameraServer`（激光相机独立进程） | `PA1664CameraServer.pro` | `src/camera_server_main.cpp` | `bin\PA1664CameraServer.exe` |

## 构建脚本（真实行为）

| 脚本 | 实际编译内容 | 备注 |
|---|---|---|
| `build_workbench.bat` | 仅 Workbench | 显式设 `QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64` + `vcvars64.bat`，shadow build 到 `build\workbench_release` |
| `build_host.bat` | 仅 RobotHost | 同上，shadow build 到 `build\host_release`；支持参数 `clean` |
| `build_all.bat` | CameraServer + Workbench | **不含 RobotHost**，且依赖 PATH 里已有 `qmake`；"编全部三个目标"必须 `build_workbench.bat` + `build_host.bat` + `build_all.bat`（或分别执行） |

- 编译前先 `taskkill /IM PA1664Workbench.exe|PA1664RobotHost.exe|PA1664CameraServer.exe /F`，否则 `LNK1104`。
- 编完用 clangd + 根目录 `compile_commands.json`（由 `gen_compile_commands.py` / `gen_compile_commands.bat` 生成）校验。

## 代码归属（U/R/L/S，详见 `docs/超声与小车代码分工.md`）

- **U 超声**：`src/devicecontroller.*`、`packetdecoder.*`、`framedecoderworker.*`、`scanwidgets.*`、`scanwaveformcache.*`、`parameterpanel.*`、`parameterpresetstore.*`、`pulseloadguard.*`、`usbbootstrap.*`、`framerecorder.*`、`frameplayer.*`、`remotecontrolserver.*`、`src/services/*`、`src/backend/ultrasound_*`。
- **R 小车**：`src/robot/*`（控制环 `synchronized_drive_controller`，执行层 `wheel_motor_controller` + CAN `candle/pcan/zdt` + 串口 `servo/mwd_rs485/mwd_modbus/rim302`）、`robotcontrolpanel.*`、`robot_sensor_controller.*`、`robot_usb_camera_controller.*`、`network_stream_reader.*`、`camera_frame_processor.*`、`src/backend/robot_*`。
- **L 激光/焊缝**：`src/laser_*`、`opencv_laser_contour.*`、`seam_state_machine.*`、`*weld_locator.h`、`profile_weld_detector.h`、`camera_server*`、`vendor/mv3dlp_laser_profile/`。
- **S 共享层**：`src/common/protocol/*`、`src/common/transport/*`、`src/common/services/*`。
- **交界点**：`mainwindow.cpp`（超声页 + 小车页装配）、`probeadjustmentpanel.cpp`（超声 UI 驱动夹爪电机）、`src/common/*`（双用途协议/编解码）、`robot_hardware_discovery_qt6.cpp`（枚举可能碰到超声 USB）。

## 网络与日志

- 通道：TCP 43100 命令 / UDP 43101 实时 / UDP 43102 媒体 / TCP 43103 批量；实时通道 200 ms 租约，`realtime_codec` 属运动安全路径。
- 日志落盘在 exe 所在目录：`bin\logs\`（本地）、`C:\Users\a\Desktop\robot_host\app\logs\`（小车端）。`robot_console.log` = Workbench，`robot_host.log` = Host，`comm/<category>.log` = `COMM.*`，`profile_weld_detection.log` = 焊缝诊断。**超声没有专属日志文件**。
- 小车端直连 `ssh a@192.168.1.20`，部署目录 `C:\Users\a\Desktop\robot_host`，远程原生命令输出 GBK，需前置 `chcp 65001 >nul`；换 EXE 前要删 `app\robot_host.lock`。
