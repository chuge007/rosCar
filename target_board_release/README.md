# Crawling Robot ROS 2 portable release

This folder is the target-board runtime package for Windows 11 x64. Copy the
whole folder to the board; no ROS 2, Python, CMake, colcon, or workspace setup
is required on the board.

The `runtime` folder contains the Python interpreter used by ROS 2 launch and
environment scripts; do not remove it from the copied folder.

## Start

Double-click `run_robot.cmd` (or the desktop shortcut created by
`create_shortcut.ps1`). The launcher loads the bundled ROS runtime and this
folder's installed node overlay, then starts `crawling_robot_bringup`.

To use an SLCAN USB-CAN adapter:

```powershell
run_robot.cmd -UseSlcanCanBridge
```

Edit `config\robot.yaml` before the first hardware test. The default values
keep CANopen motion disabled (`dry_run: true`) and serial/CAN ports empty.

The launcher also starts `crawling_robot_monitor` by default. It opens an IMU
attitude popup and a laser profile point-cloud window, and accepts arrow keys
or `W/A/S/D` for manual movement. Click `底盘使能` in the monitor before
moving; releasing a key publishes zero velocity. Use `run_robot.cmd
-SkipMonitor` when starting headless.

## Stop and diagnostics

Run `stop_robot.cmd` to stop the complete ROS process tree. Per-node logs are
written into `logs\` beside this README, one file per node, and each file keeps
only the latest 10,000 lines. `run.pid` is created only while the stack is
running and is safe to delete after an unclean power-off.

## Building a new package

在开发机（已安装匹配的 Windows ROS 2、Visual Studio C++、CMake 和 colcon）
的仓库根目录执行：

```powershell
.\packaging\build_release.ps1 -RosRoot C:\pixi_ws\ros2-window -Clean -Zip
```

也可以双击 `packaging\build_release.cmd`，再按需传入相同参数。

The script builds the source workspace, copies the ROS runtime, installed
overlay and MV3DLP SDK into a clean staging directory, writes a manifest and
creates `target_board_release.zip`. The generated directory is the one to copy
to the target board. The target board never runs this build script.

The ROS runtime is expected to be relocatable (its `setup.bat` must derive its
prefix from the script location). If a vendor ROS distribution contains
machine-specific absolute paths, use that distribution's official portable
export before packaging.
