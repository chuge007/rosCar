#include "host_autostart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStringList>

#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace pa1664::remote {

#ifdef Q_OS_WIN

namespace {

constexpr char kShortcutName[] = "PA1664 Robot Host.lnk";
constexpr char kDisabledMarker[] = ".autostart-off";

QString startupFolder() {
  PWSTR raw = nullptr;
  if (FAILED(::SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr,
                                    &raw))) {
    return {};
  }
  const QString path = QString::fromWCharArray(raw);
  ::CoTaskMemFree(raw);
  return path;
}

// 以 SYSTEM 身份运行时（例如被注册成服务），启动文件夹指向的是 SYSTEM 的
// 隐藏配置目录，写进去对当前登录用户毫无意义，不如直接说明原因。
bool runningAsSystemAccount() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD size = 0;
  ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  bool isSystem = false;
  if (size > 0) {
    std::vector<unsigned char> buffer(size);
    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    if (::GetTokenInformation(token, TokenUser, user, size, &size)) {
      SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
      PSID systemSid = nullptr;
      if (::AllocateAndInitializeSid(&authority, 1, SECURITY_LOCAL_SYSTEM_RID, 0,
                                     0, 0, 0, 0, 0, 0, &systemSid)) {
        isSystem = ::EqualSid(user->User.Sid, systemSid) != FALSE;
        ::FreeSid(systemSid);
      }
    }
  }
  ::CloseHandle(token);
  return isSystem;
}

// 网络共享或可移动盘上的 exe 写进自启动只会变成一条开机报错。
bool isLocalFixedDrive(const QString& path) {
  const QString native = QDir::toNativeSeparators(path);
  if (native.startsWith(QStringLiteral("\\\\"))) return false;
  if (native.size() < 3) return false;
  const QString root = native.left(3);
  return ::GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_FIXED;
}

// 只有“像部署目录”的形态才自动登记，避免开发机 bin\ 下调试时被装上自启动。
bool looksLikeDeployment(const QString& appDir) {
  const QDir dir(appDir);
  if (QFileInfo::exists(dir.filePath(QStringLiteral("config/host.psd1")))) return true;
  const QString root = QFileInfo(appDir).absolutePath();
  if (QFileInfo::exists(QDir(root).filePath(QStringLiteral("StartRobotHost.ps1"))))
    return true;
  return dir.dirName().compare(QStringLiteral("robot_host"), Qt::CaseInsensitive) == 0;
}

struct ShortcutData {
  QString target;
  QString arguments;
  QString workingDirectory;
  bool valid = false;
};

ShortcutData readShortcut(const QString& path) {
  ShortcutData data;
  IShellLinkW* link = nullptr;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW,
                                reinterpret_cast<void**>(&link)))) {
    return data;
  }
  IPersistFile* file = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                     reinterpret_cast<void**>(&file)))) {
    if (SUCCEEDED(file->Load(reinterpret_cast<LPCWSTR>(path.utf16()), STGM_READ))) {
      wchar_t buffer[MAX_PATH * 4] = {};
      if (SUCCEEDED(link->GetPath(buffer, ARRAYSIZE(buffer), nullptr, SLGP_RAWPATH)))
        data.target = QString::fromWCharArray(buffer);
      if (SUCCEEDED(link->GetArguments(buffer, ARRAYSIZE(buffer))))
        data.arguments = QString::fromWCharArray(buffer);
      if (SUCCEEDED(link->GetWorkingDirectory(buffer, ARRAYSIZE(buffer))))
        data.workingDirectory = QString::fromWCharArray(buffer);
      data.valid = !data.target.isEmpty();
    }
    file->Release();
  }
  link->Release();
  return data;
}

