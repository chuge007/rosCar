# 车侧专网 IP 规划（定案）

> 最后更新：2026-11-19
>
> 状态：**已于 2026-11-19 现场实施生效**。两端口地址、Workbench 配置与默认值、脚本与文档均已按本方案更新；SSH 已验证 `a@192.168.66.10` 可用。下文第 8、9 节保留作为复核与回滚依据。

---

## 1. 定案地址

| 角色 | 地址 | 掩码 | 网关 | DNS | 原地址 |
|------|------|------|------|-----|--------|
| 车侧专网 | `192.168.66.0/24` | 255.255.255.0 | 无 | 无 | `192.168.1.0/24` |
| 小车端（`PA1664RobotHost`） | `192.168.66.10` | 24 | 不设 | 不设 | `192.168.1.20` |
| 激光轮廓仪（`DB2666828`，MV-TD240-2D） | `192.168.66.11` | 24 | **留空（0.0.0.0）** | 不设 | `192.168.1.25` |
| 操作台（`PA1664Workbench`） | `192.168.66.20` | 24 | 不设 | 不设 | `192.168.1.100`（现场触屏一体机）/ `192.168.110.40`（本工作区机器，DHCP） |

海康客户端「修改 IP 地址」对话框提示的可填范围是 `192.168.66.1 - 192.168.66.254`；`.1` 留给交换机/路由器管理口，`.10/.11/.20` 已被占用，其余按需分配。

端口不变：控制 `43100`、实时 `43101`、媒体 `43102`、批量 `43103`、重启管理 `43104`。

工作台实际连接 `192.168.66.10:43100`。

---

## 2. 为什么换掉 192.168.1.20

| 风险 | 事实依据 | 后果 |
|------|----------|------|
| 撞路由器默认网段 | 车端「以太网」原配置默认网关为 `192.168.1.1`（OpenWrt），同网段还有 `192.168.1.100`、`192.168.1.25`（激光轮廓仪 `DB2666828`） | `192.168.1.0/24` 是消费级/办公路由器最常见出厂段，DHCP 池常覆盖 `.100`–`.199`；`.20` 是否被池覆盖取决于这台路由器，换设备、恢复出厂、临时接一台笔记本都可能撞上 |
| 与办公网共用广播域 | 操作台曾出现 `192.168.110.40/24`（DHCP）、`192.168.1.100` 等多种地址 | 操作台网卡随现场随意配置，一旦与车端同段就可能自己撞自己：本机取到 `.20` 时程序连的就是本机 |
| 相机段与主链路混用史 | `bin\logs\robot_net_diag.txt`（2026-11-16 14:43）显示当时 `192.168.1.20/24`、`192.168.1.180/24`、`192.168.1.181/24` 三张 Intel I226-V 同在 `192.168.1.0/24`；当日已拆为 1/2/3 三段，相机网卡改为 `192.168.2.180`、`192.168.3.181` | 同段多网卡会走弱主机接收、ARP 应答来源不确定，症状与 IP 冲突难以区分。该问题 11-16 已修，**本次切换不要再动相机网卡** |

`192.168.66.0/24` 不是任何常见路由器的默认段，也不会被办公网 DHCP 覆盖。专网只留车与操作台两个地址后，广播域内的冲突面基本消除。

---

## 3. 物理连接

```
        同一二层广播域（可继续用现有 OpenWrt 的 LAN 口）
                 |                                  |
           车端「以太网」                      操作台专网口
           192.168.66.10                      192.168.66.20
           MAC 00-90-27-FA-72-D3
```

- 两端必须处在**同一个二层广播域**（同一台交换机、同一 VLAN）。这是「不设网关也能互通」的唯一前提；若车与操作台跨 VLAN 或跨路由，必须给一端补网关或加静态路由，否则 `ping` 直接报 `DestinationHostUnreachable`。
- 可以继续沿用现有 OpenWrt 路由器（它自身 LAN 地址留在 `192.168.1.1` 不影响二层转发），但**两端网卡都不要把网关指向 `192.168.1.1`**：路由器不在 `192.168.66.0/24` 内，会把专网流量甩到错误出口，表现为「地址看着对但就是不通」。
- 若要彻底隔离，把 OpenWrt 换成独立小交换机。专网不要上联办公网，否则 DHCP 与其他终端会重新进入该广播域。
- 车端另外两张网卡是**相机点对点链路，本次一律不动**，其网关必须保持留空。

