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

## 焊道定位算法与时效诊断（2026-10-12）

实时预览和自动纠偏共用下拉框选择的定位算法。每条 `profile_localization` 摘要记录实际执行的 `mode` 和 `algorithm`，不要根据界面当前选项推断历史帧使用的算法。

| mode | algorithm | 定位方式 |
| --- | --- | --- |
| 0 | `baseline_shoulder` | 基线去除后通过肩部和拱起点位定位，不运行模板匹配 |
| 1 | `template_only` | 对基线残差进行多尺度模板滑窗匹配 |
| 2 | `hybrid` | 点位处理产生候选，模板校验候选并可修正目标位置；模板未通过时仍可能由点位处理定位 |

切换算法另记 `event=profile_algorithm_changed`。输入等待处理时仅保留最新轮廓；`replaced_frames` 表示本次消费前被新轮廓替换的待处理帧数，`replaced_total` 是控制器生命周期累计值，不是 SDK 丢帧数。默认超时仍为 180 ms，可在安全看门狗中设置；控制器输入检查、计算结果检查和实时窗口隐藏定位线共用此阈值，日志记录实际 `timeout_ms`。

### 两类日志

`bin/logs/robot_console.log` 的 `LASER.LOCATE` 分类保存轻量摘要，不依赖“记录检测计算日志”勾选。常态约每秒一条，结果状态变化时可增加输出，但相邻摘要至少间隔 100 ms。控制器保留最近 300 个已处理输入的摘要，纠偏停止时在发出零速命令后输出 `profile_localization_history` 和这些摘要，用于复核停机前每一帧。

`bin/logs/profile_weld_detection.log` 用于详细计算排查。勾选后在纠偏日志会话内至多每 250 ms 选择一帧记录点云、残差和候选，正常情况下每 30 个采样批次落盘一个，其余保留最近 320 个未写批次。候选失败、模板失败、宽度拒绝或结果过期触发缓存输出，并保留故障后 3 秒的采样数据；两次故障缓存触发至少间隔 10 秒。停止或关闭详细记录时输出剩余缓存。已输出的详细批次不再放回缓存；320 批次按采样数量限制，不保证固定秒数。

### 先区分匹配、时效与控制接受

| 字段 | 含义 |
| --- | --- |
| `detector_ran` | 此输入是否实际执行检测；过期输入、点数不足或重复帧可能在检测前被拒绝 |
| `template_evaluated` / `template_matched` | 是否进入模板模式，以及当前结果是否通过模板门槛；不代表控制器已接受 |
| `detection_valid` / `detector_reason` | 当前帧算法结果是否有效及检测原因，独立于后续时效和连续帧确认 |
| `result` | 本帧整体结果；`result_stale` 表示计算完成时结果已过期，即使模板匹配成功也不会更新跟踪测量 |
| `control_decision` | 控制器本帧走到的接受或拒绝分支；空闲预览为 `idle_observation`，检测前拒绝为 `not_run` |
| `control_measurement_accepted` | 当前帧是否真正更新了控制器的有效测量时间；尚未完成确认时为 0 |
| `control_gap_valid` / `detection_held` | 控制状态是否仍有有效焊道、是否暂时沿用旧测量；不能当成当前帧检测成功 |
| `confirmation_count` / `width_confirmation_count` | 初始候选连续确认数、宽度突变连续确认数；单帧候选不会直接改写跟踪宽度 |

例如，`algorithm=template_only template_matched=1 detection_valid=1 result=result_stale control_measurement_accepted=0` 表示模板找到了焊道，但算完已经过期。`control_decision=initial_confirmation_pending` 表示候选仍在连续帧确认；`width_confirmation_pending` 表示模板支持的宽度变化尚未确认。`association_rejected` 表示控制端关联门槛拒绝；`width_jump_without_template` 表示宽度突变缺少模板支持。只有 `control_measurement_accepted=1` 才说明本帧更新了控制测量。

输入被时效或点数门槛拒绝、或者计算结果过期时，初始确认和宽度突变确认计数均清零；超时前后的候选不能拼成连续确认。

### 定位耗时来源与模板失败原因

| 字段 | 排查用途 |
| --- | --- |
| `received_epoch_ms` | 输入原始接收时间，不能以计算完成时间替代它规避超时 |
| `entry_age_ms` | 进入本次处理时输入已过去的时间；偏大时检查上游和等待处理的延迟 |
| `detector_ms` | 检测算法耗时；`detector_ran=0` 时为 -1 |
| `result_epoch_ms` / `result_age_ms` | 检测结果进入后续处理门槛时的时间及输入年龄；与 `timeout_ms` 对照判断结果是否过期 |
| `completed_epoch_ms` / `completed_age_ms` | 本次处理完成摘要时的时间及年龄，包含控制处理和采样点云序列化；后者变大不等于本帧先前已接受的测量过期 |
| `processing_ms` | 本次处理开始到摘要生成前的耗时，包含该采样帧的详细数据整理 |
| `template_name` / `similarity` / `threshold` / `width_scale` | 成功模板或最佳失败比较的名称、相似度、门槛、尺度；未发生有效比较时名称可能为空、分数与门槛为 0 |
| `usable_templates` / `windows` / `comparisons` | 可用模板数、搜索窗口数、实际相似度比较数，区分没有可用模板与匹配分数不足 |
| `prior_rejected` / `signal_rejected` / `coverage_rejected` | 被位置或尺度先验、残差幅值、有效采样覆盖率拒绝的数量；混合模式的逐候选尺度拒绝可查看详细 `template_comparison` |

纯模板检测的 `detector_reason` 会区分 `no_enabled_template`、`no_usable_template`、`similarity_below_threshold`、`insufficient_template_coverage`、`insufficient_raised_signal`、`template_prior_rejected` 和 `no_template_window`。多个门槛均有拒绝时，原因给出优先判断，结合各拒绝计数分析。`runner_up_similarity` 可能来自相邻且重叠的窗口，不能直接解读为第二条焊道的分数。

分析时先按进程 `[P...]` 和 `session` 分组，再按 `received_epoch_ms` 排序。停止时重写的历史摘要可能和之前限频输出的摘要重复，按进程、`frame`、`received_epoch_ms` 去重；历史行前缀是缓存落盘时间，不是该帧发生时间。先看 `correction_task_stopped` 的真实停止原因，再对照同一会话的摘要，不把定位失败与输入超时混为一谈。

### 本轮验证范围

纯模板搜索保留逐点滑窗、原尺度门槛、41 点浮点归一化和 NCC 相似度，改用单调边界与最大值队列减少重复扫描，并预计算模板统计量、消除每窗口临时容器分配。离线脚本 `tmp/profile-localization-20261012/validate-localization.cjs` 对随机完整及缺测数据、覆盖率边界和附件中三个去重残差帧进行了数值对比；附件对比使用从成功窗口派生的模板，并非用户保存模板的完整重放。源码解析未发现相对备份新增的语法错误。按 Qt 工程约定未执行编译，这些检查不等于 C++ 运行、线程并发或真机测速验证；实际耗时和停车行为须使用新编译程序采集的日志复核。
