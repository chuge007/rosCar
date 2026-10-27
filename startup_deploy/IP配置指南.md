# IP 配置完全指南

## ✅ 已配置完成

所有配置文件已准备就绪，可以直接使用！

---

## 📁 配置文件位置

### Robot Host (机器人端)
**文件:** \startup_deploy\robot_host\config\host.psd1\

\\\powershell
@{
    ListenAddress = '0.0.0.0'      # 监听所有网络接口（无需修改）
    ControlPort   = 43100
    RealtimePort  = 43101
    MediaPort     = 43102
    BulkPort      = 43103
}
\\\

✅ **Robot Host 配置无需修改** - 已设置为监听所有网络接口

---

### Operator Workbench (操作员端)
**文件:** \startup_deploy\operator_workbench\config\workbench.psd1\

**当前默认配置:** 本地测试（127.0.0.1）

\\\powershell
@{
    Host        = '127.0.0.1'      # 机器人IP地址
    ControlPort = 43100
}
\\\

---

## 🎯 使用场景

### 场景 1: 本地测试（推荐用于开发）✅ 默认配置
**说明:** 在同一台电脑上同时运行 Robot Host 和 Workbench

**配置:** 使用默认的 \workbench.psd1\（已配置为 127.0.0.1）

**启动步骤:**
1. 双击运行 \obot_host\启动机器人主机.cmd\
2. 双击运行 \operator_workbench\启动操作员工作台.cmd\

---

### 场景 2: 局域网部署（两台电脑）

**说明:** Robot Host 在机器人PC，Workbench 在操作员PC

**配置步骤:**
1. 确定机器人PC的IP地址（例如：192.168.110.100）
2. 编辑 \operator_workbench\config\workbench.psd1\
3. 将 \Host = '127.0.0.1'\ 改为机器人的实际IP，例如：

\\\powershell
@{
    Host        = '192.168.110.100'  # 改为实际的机器人IP
    ControlPort = 43100
}
\\\

**备用配置文件:**（可直接重命名使用）
- \workbench-110.psd1\ - 当前网段 192.168.110.59
- \workbench-200.psd1\ - 备用网段 192.168.200.1
- \workbench-localhost.psd1\ - 本地测试备份

**启动步骤:**
1. 在机器人PC上运行 \obot_host\启动机器人主机.cmd\
2. 在操作员PC上运行 \operator_workbench\启动操作员工作台.cmd\

---

## 🖥️ 当前电脑网络信息

你的电脑有以下网络接口：
- **192.168.56.1** - VirtualBox 虚拟网络
- **192.168.110.59** - 以太网/WiFi（推荐用于机器人连接）
- **169.254.114.213** - 自动配置地址
- **192.168.200.1** - 备用网络接口

**推荐:** 如果机器人在 192.168.110.x 网段，使用该网段的IP

---

## 🔧 快速切换配置

### 切换到本地测试
\\\powershell
copy workbench-localhost.psd1 workbench.psd1
\\\

### 切换到 192.168.110.x 网段
\\\powershell
copy workbench-110.psd1 workbench.psd1
# 然后编辑 workbench.psd1，将 IP 改为机器人实际IP
\\\

### 切换到 192.168.200.x 网段
\\\powershell
copy workbench-200.psd1 workbench.psd1
# 然后编辑 workbench.psd1，将 IP 改为机器人实际IP
\\\

---

## 🛠️ 故障排查

### 无法连接到机器人

1. **检查网络连通性**
\\\powershell
ping 192.168.110.100  # 替换为实际机器人IP
\\\

2. **检查防火墙**
确保端口 43100-43103 未被防火墙阻止

3. **检查 Robot Host 是否运行**
在机器人PC上确认 PA1664RobotHost.exe 正在运行

4. **检查配置文件**
确认 workbench.psd1 中的 IP 地址与机器人PC的实际IP一致

---

## 📝 配置文件语法

\\\powershell
@{
    Host        = '192.168.x.x'  # 必须用单引号
    ControlPort = 43100          # 数字不加引号
}
\\\

---

**配置完成时间:** 2026-10-27 14:37:46
**状态:** ✅ 所有配置已就绪，默认为本地测试模式