### 车端网卡识别（切换前用 `Get-NetAdapter` 按 MAC 复核）

| 接口别名 | 网卡 | MAC | 当前地址 | 本次处置 |
|----------|------|-----|----------|----------|
| `以太网` | Intel I226-V | `00-90-27-FA-72-D3` | `192.168.1.20/24`，网关 `192.168.1.1` | 改为 `192.168.66.10/24`，去掉网关 |
| `以太网 2` | Intel I226-V #2 | `00-90-27-FA-72-D5` | `192.168.2.180/24`（相机 1） | **不动** |
| `以太网 3` | Intel I226-V #3 | `00-90-27-FA-72-D4` | `192.168.3.181/24`（相机 2） | **不动** |

> 现场若网卡别名与上表不一致，先执行 `Get-NetAdapter | Format-Table Name,MacAddress,Status` 核对 MAC 再改，不要按别名盲改。**切勿误删 `以太网 2/3` 的地址**，那会直接切断两路相机。

---

## 4. 车端配置

**注意：改网卡会中断 SSH 会话。** 必须在车端本地键盘鼠标操作，或先把命令写成脚本再由本地执行；不要在 SSH 会话里直接跑。

车端（管理员 PowerShell）。**只改 `以太网` 这一张，`以太网 2/3` 是相机链路，一律不动**：

```powershell
# 0) 核对网卡，确认别名与 MAC 对应
Get-NetAdapter | Format-Table Name,MacAddress,LinkSpeed,Status -AutoSize

# 1) 只改主链路网卡：清旧地址与默认网关，写专网地址
Remove-NetIPAddress -InterfaceAlias '以太网' -AddressFamily IPv4 -Confirm:$false
Remove-NetRoute     -InterfaceAlias '以太网' -DestinationPrefix '0.0.0.0/0' -Confirm:$false -ErrorAction SilentlyContinue
New-NetIPAddress    -InterfaceAlias '以太网' -IPAddress 192.168.66.10 -PrefixLength 24
Set-DnsClientServerAddress -InterfaceAlias '以太网' -ResetServerAddresses

# 2) 校验：以太网 应为 192.168.66.10；以太网 2/3 必须仍是 192.168.2.180 / 192.168.3.181
Get-NetIPAddress -AddressFamily IPv4 | Format-Table InterfaceAlias,IPAddress,PrefixLength,PrefixOrigin -AutoSize

# 3) 校验：不应存在指向 192.168.1.1 的默认路由
Get-NetRoute -AddressFamily IPv4 | Where-Object DestinationPrefix -eq '0.0.0.0/0'
```

车端 Host 程序**不需要改配置**：`startup_deploy/robot_host/config/host.psd1` 的 `ListenAddress = '0.0.0.0'` 监听全部网卡，换 IP 后照常工作。

---

## 5. 操作台配置

操作台要腾出一张网卡做专网口，办公网改走 WLAN 或另插网卡。接口别名因机器而异，**按当前地址识别，不要照抄下面的 `以太网 3`**（本工作区所在机器为 `以太网 3`，地址 `192.168.110.40`，DHCP；现场触屏一体机上通常是别的别名，可能是 `192.168.1.100` 静态）。

