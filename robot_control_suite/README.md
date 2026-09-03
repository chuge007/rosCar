# 爬壁机器人控制套件

这个新文件夹包含操作电脑上的控制界面，以及开发板端的线激光焊缝纠偏 ROS 2 节点。

## 桌面端 Qt 控制程序

主桌面程序是 `robot_control_suite/robot_control_suite` 下的 Qt Widgets 工程，使用 Qt 5.12.4/MSVC2017 x64 编译。程序通过 `QProcess` 调用 `ros2` CLI，因此操作电脑只需要安装与开发板匹配的 ROS 2 环境并让 `ros2` 加入 PATH。

直接运行已经生成的程序：

```powershell
robot_control_suite\robot_control_suite\release\robot_control_suite.exe
```

发布目录已经包含匹配的 Qt 运行库和 Windows 平台插件，可直接运行。若在 Qt Creator 中打开 `robot_control_suite.pro` 重新构建，或在 Qt/MSVC2017 x64 命令行中执行：

```powershell
qmake robot_control_suite.pro -spec win32-msvc
nmake release
```

如果 Qt 安装在 `D:\qt\5.12.4\msvc2017_64`，请使用该目录的 `qmake.exe`，并调用 Visual Studio 2017 的 `vcvars64.bat`。不要把 x64 程序和 32 位 Qt DLL 或 `qwindows.dll` 混用。

界面支持：

- W/A/S/D 或方向键前进、后退、原地转向，松键自动减速；
- 基础速度滑块（0.005--0.12 m/s）；
- `/drive/enable` 底盘使能；
- `/laser_correction/enable` 自动纠偏开关；
- `/laser_correction/reset` 滤波器复位；
- 急停按钮会禁用底盘和纠偏，并持续发送零速度。
- “打开轮廓成像”窗口实时显示线激光点云的横向-高度轮廓，黄色线为目标中心，红色线为识别出的焊缝中心。
- 主窗口直接显示横向误差、前视误差、角度误差、曲率、角加速度、实际线/角速度、置信度和拟合残差。
- 轮廓窗口从 `/laser_profile/frame` 解析 `PointCloud2` 的 x/y/z 浮点数据并实时绘图；不需要桌面端安装 `rclpy`。
- `ROS_DOMAIN_ID` 通过界面设置，随后所有 CLI 子进程都会继承该值。

原来的 `desktop/robot_control_panel.py` 保留为 Python 诊断/备用版本，不再是主控制入口。

## 开发板端

在 `ros2_ws` 构建后，`robot.launch.py` 会自动启动 `laser_path_follower_node`。首次联调建议：

1. 轮子悬空，低速发布轮廓，确认 `lateral_axis`、`height_axis` 和 `steering_sign`。
2. 将 `prominence_threshold_m` 调到焊缝凸起高度的 30--50%，避免把纹理当焊缝。
3. 在 `robot.yaml` 中先保持 `target_speed_m_s: 0.025`，确认误差单调收敛后再提高速度。
4. 有效轮廓丢失超过 `profile_timeout_ms` 会自动降到零速；角速度和线速度均受斜率限制，避免反复摇摆。

节点话题和服务：

- 订阅 `/laser_profile/frame`，发布 `/cmd_vel` 和 `/laser_correction/status`；
- `ros2 service call /laser_correction/enable std_srvs/srv/SetBool "{data: true}"`；
- `ros2 service call /laser_correction/reset std_srvs/srv/Trigger "{}"`。

纠偏算法先按每帧点云的横向轴排序，用低分位点拟合壁面基准，再对高于基准的凸起区域做加权质心。连续焊缝中心点经激光外参和时间同步里程计变换到 `odom`，再通过 Huber 鲁棒局部二次曲线拟合，计算小车到轨迹的有符号法向距离与最近点切线角。控制器组合角度项和横向距离项，并保留低通、速度限制和丢线停车。

几何定义、公式和标定方法见 `AUTO_CORRECTION_ALGORITHM.md`。

当前控制器已加入前视横向控制、轨迹曲率前馈和角加加速度限制。`integral_gain` 默认关闭；现场先调 `preview_distance_m`、`heading_gain`、`lateral_preview_gain` 和 `max_angular_jerk_rad_s3`，确认无蛇形摆动后再考虑启用积分。
