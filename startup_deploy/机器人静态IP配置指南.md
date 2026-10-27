# 机器人电脑静态IP配置指南

## 方法1: 通过 Windows 设置界面

1. 打开 **设置** > **网络和Internet** > **以太网**（或WiFi）
2. 点击网络连接 > **编辑**（IP设置）
3. 改为 **手动**
4. 配置如下：

   **示例配置（根据实际网络调整）：**
   - IP地址：192.168.1.100
   - 子网掩码：255.255.255.0
   - 网关：192.168.1.1（路由器IP）
   - DNS：192.168.1.1 或 8.8.8.8

---

## 方法2: 通过 PowerShell 命令

\\\powershell
# 1. 查看当前网络接口
Get-NetAdapter

# 2. 设置静态IP（替换参数）
New-NetIPAddress -InterfaceAlias "以太网" \
    -IPAddress 192.168.1.100 \
    -PrefixLength 24 \
    -DefaultGateway 192.168.1.1

# 3. 设置DNS
Set-DnsClientServerAddress -InterfaceAlias "以太网" \
    -ServerAddresses "192.168.1.1","8.8.8.8"
\\\

---

## 方法3: 路由器DHCP保留（推荐）

在路由器管理界面：
1. 找到 **DHCP设置** > **地址保留**
2. 绑定机器人电脑的MAC地址 → 固定IP
3. 机器人仍然使用DHCP，但每次获取相同IP

**优点：** 无需修改Windows配置，便于管理

---

## 推荐配置方案

| 场景 | 推荐方案 | IP设置 |
|------|---------|--------|
| 实验室/开发 | 静态IP | 手动设置（方法1或2）|
| 生产环境 | DHCP保留 | 路由器绑定MAC（方法3）|
| 临时测试 | DHCP | 无需配置，但记录当前IP |

---

## 配置后验证

\\\powershell
# 查看当前IP配置
ipconfig /all

# 查看网络接口详情
Get-NetIPAddress -InterfaceAlias "以太网"

# 测试网络连通性
ping 192.168.1.1  # 网关
ping 8.8.8.8      # 外网
\\\

---

## 配置完成后

在 **Workbench 电脑**上修改配置：

\\\powershell
# operator_workbench\config\workbench.psd1
@{
    Host = '192.168.1.100'  # 改为机器人的静态IP
    ControlPort = 43100
}
\\\

---

**总结：**
✅ Robot Host 配置文件：保持 0.0.0.0，无需修改
⚠️  机器人电脑网络：建议设置静态IP，便于 Workbench 连接