```powershell
# 0) 确认要改哪张网卡（按 MAC / 当前地址识别，不要只认别名）
Get-NetAdapter | Format-Table Name,MacAddress,LinkSpeed,Status -AutoSize
Get-NetIPAddress -AddressFamily IPv4 | Format-Table InterfaceAlias,IPAddress,PrefixLength,PrefixOrigin -AutoSize

# 1) 该网卡改为专网地址，去掉 DHCP 与默认网关（别名按上一步结果替换）
Remove-NetIPAddress -InterfaceAlias '以太网 3' -AddressFamily IPv4 -Confirm:$false
Remove-NetRoute     -InterfaceAlias '以太网 3' -DestinationPrefix '0.0.0.0/0' -Confirm:$false -ErrorAction SilentlyContinue
New-NetIPAddress    -InterfaceAlias '以太网 3' -IPAddress 192.168.66.20 -PrefixLength 24
Set-DnsClientServerAddress -InterfaceAlias '以太网 3' -ResetServerAddresses

# 2) 校验
Get-NetIPAddress -AddressFamily IPv4 | Format-Table InterfaceAlias,IPAddress,PrefixLength,PrefixOrigin -AutoSize
```

### 5.1 清掉软件记忆的旧地址（必须做）

`PA1664Workbench` 启动时按「命令行 `--remote` → 注册表 `remote/endpoint` → `remote/lastRemoteEndpoint` → 硬编码兜底」的顺序取地址，注册表里的旧值优先级高于兜底值。不清掉，软件会一直去连 `192.168.1.20`。

```powershell
# 查看当前记忆
Get-ChildItem 'HKCU:\Software\PA1664\PA1664Workbench' -Recurse

# 清除远端地址记忆
Remove-Item 'HKCU:\Software\PA1664\PA1664Workbench\remote' -Recurse -Force -ErrorAction SilentlyContinue
```

或者不改注册表，直接用带参数方式启动，程序会把新地址写回：

```powershell
start "" bin\PA1664Workbench.exe --remote 192.168.66.10:43100
```

---

## 6. 相机与激光轮廓仪

相机早已从主链路独立出去，维持在两条点对点链路上；激光轮廓仪跟随主链路进 `192.168.66.0/24`。

| 设备 | 网段 | 车端网卡 | 车端地址 | 相机地址 |
|------|------|----------|----------|----------|
| 相机 1 | `192.168.2.0/24` | `以太网 2` | `192.168.2.180` | `192.168.2.104` |
| 相机 2 | `192.168.3.0/24` | `以太网 3` | `192.168.3.181` | `192.168.3.104` |

- 正常远程模式下，相机流由车端 Host 采集后经媒体通道 `43102` 转发给 Workbench，**操作台不需要直连相机**，主链路换段不影响画面。
- 仅当现场改用「Workbench 直接拉 RTSP」模式时，才需要操作台到 2/3 段的通路。核对 `bin\logs\robot_console.log` 里 `[PANEL] [外设] USB 摄像头` 的地址来源与 `startup_deploy/operator_workbench/config/drive_settings.ini` 后再决定，不要凭配置文件里的旧地址直接加路由。
- 相机是雄迈（XiongMai / XMEye）方案，改地址只能用小车端的 `VMS Pro`（设备列表 → 修改 IP），ONVIF 与大华 ToolBox 都认不出。相机不属于本次改动范围。
- 两条点对点链路的网关必须留空，否则回包走网关会连不上。本次操作不得触碰这两张网卡的地址与路由。

### 6.1 激光轮廓仪（`DB2666828` / MV-TD240-2D）

| 项目 | 值 |
|------|-----|
| 地址 | `192.168.66.11` |
| 掩码 | `255.255.255.0` |
| 网关 | 留空（对话框自动填的 `192.168.66.254` 要删掉，该地址不存在） |
| 连接位置 | 与车端 `192.168.66.10` 同一台交换机 |
| 原地址 | `192.168.1.25` |

**必须同段的原因**：激光走海康 SDK 的广播发现（`MV3D_LP_GetDeviceNumber` → `Driver::enumerateDevices()`），广播不跨路由；`RobotSensorController::connectCamera()` 在按序列号连接前还会强制 `scanCamera()`，扫不到直接放弃。相机不同段能通是因为程序里写死了 RTSP 的 IP、车端又各有一张网卡落在 2/3 段，本质仍是同段点对点。SDK 虽提供 `connectByIp`，但当前代码未使用，所以不走跨段方案。