bool writeShortcut(const QString& path, const QString& target,
                   const QString& arguments, const QString& workingDirectory,
                   const QString& iconPath) {
  IShellLinkW* link = nullptr;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW,
                                reinterpret_cast<void**>(&link)))) {
    return false;
  }
  link->SetPath(reinterpret_cast<LPCWSTR>(target.utf16()));
  link->SetArguments(reinterpret_cast<LPCWSTR>(arguments.utf16()));
  link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(workingDirectory.utf16()));
  link->SetDescription(L"Start the PA1664 robot host at user logon.");
  link->SetIconLocation(
      reinterpret_cast<LPCWSTR>((iconPath + QStringLiteral(",0")).utf16()), 0);

  bool ok = false;
  IPersistFile* file = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                     reinterpret_cast<void**>(&file)))) {
    ok = SUCCEEDED(file->Save(reinterpret_cast<LPCWSTR>(path.utf16()), TRUE));
    file->Release();
  }
  link->Release();
  return ok;
}

struct DesiredShortcut {
  QString target;
  QString arguments;
  QString workingDirectory;
  QString source;  // 日志用：指向 launcher 还是 exe
};

QString powershellPath() {
  const QString systemRoot = QProcessEnvironment::systemEnvironment()
                                 .value(QStringLiteral("SystemRoot"),
                                        QStringLiteral("C:\\Windows"));
  return QDir(systemRoot).filePath(
      QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
}

// 自启动需要沿用当前的监听地址与端口（现场可能改过），但绝不能带上一次
// 进程内重启留下的临时参数，否则每次重启都会多带一个已失效的 PID 等待。
QStringList persistentArguments() {
  QStringList args = QCoreApplication::arguments().mid(1);
  for (int i = args.size() - 1; i >= 0; --i) {
    if (args.at(i) == QStringLiteral("--restart-after-pid")) {
      args.removeAt(i);
      if (i < args.size()) args.removeAt(i);
    } else if (args.at(i).startsWith(QStringLiteral("--restart-after-pid=")) ||
               args.at(i).startsWith(QStringLiteral("--autostart"))) {
      args.removeAt(i);
    } else if (args.at(i) == QStringLiteral("--no-autostart") ||
               args.at(i) == QStringLiteral("--remove-autostart")) {
      args.removeAt(i);
    }
  }
  return args;
}

DesiredShortcut desiredShortcut() {
  DesiredShortcut desired;
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString rootDir = QFileInfo(appDir).absolutePath();
  const QString launcher = QDir(rootDir).filePath(QStringLiteral("StartRobotHost.ps1"));
  desired.target = powershellPath();
  const QString prefix = QStringLiteral(
      "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass "
      "-WindowStyle Hidden ");

  // launcher 脚本自己会去 app\PA1664RobotHost.exe 取程序，因此只有 exe 确实
  // 位于 app 子目录时才用它；包目录/裸目录直接指向 exe，避免 launcher 找不到文件。
  const bool exeUnderAppDir =
      QFileInfo(appDir).fileName().compare(QStringLiteral("app"),
                                           Qt::CaseInsensitive) == 0;
  if (exeUnderAppDir && QFileInfo::exists(launcher)) {
    desired.workingDirectory = rootDir;
    desired.arguments = prefix + QStringLiteral("-File \"%1\"").arg(
                                      QDir::toNativeSeparators(launcher));
    desired.source = launcher;
    return desired;
  }

  const QStringList args = persistentArguments();
  QString command = QStringLiteral("Start-Process -FilePath '%1'")
                        .arg(QDir::toNativeSeparators(
                            QCoreApplication::applicationFilePath()));
  if (!args.isEmpty())
    command += QStringLiteral(" -ArgumentList '%1'").arg(args.join(QLatin1Char(' ')));
  command += QStringLiteral(" -WorkingDirectory '%1' -WindowStyle Hidden")
                 .arg(QDir::toNativeSeparators(appDir));
  desired.workingDirectory = appDir;
  desired.arguments = prefix + QStringLiteral("-Command \"%1\"").arg(command);
  desired.source = QCoreApplication::applicationFilePath();
  return desired;
}

class ComScope final {
 public:
  ComScope() : result_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ComScope() {
    if (SUCCEEDED(result_)) ::CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  bool usable() const {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_;
};

QString normalized(const QString& path) {
  if (path.trimmed().isEmpty()) return {};
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

}  // namespace

#endif  // Q_OS_WIN

bool HostAutostart::apply(HostAutostartMode mode, QString* detail) {
  const auto report = [detail](bool result, const QString& text) {
    if (detail) *detail = text;
    return result;
  };

  if (mode == HostAutostartMode::Skip) return report(false, QStringLiteral("已跳过"));

#ifdef Q_OS_WIN
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString marker = QDir(appDir).filePath(QString::fromLatin1(kDisabledMarker));
  const QString folder = startupFolder();
  if (folder.isEmpty())
    return report(false, QStringLiteral("无法定位当前用户的启动文件夹"));
  const QString shortcut =
      QDir(folder).filePath(QString::fromLatin1(kShortcutName));

  if (mode == HostAutostartMode::Remove) {
    QFile markerFile(marker);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      markerFile.write(
          "PA1664RobotHost autostart disabled by --remove-autostart.\n"
          "Delete this file or start with --autostart to re-enable.\n");
      markerFile.close();
    }
    if (!QFileInfo::exists(shortcut)) {
      return report(true, QStringLiteral("自启动项本就不存在，已写入关闭标记 %1")
                              .arg(marker));
    }
    if (!QFile::remove(shortcut))
      return report(false, QStringLiteral("删除自启动快捷方式失败：%1").arg(shortcut));
    return report(true, QStringLiteral("已删除自启动快捷方式并写入关闭标记：%1")
                            .arg(shortcut));
  }

  const bool forced = (mode == HostAutostartMode::Force);
  if (!forced && QFileInfo::exists(marker)) {
    return report(false, QStringLiteral(
                            "检测到 %1，本次不登记自启动（--autostart 可覆盖）")
                            .arg(marker));
  }
  if (runningAsSystemAccount()) {
    return report(false, QStringLiteral(
                            "当前以 SYSTEM 身份运行，用户启动文件夹对本机登录无效"));
  }
  if (!isLocalFixedDrive(appDir)) {
    return report(false, QStringLiteral("程序不在本地固定磁盘上（%1），跳过自启动登记")
                            .arg(QDir::toNativeSeparators(appDir)));
  }
  if (!forced && !looksLikeDeployment(appDir)) {
    return report(false,
                  QStringLiteral("当前目录不像部署形态（无 config/host.psd1 或 "
                                 "StartRobotHost.ps1），跳过；--autostart 可强制"));
  }
  if (forced && QFileInfo::exists(marker)) QFile::remove(marker);

  ComScope com;
  if (!com.usable())
    return report(false, QStringLiteral("COM 初始化失败，无法写自启动快捷方式"));

  const DesiredShortcut desired = desiredShortcut();
  const ShortcutData existing =
      QFileInfo::exists(shortcut) ? readShortcut(shortcut) : ShortcutData{};
  if (existing.valid &&
      normalized(existing.target) == normalized(desired.target) &&
      existing.arguments.trimmed() == desired.arguments.trimmed() &&
      normalized(existing.workingDirectory) ==
          normalized(desired.workingDirectory)) {
    return report(true, QStringLiteral("自启动项已就绪：%1 -> %2")
                            .arg(shortcut, desired.source));
  }

  const bool existed = QFileInfo::exists(shortcut);
  if (!writeShortcut(shortcut, desired.target, desired.arguments,
                     desired.workingDirectory,
                     QCoreApplication::applicationFilePath())) {
    return report(false, QStringLiteral("写入自启动快捷方式失败：%1").arg(shortcut));
  }
  return report(true, QStringLiteral("%1自启动快捷方式：%2 -> %3")
                          .arg(existed ? QStringLiteral("已重写")
                                       : QStringLiteral("已创建"),
                               shortcut, desired.source));
#else
  return report(false, QStringLiteral("当前平台不支持自启动登记"));
#endif
}

}  // namespace pa1664::remote
