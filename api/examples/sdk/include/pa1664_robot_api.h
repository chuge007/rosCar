// PA1664 爬壁机器人 —— 第三方 C++ 调用接口（对外唯一头文件）
//
// 设计目的：把小车端与超声端的通信细节（TCP 43100 控制会话、UDP 43101 实时
// 运动、UDP 43102 媒体流、TCP 43103 批量流、JSON 载荷、50 ms 指令重发、
// 200 ms 运动租约、心跳、断线重连、HMAC 校验、图像/扫描数据编解码）全部封装
// 在本库内部。调用方只写：
//
//     pa1664::RobotApi api;          // 默认连 192.168.1.20:43100
//     api.start();                   // 后台建链 + 自动重连
//     api.takeControl();             // 连接底盘 + 使能
//     api.forward(0.5);              // 前进（一半速度），持续生效
//     api.stopMotion();              // 停车
//
// 线程模型：本类必须在存在 Qt 事件循环的线程中创建与使用（通常是 main），
// 所有回调都在该线程触发。底层 Qt 对象不跨线程暴露。
//
// 依赖：Qt 6 Core + Network + Gui（图像解码），C++17。与 PA1664Workbench
// 使用同一份 src/common 协议实现，因此不会与上位机行为分叉。
//
// 回调里拿到的指针（CameraFrame::data、UltrasoundAScanFrame::beams）只在
// 回调执行期间有效，需要留存请自行拷贝。

#pragma once

