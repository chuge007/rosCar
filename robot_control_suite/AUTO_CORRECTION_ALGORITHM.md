# 自动纠偏几何算法

## 输出量定义

坐标遵循 ROS 约定：`base_link` 的 `x` 向前、`y` 向左、`z` 向上。

- `lateral_error_m > 0`：焊缝位于小车控制参考点左侧；
- `preview_lateral_error_m`：前视距离处曲线相对目标轨迹的横向误差；
- `heading_error_rad > 0`：焊缝局部切线相对车头向左；
- `curvature_1pm`：前视点处焊缝曲率，单位 `1/m`；
- `fit_residual_m`：历史焊缝点到拟合曲线的加权平均残差；
- `geometry_valid`：时间同步、点数、纵向跨度、残差和角度门限全部通过。

## 计算流程

1. 单帧点云使用中值滤波、低分位壁面拟合和凸起显著性加权质心，得到传感器坐标中的焊缝中心。
2. 根据 `forward/lateral/height_axis`、对应符号和激光平面安装外参，把焊缝中心转换到 `base_link`。
3. 按激光时间戳在线性插值 `/odom` 位姿，再把焊缝点转换到统一的 `odom` 坐标系。
4. 保留最近 `trajectory_window_m` 行程内的点，并在当前车体坐标系中拟合局部二次曲线：

   ```text
   y(x) = a*x^2 + b*x + c
   ```

   为改善数值条件，程序内部使用 `u=x/trajectory_window_m`。拟合以轮廓置信度为基础权重，执行 4 次 Huber 迭代重加权，错误识别点不会直接拉偏曲线。
5. 用 Newton 迭代求车体参考点 `(0,0)` 到曲线的最近点 `(x*,y*)`。该点切线斜率为：

   ```text
   u* = x* / trajectory_window_m
   k = (2*a*u* + b) / trajectory_window_m
   ```

   其中 `k=dy/dx` 是物理坐标下的切线斜率。
6. 航向角误差和有符号法向距离为：

   ```text
   heading_error = atan(k)
   lateral_error = (y* - k*x*) / sqrt(1 + k^2) - target_lateral_m
   ```

相比相邻两帧差分，该方法使用一段实际行程，能够抑制点云噪声；相比整段直线拟合，局部二次曲线可以保留缓弯焊缝在小车附近的真实切线。

## 柔和控制律

控制器使用前视横向项、航向项和曲率前馈：

```text
omega_raw = steering_sign * (
    curvature_feedforward_gain * v * curvature
    + heading_gain * heading_error
    + lateral_preview_gain * atan(preview_lateral_error / preview_distance)
    + Kp * lateral_error
    + Kd * d(lateral_error)/dt
    + integral_gain * integral(lateral_error)
)
```

`integral_gain` 默认是 `0`。只有存在稳定的、已排除外参误差的固定偏差时才建议开启。积分仅在角度误差小、横向误差小且未发生角速度饱和时累积，并有 `integral_limit_m_s` 抗积分饱和。

角速度先限制在 `max_angular_z_rad_s`，再限制角加速度，最后限制角加加速度 `max_angular_jerk_rad_s3`。因此目标角速度突变不会直接传给底盘。曲率前馈让车辆在进入弯曲焊缝前开始转向，前视横向项使回轨过程平滑，不会只盯着车底最近点左右追逐。

## 有效性门限

- 至少 `min_fit_points` 个轨迹点；
- 实际纵向跨度至少 `min_fit_span_m`；
- 里程计与激光时间差不超过 `max_odom_age_ms`；
- 平均拟合残差不超过 `max_fit_residual_m`；
- 航向误差不超过 `max_heading_error_rad`。

几何估计尚未建立时，节点暂时使用经过外参修正的单帧横向值低速控制，角度项为零。建立足够行程后自动切换到几何估计，并清除微分状态，避免切换冲击。

## 必须标定的参数

- `laser_forward_m`：`base_link` 原点到激光原点的前向距离；
- `laser_lateral_offset_m`：激光原点相对 `base_link` 的左向距离；
- `laser_yaw_rad`：激光平面坐标相对 `base_link` 的平面偏航角；
- `forward_axis/lateral_axis/height_axis`：厂商点云中三个物理方向对应的字段；
- `forward_sign/lateral_sign/height_sign`：字段方向与 ROS 方向相反时设为 `-1`；
- `target_lateral_m`：希望焊缝相对小车控制参考点保持的法向距离，通常为 `0`。

外参误差会直接形成横向距离系统误差，IMU 航向误差和计米轮比例误差会影响角度拟合。实机应先用平直焊缝和已知偏移量完成静态标定，再做动态控制参数整定。
