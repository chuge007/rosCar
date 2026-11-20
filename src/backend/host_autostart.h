#pragma once

#include <QString>

namespace pa1664::remote {

// “第一次运行即登记开机自启动，之后换 exe 也不用重装”的自愈逻辑。
//
// 现场的小车端是无人值守的：换版本靠 scp 覆盖 app\PA1664RobotHost.exe 完成。
// 只要自启动项被系统清理、被误删、部署目录被搬动，或者当初是人工点
// InstallAutoStart 装的，换完 exe 重启就再也起不来，必须有人上小车手动点一次。
// 这里让主机进程自己负责这件事：每次启动校验当前用户的启动文件夹快捷方式，
// 缺失或指向过期就重写。
//
// 快捷方式的目标按部署形态选择：
//   * 部署根目录存在 StartRobotHost.ps1 -> 指向它（读 config\host.psd1 的端口，
//     换 exe 完全无关）
//   * 否则 -> 指向 exe 本身（经 powershell 隐藏窗口拉起，同路径换 exe 同样有效）
enum class HostAutostartMode {
  Ensure,  // 默认：确保自启动项存在且指向当前部署
  Force,   // --autostart：忽略关闭标记与部署形态判断，强制写回
  Remove,  // --remove-autostart：删除自启动项并留下关闭标记
  Skip,    // --no-autostart：本次不处理
};

class HostAutostart final {
 public:
  // 永远不抛异常、不影响主服务：失败只通过 detail 返回原因，由调用方记日志。
  static bool apply(HostAutostartMode mode, QString* detail);
};

}  // namespace pa1664::remote
