# 爬壁机器人 ROS 2 便携运行包

本目录是 Windows 11 x64 目标电脑运行包。复制整个目录到另一台电脑即可运行；目标电脑不需要另行安装 ROS 2、Python、CMake、colcon 或工作区。

The `runtime` folder contains the Python interpreter used by ROS 2 launch and
environment scripts; do not remove it from the copied folder.

## 启动

Double-click `run_robot.cmd` (or the desktop shortcut created by
`create_shortcut.ps1`). The launcher loads the bundled ROS runtime and this
folder's installed node overlay, then starts `crawling_robot_bringup`.

When a CANable/SLCAN USB-CAN adapter is connected, the launcher scans the
Windows serial and PNP device lists, selects the matching `COMx` port, and
starts the SLCAN bridge at 1 Mbps. `-UseSlcanCanBridge` can still be supplied
to force the bridge on when testing without automatic detection.

Edit `config\robot.yaml` before the first hardware test. The default values
keep CANopen motion disabled (`dry_run: true`) and serial/CAN ports empty.

The launcher also starts `crawling_robot_monitor` by default. IMU attitude and
laser profile data are shown as tabs in the same monitor window; there are no
separate sensor popups. The monitor accepts arrow keys or `W/A/S/D` for manual
movement. Set the left/right motor node IDs and direction signs in `电机节点映射`,
set speed and acceleration limits in `运动参数`, apply them, then click
`底盘使能` before moving. Mapping changes stop the wheels and require a new
enable request. Use `run_robot.cmd -SkipMonitor` when starting headless.

## 停止与诊断

运行 `stop_robot.cmd` 停止整个 ROS 进程树。所有节点与监控界面的日志统一写入本目录旁的
`logs\robot.log`；文件最多保留最新 10,000 行且最大约 2 MB，超限时自动删除最旧内容。
`run.pid` 仅在系统运行时存在，异常断电后可以删除。

## 构建新运行包

在开发机（已安装匹配的 Windows ROS 2、Visual Studio C++、CMake 和 colcon）
的仓库根目录执行：

```powershell
.\packaging\build_release.ps1 -RosRoot C:\pixi_ws\ros2-window -Clean -Zip
```

也可以双击 `packaging\build_release.cmd`，再按需传入相同参数。

脚本会构建源码工作区，将 ROS 运行时、Python、MSVC 运行库、节点安装目录和 MV3DLP SDK
复制到干净的暂存目录，写入清单并创建 `target_board_release.zip`。生成的目录或 ZIP 即为复制到
另一台电脑的完整运行包；目标电脑不需要运行构建脚本。

The ROS runtime is expected to be relocatable (its `setup.bat` must derive its
prefix from the script location). If a vendor ROS distribution contains
machine-specific absolute paths, use that distribution's official portable
export before packaging.
