# mv3dlp_laser_profile

面向海康 3D 线激光轮廓仪的独立 C++ 封装模块，优先按 Linux 部署和后续 ROS 集成来设计。

## 设计目标

- 对上提供稳定的纯 C++ 接口，不把 ROS 直接耦合进底层采集库。
- 对下通过运行时动态加载厂商 SDK，避免在编译期强绑定厂商 `.so` 路径。
- 支持后续拆成两层：
  - `mv3dlp_laser_profile`：纯采集和数据转换库
  - `mv3dlp_ros2_driver`：ROS2 节点、参数、topic 和诊断层

## 当前结论

按 2026-08-12 本机安装内容检查，当前已经安装的开发包里只有 Windows `dll/lib`，没有 Linux `.so`。  
所以这个模块已经先按 Linux C++ 方式封好接口，但真正接到 Linux 设备时，仍然需要你补齐厂商 Linux 运行库。

## 目录结构

```text
mv3dlp_laser_profile/
  include/mv3dlp_laser_profile/
    driver.hpp
    types.hpp
  src/
    driver.cpp
    vendor_sdk.hpp
    vendor_sdk.cpp
  examples/
    fetch_frame.cpp
  vendor/
    linux-x86_64/
      README.md
```

## 为什么这样拆

如果现在直接把 ROS2、线程、消息发布、参数系统全塞进一个库，后面会有几个问题：

- 采集逻辑和 ROS 生命周期强耦合，不利于调试裸 SDK 问题。
- 后续从 ROS1 切 ROS2，或者做非 ROS 工控程序时，很难复用。
- 厂商 Linux SDK 经常要跟随 `.so`、配置文件、环境变量一起调整，独立底层库更容易定位问题。

所以更稳的做法是：

1. 先把厂商 SDK 包成纯 C++ 驱动库。
2. ROS 层只做参数读取、循环采集、消息发布、重连和诊断。

## 已封装能力

- SDK 动态加载
- SDK 初始化/释放
- 枚举设备
- 按序列号或 IP 连接
- 开始/停止采集
- 拉取一帧原始数据
- 设置常见参数
- 深度图转点云
- 设备异常回调转成 C++ handler

## 构建

Linux 上建议：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Linux SDK 放置方式

优先级从高到低：

1. `DriverOptions.library_path`
2. 环境变量 `MV3DLP_LIBRARY_PATH`
3. 环境变量 `MV3DLP_SDK_ROOT`
4. 本模块目录下的 `vendor/linux-x86_64/`
5. 系统默认库目录，例如 `/usr/local/lib`

推荐把厂商 Linux 运行库放到：

```text
vendor/linux-x86_64/libMv3dLp.so
```

如果还有依赖的其他 `.so`，也一起放在同目录，并在运行前配置 `LD_LIBRARY_PATH`。

## 示例

示例程序在 `examples/fetch_frame.cpp`，流程是：

1. 加载 SDK
2. 枚举设备
3. 默认连接第一台设备
4. 切到范围图模式
5. 采一帧
6. 如为深度图则转点云

## 后续接 ROS2 的建议

建议单独再建一个 `mv3dlp_ros2_driver` 包，底层直接依赖本模块。

推荐 ROS2 节点参数：

- `serial_number`
- `device_ip`
- `frame_id`
- `acquisition_mode`
- `fetch_timeout_ms`
- `auto_reconnect`
- `publish_depth`
- `publish_intensity`
- `publish_pointcloud`

推荐 topic：

- `/mv3dlp/depth`
- `/mv3dlp/intensity`
- `/mv3dlp/points`
- `/diagnostics`

推荐职责边界：

- 本模块负责：设备连接、参数设置、采帧、数据转换
- ROS2 层负责：线程循环、topic 发布、时间戳映射、重连、诊断、launch

## 注意

- 这个库当前不直接链接厂商 SDK，而是运行时加载，所以没有 Linux `.so` 时也能先编译通过。
- 真机联调前，仍然要确认厂商 Linux SDK 的库名、依赖库和配置文件要求。
- 如果厂商 Linux 版函数名和 Windows 版不一致，需要在 `src/vendor_sdk.cpp` 中补一层兼容映射。