**改法**：用海康 MVS / Mv3dLp 客户端改。客户端要先能访问旧地址 `192.168.1.25`，即临时把操作电脑配到 `192.168.1.x`；改完再把电脑改回 `192.168.66.20`。

**改完必须重启车端 Host**，清掉 SDK 的旧设备缓存：

```powershell
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL a@192.168.66.10 "chcp 65001 >nul && taskkill /F /IM PA1664RobotHost.exe"
# 删除 app\robot_host.lock 后，用 WMI 启动（见 .codebuddy\start_remote_host.ps1）
```

**验证**：

```powershell
# 1) 车端邻居表应出现 66.11
ssh a@192.168.66.10 "arp -a" | Select-String '192.168.66'

# 2) 车端日志应出现新 IP（旧的是 192.168.1.25）
#    C:\Users\a\Desktop\robot_host\app\logs\robot_host.log 里搜 CAMERA.DISCOVERY
#    event=device_found serial=DB2666828 model=MV-TD240-2D ip=192.168.66.11 host=192.168.66.10

# 3) 媒体统计里 laser_pts 不再恒为 0
#    MEDIA.TX ... laser_img=0 laser_pts=<非0> ...
```

**2026-11-19 实测记录**（车端 `app\logs\camera_connection.log`）：

```
15:52:32  event=device_found serial=DB2666828 model=MV-TD240-2D ip=192.168.1.25 host=192.168.66.10
16:06:40  event=device_found serial=DB2666828 model=MV-TD240-2D ip=192.168.66.11 host=192.168.66.10
```

迁移后车端 ARP 出现 `192.168.66.11 → 34-bd-20-a9-44-ae`；Workbench 媒体统计 `点云=276/5s`、`相机1=40`、`相机2=50`、`解码失败=0`，激光轮廓数据正常回流。

---

## 7. 项目内需要同步的位置

下表是 **2026-11-19 已完成的实施记录**：左列为切换前旧值，右列为已落地的新值。左列仅作回滚参照，不要照着改回去。

| 文件 | 切换前旧值 | 已改为 | 是否重编 |
|------|--------|--------|----------|
| `startup_deploy/operator_workbench/config/workbench.psd1` | `Host = '192.168.1.20'` | `Host = '192.168.66.10'` | 否，改完即生效 |
| `src/main.cpp` 第 344 行附近 | `QStringLiteral("192.168.1.20:43100")` | `QStringLiteral("192.168.66.10:43100")` | 是，`build_workbench.bat` |
| `start_workbench_robot.bat` 第 9–10 行 | `--remote 192.168.1.20:43100` | `--remote 192.168.66.10:43100` | 否 |
| `check_link.ps1` 第 2 行 | `$target = '192.168.1.20'` | `192.168.66.10` | 否 |
| `watch_link.ps1` 第 2 行 | `[string]$Target = '192.168.1.20'` | `192.168.66.10` | 否 |
| `robot_comm_test/main.cpp` 第 136、1060 行 | `192.168.1.20` | `192.168.66.10` | 是，测试工具单独编译 |
| `robot_comm_test/run_test.bat` 第 5 行 | `-H 192.168.1.20` | `-H 192.168.66.10` | 否 |
| `robot_comm_test/run_connect.bat` 第 14 行 | `-H 192.168.1.20` | `-H 192.168.66.10` | 否 |
| `robot_comm_test/run_check.bat` 第 24、26、28 行 | `-H 192.168.1.20` | `-H 192.168.66.10` | 否 |
| `docs/远程调试与部署指南.md` | 全部 `ssh a@192.168.1.20` | `ssh a@192.168.66.10` | 否 |
| `AGENTS.md`、`.codebuddy/rules/*.mdc` 中的远程部署地址 | `a@192.168.1.20` | `a@192.168.66.10` | 否 |
| `api/include/pa1664_robot_api.h`、`api/小车api/include/pa1664_robot_api.h`、`api/examples/sdk/include/pa1664_robot_api.h` | `Config::host = "192.168.1.20"` | `"192.168.66.10"` | SDK 使用者按需重编 |

