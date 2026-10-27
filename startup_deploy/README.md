# PA1664 部署系统 - 快速使用指南

## 📁 位置
所有命令都在：startup_deploy 文件夹

---

## 🚀 使用方法

### 首次使用（必须）
`
双击：【完整构建】Full-Build.cmd
等待：5-10 分钟
`

### 日常开发（推荐）
`
双击：【快速构建】Quick-Build.cmd
等待：1-2 分钟（快 5 倍！）
`

---

## 📋 两种模式对比

| 命令文件 | 用途 | 耗时 | 更新内容 |
|---------|------|------|---------|
| **【完整构建】Full-Build.cmd** | 首次使用、依赖变化 | 5-10 分钟 | EXE + 所有 DLL |
| **【快速构建】Quick-Build.cmd** | 日常代码修改 | 1-2 分钟 | 仅 EXE 文件 |

---

## 📂 文件结构

`
startup_deploy\
│
├── 【完整构建】Full-Build.cmd      ← 完整构建（首次必用）
├── 【快速构建】Quick-Build.cmd     ← 快速构建（日常开发）
│
├── build-all.ps1                   ← 编译脚本
├── deploy-to-startup.ps1           ← 打包脚本
│
├── README.md                       ← 本文件
├── 使用说明.md                     ← 详细指南
│
├── robot_host\                     ← 小车电脑部署包
│   ├── app\                        ← 编译后生成
│   ├── config\                     ← 配置文件
│   ├── 启动小车程序.cmd
│   └── Install/UninstallAutoStart.ps1
│
└── operator_workbench\             ← 操作电脑部署包
    ├── app\                        ← 编译后生成
    ├── config\                     ← 配置文件
    ├── 启动操作界面.cmd
    └── Install/UninstallAutoStart.ps1
`

---

## 🔄 典型工作流

### 首次部署
1. 双击 【完整构建】Full-Build.cmd
2. 等待编译完成（5-10 分钟）
3. 复制 obot_host 到小车电脑
4. 复制 operator_workbench 到操作电脑
5. 配置 IP 地址
6. 测试运行

### 日常开发
1. 修改代码
2. 双击 【快速构建】Quick-Build.cmd
3. 等待 1-2 分钟
4. 仅更新 EXE 文件到对应电脑
5. 重启程序测试

---

## 🎯 何时使用哪个命令

### 使用完整构建的情况
- ✅ 第一次编译
- ✅ 添加了新的库或依赖
- ✅ 升级了 Qt 版本
- ✅ 升级了 OpenCV 版本
- ✅ 更新了厂商 SDK
- ✅ 准备发布版本

### 使用快速构建的情况
- ✅ 修改了 .cpp/.h 代码
- ✅ 修改了界面 UI
- ✅ 修改了业务逻辑
- ✅ 修复了 Bug
- ✅ 日常开发调试

---

## 💡 高级用法

### 命令行方式
`powershell
# 进入 startup_deploy 目录
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

## ⚠️ 常见问题

### Q1: 快速构建失败，提示 "app not found"
**原因**：从未运行过完整构建  
**解决**：双击 【完整构建】Full-Build.cmd

### Q2: 编译失败，找不到 qmake
**原因**：未安装 Qt 或路径不对  
**解决**：
- 安装 Qt 6.8.x MSVC 2022 64-bit
- 或在命令行指定路径：.\build-all.ps1 -QtPath "D:\Qt\6.8.3\msvc2022_64"

### Q3: 编译失败，找不到 nmake
**原因**：未配置 Visual Studio 环境  
**解决**：
1. 打开 "x64 Native Tools Command Prompt for VS 2022"
2. cd 到 startup_deploy 目录
3. 运行：powershell -ExecutionPolicy Bypass -File .\build-all.ps1

### Q4: 快速构建后程序无法运行
**原因**：依赖库可能已变化但未更新  
**解决**：运行完整构建重新收集依赖

---

## 📊 性能对比

### 完整构建
`
编译 EXE:       3-5 分钟
收集 Qt DLL:    1-2 分钟
收集 OpenCV:    30 秒
收集 SDK DLL:   30 秒
复制 plugins:   1 分钟
------------------------
总计:          5-10 分钟
`

### 快速构建
`
编译 EXE:       1-2 分钟
更新 EXE:       5 秒
------------------------
总计:          1-2 分钟 ⚡
`

**加速效果：约 5 倍！**

---

## 🎉 优势

- ⚡ **快速迭代**：日常开发只需 1-2 分钟
- 💾 **节省时间**：避免重复复制数百个 DLL
- 🎯 **简单明确**：两个命令，一目了然
- 🛡️ **安全可靠**：自动检查前置条件

---

**详细说明请查看：使用说明.md**
