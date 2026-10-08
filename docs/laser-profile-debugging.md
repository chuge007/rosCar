# 激光轮廓调试

参考工程为 `D:/dev/deskCrawlingRobot/crawling_robot_desktop2`。其 `CrawlingRobotDesktop.pro` 实际编译 `../modules/mv3dlp_laser_profile/windows_x64/src` 中的驱动，而不是模块根目录的 `src`。工作台使用本地 `vendor/mv3dlp_laser_profile` 中同步后的 Windows 实现，不依赖参考工程目录编译。

## 已确认的差异

2026-10-08 最新日志中，`laser_profile_received` 已收到 3200 个点，但 `positive_z=0`、`increasing_x=0`，X/Z 范围全为零。因此信号已进入检测和预览链路，收到帧不等于收到有效轮廓。

旧工作台驱动的轮廓回调只有 `(profile, user)` 两个参数；Windows 参考驱动为 `(profile, intensity, user)` 三个参数。必须同时同步 SDK 回调类型和回调入口，否则无法正确取得用户指针。

轮廓回调数据按参考实现解码为 XYZ_S16，使用 -32768 判断无效坐标，根据实际负载大小计算每行点数。设备声明的 3200 点容量不能代替实际负载点数。参考实现中，非零有效比例尺用于坐标转换；零比例尺保留原始坐标，不能把整条轮廓乘成零。零比例尺路径的坐标是设备原生单位，不能据此声称已标定为毫米。

图像回调与轮廓回调分开处理：图像回调原样复制缓冲区，不能因帧类型同为轮廓而再次套用整数解码。队列按通道保留最新帧；启停采集时不持有回调需要的队列锁。以上均按 Windows 参考实现同步。

## 真机复核

1. 重新编译修改后的工作台。仅替换源码不会改变 `bin/PA1664Workbench.exe`。
2. 退出参考工程、其他工作台实例和占用激光设备的调试工具，只运行新编译的工作台。旧日志出现过相机和 COM6 的访问拒绝。
3. 在小车页连接激光设备，选择 X/Z 预览。轮廓预览无需启动自动纠偏。等待约 10 秒，保留该次运行的 `bin/logs/robot_console.log`。
4. 依次检查下表。焊道检测失败时，有效轮廓仍应显示；焊道定位和轮廓显示需分别判断。

| 日志事件 | 关注字段 | 判断 |
| --- | --- | --- |
| `driver_initialize` | `library`、`sdk` | 确认实际选中的 SDK 路径和版本 |
| `profile_capabilities` | `supported_image_modes`、`requested=4` | 按参考工程查询能力；查询不可用时记录错误并尝试模式 4 |
| `first_valid_frame` | `type`、`size`、`data_bytes` | 确认本次运行收到的帧尺寸和转换后字节数 |
| `payload_snapshot` | `callback`、`raw_size`、`raw_bytes`、`decoded_size`、`decoded_bytes`、`scales`、`offsets`、`decode_reason` | 区分 SDK 图像回调与 XYZ_S16 轮廓回调；对照解码前后的点数和字节数 |
| `profile_emit` | `width`、`height`、`selected_row`、`samples`、`valid_xz` | 选择最后一条完整扫描；`valid_xz` 统计有限且 Z 为正的点，不再只是数组长度 |
| `laser_profile_received` | `age_ms`、`positive_z`、`increasing_x`、X/Z 范围 | 确认纠偏控制器拿到新鲜且有几何变化的轮廓 |
| `laser_profile_observation_emit` | `valid`、边界索引、置信度 | 空闲时的本帧焊道检测结果；`valid=0` 不代表没有轮廓数据 |
| `profile_ui_received` | `frame`、`age_ms`、`visible`、`widget_size`、`received_profiles` | 确认 UI 已接收该轮廓，检查线程排队延迟和页面是否可见 |
| `profile_paint` | `frame`、`plotted_points`、`nonfinite`、`nonpositive_z`、`nonincreasing_x`、`reason` | 确认已进入绘制，判断哪些点被过滤；`view_mode=1` 为 X/Z |
| `profile_not_emitted` | `frame`、`reason` | 定位轮廓模式未开启、点数不足、行数据不完整或非几何帧 |
| `frame_stream_stalled` | `image_callbacks`、`profile_callbacks` | 无帧时检查是否进入驱动回调；计数为驱动实例生命周期累计值 |

每行包含进程编号 `[P...]`，不要把不同进程的帧号串成一条链路。先按进程编号筛选，再依次看 `payload_snapshot → profile_emit → laser_profile_received → profile_ui_received → profile_paint`。日志各阶段独立限频，抽到的帧号可能不同；连续若干秒内应能看到各阶段持续前进。

`payload_snapshot` 的原始采样最多 36 字节，为负载头部、中部和尾部各最多 12 字节的十六进制数据，缓冲区只在 SDK 回调期间读取。原始采样仅用于解码排查，不当作整帧或标定结果。`profile_emit` 另记解码后扫描的头、中、尾 X/Z。重复快照、UI 接收和绘制日志各阶段每秒至多一次。`sdk_exception` 记录 SDK 异步错误，`acquisition_inactive` 记录意外停止采集；采集或转换抛异常时按现有策略记录 `capture_exception action=disconnect` 并断开设备。

若 `profile_emit` 的有效点为零，继续定位采集或解码，保留 SDK 同时段日志。若轮廓有有效正 Z 和递增 X，而界面仍为空，检查预览投影及 UI 信号链路。若有轮廓但焊道 `valid=0`，再检查工件轮廓和检测参数，不通过关闭无效点过滤伪造轮廓。

焊道检测、路径估计、轨迹预测和纠偏控制算法已与参考工程逐文件核对，保留 Qt 6 所需的容器长度类型转换以及限频诊断日志。工作台使用自身保存的底盘尺寸、电机方向及通信设置，因此现场参数也需要单独核对。

本次只做源码与静态检查，没有编译或真机采集验证；预览恢复须以上述单实例运行结果确认。
