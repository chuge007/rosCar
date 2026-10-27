# PA1664 Robot Control System - Deployment Guide

## 项目概述

PA1664 爬壁机器人控制系统 - 上位机/下位机架构

- **下位机（小车端）**: PA1664RobotHost.exe - 后台服务，控制硬件
- **上位机（操作端）**: PA1664Workbench.exe - 图形界面，远程控制

---

## 🚀 快速开始

### Step 1: 编译打包（开发机）

**进入 startup_deploy 目录，选择编译模式：**

#### 首次使用（必须）
`
双击：【完整构建】Full-Build.cmd
等待：5-10 分钟
`

#### 日常开发（推荐）
`
双击：【快速构建】Quick-Build.cmd
等待：1-2 分钟（快 5 倍！）
`

### Step 2: 部署小车端

`
复制 startup_deploy\robot_host\ 到小车电脑
编辑 robot_host\config\host.psd1（可选）
双击 robot_host\启动小车程序.cmd
`

### Step 3: 部署控制端

`
复制 startup_deploy\operator_workbench\ 到操作电脑
编辑 operator_workbench\config\workbench.psd1 填写小车IP
双击 operator_workbench\启动操作界面.cmd
`

---

## 📁 项目结构

`
PA1664WorkbenchTmpSeparate\
│
├── PA1664RobotHost.pro           # 小车端项目（Qt）
├── PA1664Workbench.pro           # 控制端项目（Qt）
│
├── bin\                          # 编译输出目录
│   ├── PA1664RobotHost.exe
│   ├── PA1664Workbench.exe
│   └── *.dll
│
├── runtime\                      # 运行时依赖
│
├── README.md                     # 本文件
├── 超简单README.md               # 极简指南
│
└── startup_deploy\               # 🔥 部署包目录（所有命令都在这里）
    │
    ├── 【完整构建】Full-Build.cmd      # 完整构建命令
    ├── 【快速构建】Quick-Build.cmd     # 快速构建命令
    │
    ├── build-all.ps1                   # 编译脚本
    ├── deploy-to-startup.ps1           # 打包脚本
    │
    ├── README.md                       # 快速参考
    ├── 使用说明.md                     # 详细指南
    │
    ├── robot_host\               # 小车电脑部署包
    │   ├── app\                  # 编译后生成
    │   ├── config\
    │   ├── 启动小车程序.cmd
    │   └── Install/UninstallAutoStart.ps1
    │
    └── operator_workbench\       # 操作电脑部署包
        ├── app\                  # 编译后生成
        ├── config\
        ├── 启动操作界面.cmd
        └── Install/UninstallAutoStart.ps1
`

---

## 🔥 两种编译模式

### 完整构建（Full Build）
- **命令**: 双击 startup_deploy\【完整构建】Full-Build.cmd
- **用途**: 首次编译、依赖变化
- **时间**: 5-10 分钟
- **更新**: EXE + 所有 DLL、plugins

### 快速构建（Quick Build）⚡
- **命令**: 双击 startup_deploy\【快速构建】Quick-Build.cmd
- **用途**: 日常代码修改
- **时间**: 1-2 分钟
- **更新**: 仅 EXE 文件

**加速效果：约 5 倍！**

---

## 💻 命令行方式

`powershell
# 进入部署目录
cd startup_deploy

# 完整构建
.\build-all.ps1

# 快速构建
.\build-all.ps1 -QuickMode

# 指定 Qt 路径
.\build-all.ps1 -QtPath "D:\Qt\6.8.3\msvc2022_64"

# 强制重新收集依赖
.\build-all.ps1 -Force
`

---

## 📡 网络架构

### 通信通道（4 个端口）

| 端口 | 协议 | 用途 |
|------|------|------|
| 43100 | TCP | 命令控制 |
| 43101 | UDP | 实时数据 |
| 43102 | UDP | 媒体流 |
| 43103 | TCP | 批量数据 |

### 配置文件

**小车端** (obot_host\config\host.psd1):
`powershell
@{
    TcpCommandPort = 43100
    UdpRealtimePort = 43101
    UdpMediaPort = 43102
    TcpBulkPort = 43103
}
`

**控制端** (operator_workbench\config\workbench.psd1):
`powershell
@{
    RobotHostIP = "192.168.1.100"  # 小车IP
    TcpCommandPort = 43100
    UdpRealtimePort = 43101
    UdpMediaPort = 43102
    TcpBulkPort = 43103
}
`

---

## 🛠️ 开发环境要求

### 编译环境
- Qt 6.8.x (MSVC 2022 64-bit)
- Visual Studio 2022 (MSVC v143)
- CMake 或 qmake
- OpenCV 5.0
- PhaseLink SDK

### 运行环境
- Windows 10/11 64-bit
- .NET Framework 4.8+
- Visual C++ Redistributable 2022

---

## 📖 详细文档

- **极简指南**: 超简单README.md
- **快速参考**: startup_deploy\README.md
- **完整指南**: startup_deploy\使用说明.md
- **架构设计**: docs\REMOTE_CONTROL_ARCHITECTURE.md

---

## ⚙️ 开机自启动

### 安装（需管理员权限）
`powershell
# 小车端
cd robot_host
.\InstallAutoStart.ps1

# 控制端
cd operator_workbench
.\InstallAutoStart.ps1
`

### 卸载
`powershell
.\UninstallAutoStart.ps1
`

---

## 🐛 故障排查

### 1. 编译问题

#### 找不到 qmake
`powershell
# 指定 Qt 路径
cd startup_deploy
.\build-all.ps1 -QtPath "D:\Qt\6.8.3\msvc2022_64"
`

#### 找不到 nmake
`
打开 "x64 Native Tools Command Prompt for VS 2022"
cd startup_deploy
powershell -ExecutionPolicy Bypass -File .\build-all.ps1
`

#### 快速构建失败："app not found"
`
原因：首次必须运行完整构建
解决：双击【完整构建】Full-Build.cmd
`

### 2. 连接问题

- ✅ 小车程序是否运行
- ✅ IP 地址配置是否正确
- ✅ 防火墙是否允许端口 43100-43103
- ✅ 网络是否互通（ping 测试）

### 3. 日志位置

- 小车端: obot_host\app\logs\
- 控制端: operator_workbench\app\logs\

---

## 🎯 典型工作流

### 首次部署
1. 进入 startup_deploy 目录
2. 双击 【完整构建】Full-Build.cmd
3. 等待编译完成（5-10 分钟）
4. 复制 obot_host 到小车电脑
5. 复制 operator_workbench 到操作电脑
6. 配置 IP 地址
7. 测试运行

### 日常开发
1. 修改代码
2. 进入 startup_deploy 目录
3. 双击 【快速构建】Quick-Build.cmd
4. 等待 1-2 分钟
5. 仅更新 EXE 文件到对应电脑
6. 重启程序测试

### 依赖变化时
1. 进入 startup_deploy 目录
2. 双击 【完整构建】Full-Build.cmd
3. 重新复制整个文件夹到目标电脑

---

## 📞 技术支持

详细架构和设计文档请参考:
- docs\REMOTE_CONTROL_ARCHITECTURE.md
- startup_deploy\README.md
- startup_deploy\使用说明.md

---

**版本**: v2.0 - Separated Build Commands  
**最后更新**: 2024-10-27