不需要改：`startup_deploy/robot_host/config/host.psd1`（`0.0.0.0`）、`src/common/protocol/protocol_types.h` 里各端口常量、`hasSameSubnetLocalAddress()` 等同网段判断逻辑（运行时按网卡实时枚举，不写死网段）。

---

## 8. 切换步骤（按顺序执行）

1. 车端本地改网卡（第 4 节），用 `ipconfig` 与 `Get-NetIPAddress` 校验地址生效，`ping 192.168.66.10` 自身通。
2. 操作台改专网口地址（第 5 节），接好与车端同一交换机的连线。
3. 操作台验证连通性（第 9 节前两条）。不通先解决网络，不要动软件。
4. 改 `workbench.psd1` 的 `Host` 为 `192.168.66.10`。
5. 清掉注册表 `remote` 子键，或改用 `--remote 192.168.66.10:43100` 启动一次。
6. 启动车端 `StartRobotHost.ps1`（本地或 SSH 远程），确认 `robot_host.lock` 已清理、进程存活。
7. 启动 `PA1664Workbench`，看状态栏。
8. 按需改代码与脚本里的默认值（第 7 节），重编 Workbench 只是为了让「无配置时」也落到新地址，不影响本次连通。

---

## 9. 验证

操作台执行：

```powershell
# 网络层
ping 192.168.66.10
Test-NetConnection 192.168.66.10 -Port 43100

# 四个通道
foreach ($p in 43100,43101,43102,43103) { Test-NetConnection 192.168.66.10 -Port $p -InformationLevel Quiet }

# SSH 换地址后
ssh a@192.168.66.10 "chcp 65001 >nul && tasklist | findstr PA1664RobotHost"
```

软件侧应看到：

- 状态栏 `● 主机在线 192.168.66.10:43100 / SDK 已连接`
- `bin\logs\robot_console.log` 出现 `REMOTE_MODE 192.168.66.10 43100`
- `LINK.CONTROL` 的 `event=link_status` 中 `attempts=1`、`timeouts=0`、`missed_pongs=0`；`rtt_ms` 应在 1 ms 以内（同交换机直连不该出现两位数，出现两位数说明走了路由或存在丢包）

---

## 10. 回滚

1. 车端「以太网」改回 `192.168.1.20/24`，网关指回 `192.168.1.1`（相机网卡本次未动，不需要恢复）。
2. 操作台专网口恢复原地址或改回 DHCP。
3. `workbench.psd1` 的 `Host` 改回 `192.168.1.20`。
4. 删除注册表 `HKCU:\Software\PA1664\PA1664Workbench\remote`。

回滚只涉及网卡与一个配置文件，代码无需回退。

---

## 11. 故障判定

| 现象 | 判定 | 处置 |
|------|------|------|
| `ping` 报 `DestinationHostUnreachable` | 本机没有专网地址或网线未接，路由把包甩给默认网关 | 检查网卡地址与交换机连线，不要查软件 |
| `ping` 通、`43100` 拒绝 | 车端 `PA1664RobotHost` 没起来，或防火墙拦了 43100–43103 | 查车端进程与 `app\logs\robot_host.log`，补防火墙放行规则 |
| 连上后反复断开，`attempts` 持续增长 | 广播域里出现同 IP 设备、或网卡反复复位 | 抓 `robot_console.log` 的 `state_change` 时间点，比对车端 PnP 事件 |
| 连的是旧地址 | 注册表 `remote` 子键残留 | 按 5.1 清除，或用 `--remote` 覆盖 |
| 相机两路黑屏 | 相机网卡地址/网关被误动，或车端 Host 未采集 | 按第 6 节核对 `以太网 2/3` 与车端 `drive_settings.ini`，相机在线性用 VMS Pro 确认 |

---

## 12. 相关文档

- `docs/远程调试与部署指南.md`：SSH 连接、日志位置、Host 部署流程
- `startup_deploy/网络连接方式详解.md`：连接方式与防火墙
- `startup_deploy/robot_host/config/host.psd1`：车端监听地址（`0.0.0.0`，无需改动）
