# 当前IP配置说明

## 已部署的配置

### Robot Host (机器人端)
位置: startup_deploy\robot_host\config\host.psd1
\\\powershell
@{
    ListenAddress = '0.0.0.0'      # 监听所有网络接口
    ControlPort   = 43100
    RealtimePort  = 43101
    MediaPort     = 43102
    BulkPort      = 43103
}
\\\

### Operator Workbench (操作员端)
位置: startup_deploy\operator_workbench\config\workbench.psd1
\\\powershell
@{
    Host        = '192.168.50.20'  # 机器人IP地址
    ControlPort = 43100
}
\\\

## 使用说明

### 场景1: 本地测试（同一台电脑）
两个配置都**不需要修改**，使用 localhost 或 127.0.0.1：
- Robot Host 保持监听 0.0.0.0
- Workbench 改为: Host = '127.0.0.1'

### 场景2: 局域网部署（两台电脑）
1. 确定机器人PC的实际IP地址
2. 修改 operator_workbench\config\workbench.psd1 中的 Host 为机器人的实际IP
   - 如果机器人IP是 192.168.50.20，保持不变 ✓
   - 如果机器人IP是其他（如 192.168.1.100），修改为实际IP

### 场景3: 跨网段或远程连接
需要配置防火墙规则，允许端口 43100-43103

---

**当前电脑网络信息：**