// ---------------------------------------------------------------------------
// 库的两种用法（二选一，链接哪一种就按哪一种配置，不要混用）
//
//   静态库  static/PA1664RobotApi.lib
//           工程里不定义任何宏，直接包含本头文件并链接该 .lib 即可。
//
//   动态库  shared/PA1664RobotApi.dll + shared/PA1664RobotApi.lib（导入库）
//           工程里必须定义 PA1664_ROBOT_API_DLL，再同时链接导入库。
//           exe 目录下需要有 PA1664RobotApi.dll，以及 Qt6Core/Qt6Network/
//           Qt6Gui 三个 DLL（可用 windeployqt 生成）。
//
// 运行时约束：本库用 /MD（动态 CRT）编译，调用方也必须使用 /MD；Qt 版本与
// 编译器版本（MSVC 2022 x64）必须与库一致，否则跨 DLL 传递 std::string /
// std::function 会崩溃。
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#  if defined(PA1664_ROBOT_API_EXPORTS)
#    define PA1664_API __declspec(dllexport)
#  elif defined(PA1664_ROBOT_API_DLL)
#    define PA1664_API __declspec(dllimport)
#  else
#    define PA1664_API
#  endif
#else
#  define PA1664_API
#endif

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pa1664 {

// ---------------------------------------------------------------------------
// 基础类型
// ---------------------------------------------------------------------------

// 底盘状态。数值与小车端协议中的 state 字段一一对应，不可更改。
enum class DriveState : int {
  Unknown = -1,
  Disconnected = 0,
  Idle = 1,
  Arming = 2,
  Enabled = 3,
  Fault = 4,
  EmergencyStop = 5,
};

// 一次调用失败的原因。Timeout 只出现在异步响应回调里；同步返回的 Result
// 可能是 None / NotOnline / InvalidArgument / Transport，以及 HostRejected
// （主机未声明所需能力）。
enum class ErrorCode {
  None = 0,
  NotOnline,        // 会话未就绪（未启动、握手中、断线）
  InvalidArgument,  // 参数越界或非有限数
  Transport,        // 本机发送失败（套接字异常、会话参数无效）
  HostRejected,     // 小车主机明确拒绝（errorCode/errorText）
  Timeout,          // 小车主机 5 s 内未响应
};

struct Result {
  bool ok = false;
  ErrorCode code = ErrorCode::None;
  std::string message;  // UTF-8 中文，可直接打印

  explicit operator bool() const { return ok; }
};

// 连通参数。速度上限必须与小车主机 DriveSettings 中的配置一致，
// 否则会被主机端限幅或判为非法目标（默认值与出厂配置相同）。
// 用 loadDriveConfig() 读到的值调用 applyDriveConfig() 可自动对齐。
struct Config {
  std::string host = "192.168.1.20";
  std::uint16_t controlPort = 43100;   // TCP 控制通道
  double maxLinearSpeedMps = 0.120;    // maximumLinearSpeedMps
  double maxAngularSpeedRadps = 0.8726646259971648;  // 50 deg/s
  std::uint16_t motionLeaseMs = 200;   // 运动租约，超时主机自动停车
};

// 遥测快照。字段来源为小车端 driveTelemetryToJson()，1:1 映射。
struct RobotStatus {
  DriveState state = DriveState::Unknown;
  std::string stateText;  // UTF-8：未连接/就绪/正在检查反馈/底盘已使能/安全故障/紧急停止
  std::string reason;
  bool commandFresh = false;   // 主机最新指令是否在超时窗口内
  bool feedbackFresh = false;  // 电机反馈是否新鲜
  double targetLinearMps = 0.0;
  double targetAngularRadps = 0.0;
  double appliedLinearMps = 0.0;
  double appliedAngularRadps = 0.0;
  double leftWheelMps = 0.0;
  double rightWheelMps = 0.0;
};

enum class LogLevel { Info, Warning, Error };

// ---------------------------------------------------------------------------
// 底盘配置（config/drive_settings.ini）
// ---------------------------------------------------------------------------

// 一台夹爪电机的配置。axis/index 就是 robot.clamp.* 请求里的坐标：
//   X 轴 = axis 0，Z 轴 = axis 2；同轴内 index 0 → 界面名 "X1"/"Z1"。
struct ClampMotorConfig {
  int axis = 0;
  int index = 0;
  std::string name;   // "X1" / "X2" / "Y1" / "Z1" / "Z2"
  std::string alias;  // 用户自定义别名，空表示未设置
  std::string mode;   // tt_canopen / mwd_rs485 / mwd_can / zdt_can
  std::string port;
  int baudRate = 115200;
  int canBitrate = 500000;
  int motorId = 1;
  int sign = 1;
  double gearRatio = 10.0;
  double upperLimit = 0.0;
  double lowerLimit = 0.0;
  bool upperLimitEnabled = false;
  bool lowerLimitEnabled = false;
};

// drive_settings.ini 中调用方真正关心的字段。raw 保留 group "drive" 的全部
// 原始键值（键名去掉了 "drive/" 前缀），未列出的字段也能从这里取。
struct DriveConfig {
  bool loaded = false;
  std::string path;   // 实际读取的文件
  std::string error;  // loaded=false 时的原因
  int schemaVersion = 0;

  // 几何与限幅
  double wheelRadiusM = 0.07953;
  double trackWidthM = 0.1473;
  double motorOutputToWheelRatio = 1.0;
  double maximumWheelSpeedMps = 0.160;
  double maximumLinearSpeedMps = 0.120;
  double maximumAngularSpeedRadps = 0.8726646259971648;
  double maximumLinearAccelerationMps2 = 0.200;
  double maximumAngularAccelerationRadps2 = 1.000;
  int commandTimeoutMs = 300;
  int feedbackTimeoutMs = 800;
  int armingTimeoutMs = 1000;

  // 轮电机总线
  std::string wheelMode;  // can / rs485
  std::string wheelCanPort;
  int wheelCanBaudRate = 115200;
  int wheelCanBitrate = 1000000;
  std::string leftPort;
  std::string rightPort;
  int leftBaudRate = 115200;
  int rightBaudRate = 115200;
  int leftMotorId = 1;
  int rightMotorId = 2;
  int leftMotorSign = -1;
  int rightMotorSign = 1;

  std::vector<ClampMotorConfig> clampMotors;

  // 摄像头通道配置（cameraSource0/1、usbCameraDeviceIndex0/1、networkCameraUrl0/1 ...）
  struct CameraConfig {
    int slot = 0;
    std::string source = "usb";  // usb / network
    int deviceIndex = -1;
    std::string url;
    int fps = 15;
    bool autoConnect = true;
    bool flipHorizontal = false;
    bool flipVertical = false;
  };
  std::vector<CameraConfig> cameras;

  // group "drive" 下的全部标量键值（不含 clampXMotors 这类数组）。
  std::map<std::string, std::string> raw;
};

// 读取本地 drive_settings.ini。path 为空时使用与上位机相同的默认位置：
// <exe 目录>/config/drive_settings.ini。
// 注意：小车端的 ini 在主机 PC 上（robot_host\app\config\drive_settings.ini），
// 若本程序不在主机上运行，需要先把该文件拷过来或手动指定路径。
PA1664_API DriveConfig loadDriveConfig(const std::string& path = std::string());

// ---------------------------------------------------------------------------
// 摄像头（USB / 网络流）
// ---------------------------------------------------------------------------

enum class PixelFormat {
  Unknown = 0,
  Grayscale8,
  RGB888,
  RGBA8888,  // 含 ARGB32，字节序为 B,G,R,A（小端）
  Indexed8,
};

struct CameraFrame {
  int slot = 0;        // 0 = 摄像头1，1 = 摄像头2
  int width = 0;
  int height = 0;
  int stride = 0;      // 每行字节数
  int sizeBytes = 0;   // stride * height
  PixelFormat format = PixelFormat::Unknown;
  const std::uint8_t* data = nullptr;  // 仅回调期间有效
};

// ---------------------------------------------------------------------------
// 自动纠偏
// ---------------------------------------------------------------------------

struct CorrectionStatus {
  bool active = false;
  bool paused = false;
  std::string phase;   // preparing / tracking / paused / stopped ...
  std::string reason;
  std::string seamStateDescription;
  double confidence = 0.0;
  double linearCommandMps = 0.0;
  double angularCommandRadps = 0.0;
  int cycleCount = 0;
};

// ---------------------------------------------------------------------------
// 超声
// ---------------------------------------------------------------------------

// A 扫视图。beams[beam][point] 为 qint16 原始采样值，纵向为深度（时间）轴。
// 指针指向解码缓存，仅回调期间有效。
struct UltrasoundAScanFrame {
  int groupId = 0;
  int beamCount = 0;          // 本次携带的波束数（通常 1，选中波束）
  int pointCount = 0;         // 每条波束的采样点数
  int sourceBeamCount = 0;    // 设备总波束数
  int selectedSourceBeam = -1;
  int scanModes = 0;          // bit0 A 扫，bit1 B 扫，bit2 C 扫
  std::uint32_t frameNumber = 0;
  std::uint64_t timestamp = 0;
  double rangeStartMm = 0.0;
  double rangeEndMm = 0.0;
  const std::int16_t* const* beams = nullptr;  // [beamCount] → pointCount 个采样
  int measurementCount = 0;
  const struct GateMeasurement* measurements = nullptr;  // [beamCount]，可为空
};

// 闸门测量（A/B/C/I 四路），与超声 SDK 的 BeamMeasurement 一一对应。
struct GateMeasurement {
  std::uint32_t amplitudeA = 0;
  std::uint32_t amplitudeB = 0;
  std::uint32_t amplitudeC = 0;
  std::uint32_t amplitudeI = 0;
  std::uint32_t positionA = 0;
  std::uint32_t positionB = 0;
  std::uint32_t positionC = 0;
  std::uint32_t positionI = 0;
};

// 超声设备状态（ultrasound.status 的 device 段）。
struct UltrasoundStatus {
  bool serviceConnected = false;
  bool hardwareOnline = false;
  bool capturing = false;
  bool captureReady = false;
  int pointCount = 1024;
  int beamCount = 1;
  int maxAmplitudeValue = 4096;
  int currentGroup = 0;
  double totalFrames = 0.0;
};

// 解码工作组配置。必须与设备真实几何一致，否则 A 扫数据解不出来。
// pointCount / beamCount / maxAmplitudeValue 从 ultrasound.status 的
// device 段读取。
struct UltrasoundDecoderConfig {
  int groupId = 0;
  int beamCount = 1;
  int pointCount = 1024;
  int amplitudeScale = 4096;
  int selectedBeam = 0;
  int scanModes = 0x1;  // 0x1 = A 扫
  double rangeStart = 0.0;
  double rangeEnd = 50.0;
};

// 本地配置镜像。库把远端底盘配置与超声参数缓存到操作端自己的 JSON 文件，
// 下次启动可直接展示、无需重新设置；配置内容始终以远端为准刷新。
//
// 文件位置默认 <exe 目录>/config/pa1664_profile.json，与主机端的
// drive_settings.ini / parameter_presets.json 完全独立，不需要随库分发。
struct ProfileOptions {
  bool enabled = true;             // 关闭后不读写本地镜像
  std::string path;                // 空 = <exe>/config/pa1664_profile.json
  bool autoApplyDecoder = true;    // 设备就绪后自动下发超声解码器配置
  bool mirrorRemote = true;        // 连上后自动把远端配置写入本地文件
  // 设备就绪后自动读一次 84 项参数（结果写本地镜像）。
  // 设备不在线时不发请求：此时 SDK 调用会阻塞，表现为请求超时。
  bool autoSnapshotUltrasound = true;
};

// ---------------------------------------------------------------------------
// 主接口
// ---------------------------------------------------------------------------

// pimpl 成员是 std::unique_ptr，C4251 是 MSVC 对导出类的已知误报：
// Impl 完全定义在 DLL 内，调用方只持有指针，构造/析构都在库内部执行。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif

class PA1664_API RobotApi {
 public:
  explicit RobotApi(Config config = {});
  ~RobotApi();

  RobotApi(const RobotApi&) = delete;
  RobotApi& operator=(const RobotApi&) = delete;

  const Config& config() const;
  // 用 ini 里的限幅/几何对齐本机 Config（不会下发到主机）。
  void applyDriveConfig(const DriveConfig& drive);

  // ---- 会话 -------------------------------------------------------------
  // start() 立即返回，建链在后台完成；断线由内部自动重连，无需调用方干预。
  void start();
  void stop();  // 主动断链（同时清零运动指令）
  bool isOnline() const;
  bool mediaReady() const;
  bool bulkReady() const;
  std::string statusText() const;  // 最近一次链路状态描述（UTF-8）

  // ---- 底盘状态机 -------------------------------------------------------
  // 连接底盘：下发 robot.connect（不带 settings），小车端沿用自己 ini 里
  // 已保存的驱动配置。首次使用必须先在工作台里配置并保存过一次底盘参数。
  Result connectChassis();
  Result disconnectChassis();
  Result enable(bool enabled = true);  // robot.enable：使能 / 撤销使能
  Result emergencyStop();              // 双通道急停（UDP 锁存 + TCP 请求）
  Result reset();                      // robot.reset：清故障、重启控制环
  Result clearAlarm();                 // robot.clearAlarm：清报警位
  Result requestSnapshot();            // robot.snapshot：结果走响应回调
  // 从主机拉取底盘配置：robot.snapshot 的 settings.iniBase64 解码后解析，
  // 结果经 setDriveConfigHandler() 回调返回，不依赖本地 ini。
  Result requestDriveConfig();

  // 一条命令拿到控制权：robot.connect → (主机应答后 1.2 s) robot.enable。
  // 1.2 s 是主机 armingTimeout（默认 1 s）加余量；需要精确时序时改用
  // connectChassis() + enable() 并观察 robot.stateChanged 事件。
  Result takeControl();

  // ---- 运动（持续指令，直到 stopMotion / 急停 / 断链） -------------------
  // speedRatio ∈ [0, 1]，线性映射到 config().maxLinearSpeedMps。
  Result forward(double speedRatio = 1.0);
  Result backward(double speedRatio = 1.0);
  Result turnLeft(double speedRatio = 1.0);   // 原地左转（角速度为负）
  Result turnRight(double speedRatio = 1.0);  // 原地右转（角速度为正）
  // 精确速度控制，单位 m/s 与 rad/s；正值角速度 = 右转（与差速混合器一致）。
  Result moveVelocity(double linearMps, double angularRadps);
  Result stopMotion();
  // robot.motion.stop：让主机侧也清零运动指令（UDP 零速包之外的兜底通道）。
  Result stopMotionOnHost();  // 立即清零并发出最后一个零速包

  // ---- 自动纠偏（激光焊缝跟踪）------------------------------------------
  // settingsJson 为 LaserCorrectionSettings 字段子集，空对象表示全部用主机
  // 默认值。常用键：targetSpeedMps / segmentLengthM / proportionalGain /
  // derivativeGain / imageTimeoutMs / maximumCurvatureRadPerM。
  // 前置条件：底盘已使能（Enabled）+ 激光相机已连接，否则主机返回
  // drive_not_enabled / laser_not_connected。
  Result startAutoCorrection(const std::string& settingsJson = "{}");
  Result stopAutoCorrection();
  Result pauseAutoCorrection(bool paused = true);
  Result requestAutoCorrectionStatus();

  // ---- 夹爪电机（X/Y/Z 轴，每轴 0..N 台）--------------------------------
  // percent ∈ [-100, 100]，正负对应电机配置里的 sign。
  Result clampSetSpeed(int axis, int index, int percent);
  Result clampStopMotor(int axis, int index);
  Result clampStop();  // 全部电机立即停
  // 位置控制/反馈仅部分协议支持（TT CANopen / MWD / ZDT 视固件而定）。
  Result clampMoveToPosition(int axis, int index, double degrees);
  Result clampRequestPosition(int axis, int index);
  Result clampRequestPositions();
  // 夹爪总线：调速前必须先 connect（沿用主机已保存的夹爪配置）。
  Result clampConnect();
  Result clampDisconnect();
  Result requestClampStatus();
  Result requestClampSettings();
  // 便捷入口：x1/x2 属于 X 轴（axis 0），z1/z2 属于 Z 轴（axis 2）。
  Result x1Motor(int percent) { return clampSetSpeed(0, 0, percent); }
  Result x2Motor(int percent) { return clampSetSpeed(0, 1, percent); }
  Result z1Motor(int percent) { return clampSetSpeed(2, 0, percent); }
  Result z2Motor(int percent) { return clampSetSpeed(2, 1, percent); }

  // ---- 摄像头 -----------------------------------------------------------
  // slot：0 = 摄像头1，1 = 摄像头2。USB 摄像头需要 deviceIndex（0 起）。
  Result connectCamera(int slot, int deviceIndex, int fps = 15,
                       bool flipHorizontal = false, bool flipVertical = false);
  Result connectNetworkCamera(int slot, const std::string& url,
                              bool flipHorizontal = false,
                              bool flipVertical = false);
  Result disconnectCamera(int slot);
  Result disconnectAllCameras();
  Result requestCameraStatus();
  Result scanCameras();  // robot.camera.scan：扫描主机 USB 摄像头（结果走事件）
  Result setCameraDisplaySize(int slot, int width, int height);

  // ---- 超声 -------------------------------------------------------------
  Result ultrasoundConnect(const std::string& address = "127.0.0.1",
                           int deviceId = 0);
  Result ultrasoundDisconnect();
  Result ultrasoundStartAcquisition();
  Result ultrasoundStopAcquisition();
  Result requestUltrasoundStatus();
  // 参数写入会让 SDK 重新配置硬件：先停采 → 写参数 → 本函数恢复采集。
  Result ultrasoundParametersSettled(bool resumeCapture = true);
  // 一次性读取全部参数（values 的键是 "方法名:base64(args)"，见文档）。
  Result ultrasoundParameterSnapshot();
  // 单参数读：method 形如 "getGain"，argsJson 形如 "[0]"。
  Result ultrasoundGetParameter(const std::string& method,
                                const std::string& argsJson = "[0]");
  // 单参数写 + 回读。setMethod 形如 "setGain"；readbackMethod 留空时按
  // set→get 自动推导（setGain → getGain），参数原样复用。
  Result ultrasoundSetParameter(const std::string& setMethod,
                                const std::string& argsJson = "[0]",
                                const std::string& readbackMethod = std::string(),
                                const std::string& readbackArgsJson =
                                    std::string());
  // 批量写：changesJson 为
  // [{"method":"setGain","args":[0,40],"readbackMethod":"getGain",
  //   "readbackArgs":[0]}, ...]
  Result ultrasoundApplyParameters(const std::string& changesJson);
  // 配置解码工作组。不配置则主机不会解出 A 扫帧。
  Result configureUltrasoundDecoder(const UltrasoundDecoderConfig& config);

  // ---- 超声预设 / 扫查精度 / 编码器标定（主机端持久化）------------------
  Result requestUltrasoundPresets();          // ultrasound.presets.list
  Result switchUltrasoundPreset(int presetId);  // 切换工况（会重配硬件参数）
  Result captureUltrasoundPreset(int presetId); // 把当前硬件参数存成预设
  Result requestUltrasoundScanPrecision();    // ultrasound.scan.precision.get
  Result setUltrasoundScanPrecision(int presetId, double mmPerCell);
  Result requestUltrasoundEncoderCalibration();  // encoder.calibration.get
  Result setUltrasoundEncoderCalibration(const std::string& json);
  Result ultrasoundResetEncoder();            // encoder.reset
  Result ultrasoundRefreshGeometry();         // geometry.refresh

  // ---- 本地配置镜像（操作端记录）----------------------------------------
  // 连上主机后自动执行：robot.snapshot → 缓存底盘 iniBase64；
  // ultrasound.status → 缓存几何并自动下发解码器；
  // ultrasound.parameters.snapshot → 缓存 84 项参数。
  // 之后每次启动都能读到上次的配置，不需要重新设置。
  void setProfileOptions(const ProfileOptions& options);
  ProfileOptions profileOptions() const;
  std::string profilePath() const;
  bool hasDriveProfile() const;
  bool hasUltrasoundProfile() const;
  Result reloadProfile();            // 从本地文件重新读入内存
  Result saveProfile();              // 立即落盘
  Result applyDecoderProfile();      // 用手上/保存的解码器配置下发
  // 把缓存的底盘配置下发给主机。串口/PCAN 端口由主机保留，不会被覆盖。
  Result applyDriveProfileToHost();

  // ---- 外围设备（激光相机 / IMU / 串口）---------------------------------
  // 激光相机是启动自动纠偏的前置条件；IMU 与编码器一起算扫查轨迹。
  Result scanLaserDevices();                        // robot.laser.scan
  Result connectLaser(const std::string& serialNumber);  // robot.laser.connect
  Result disconnectLaser();                         // robot.laser.disconnect
  Result requestLaserStatus();                      // robot.laser.status
  Result connectImu(const std::string& port, int baudRate,
                    int outputDivider = 10);        // robot.imu.connect
  Result disconnectImu();                           // robot.imu.disconnect
  Result requestImuStatus();                        // robot.imu.status
  Result requestPeripheralsSnapshot();               // robot.peripherals.snapshot
  Result requestSerialPorts();                       // robot.peripherals.serialPorts

  // ---- 通用逃生口 -------------------------------------------------------
  // 直接下发任意已支持的 operation（robot.clamp.* / robot.imu.* /
  // robot.laser.* / ultrasound.* 等），dataJson 为 JSON 对象文本。
  // 返回请求号，0 表示未发送（原因同时通过响应回调给出）。
  std::uint64_t request(const std::string& operation,
                        const std::string& dataJson = "{}");

  // ---- 回调（均可在构造后任意时刻设置，传空函数对象即取消） -------------
  void setConnectionHandler(
      std::function<void(bool online, const std::string& detail)> handler);
  void setStatusHandler(std::function<void(const RobotStatus&)> handler);
  // operation：请求名；ok/errorCode/errorText：主机响应；dataJson：响应 data 的 JSON 文本。
  void setResponseHandler(
      std::function<void(const std::string& operation, bool ok,
                         const std::string& errorCode,
                         const std::string& errorText,
                         const std::string& dataJson)> handler);
  // 主机主动上报事件（robot.stateChanged / robot.clamp.position / robot.log ...）。
  void setEventHandler(
      std::function<void(const std::string& event, const std::string& dataJson)>
          handler);

  // requestDriveConfig() 的应答；loaded=false 表示主机未返回有效配置。
  void setDriveConfigHandler(std::function<void(const DriveConfig&)> handler);
  void setCameraFrameHandler(std::function<void(const CameraFrame&)> handler);
  void setUltrasoundFrameHandler(
      std::function<void(const UltrasoundAScanFrame&)> handler);
  void setCorrectionStatusHandler(
      std::function<void(const CorrectionStatus&)> handler);
  void setUltrasoundStatusHandler(
      std::function<void(const UltrasoundStatus&)> handler);
  void setLogHandler(std::function<void(LogLevel, const std::string&)> handler);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

}  // namespace pa1664
