# PA1664 远程通讯测试客户端 (Qt)

Qt5/Qt6 控制台程序,用 PA1664 二进制协议直连 `PA1664RobotHost.exe` 的控制
端口(默认 TCP 43100),验证握手和业务请求链路。

## 文件

- `main.cpp` — 测试客户端源码
- `robot_comm_test.pro` — 客户端 qmake 工程
- `test_server.cpp` / `test_server.pro` — 本地回环用的最小 ControlServer
- `release/robot_comm_test.exe`、`release/test_server.exe` — 编译产物

## 编译 (Windows MSVC)

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
D:\qt\5.12.4\msvc2017_64\bin\qmake.exe robot_comm_test.pro
nmake
```

`test_server.pro` 同理。

## 运行

```bat
robot_comm_test.exe -H 192.168.1.20 -p 43100
```

`-H` 指定主机(默认 `192.168.1.20`),`-p` 指定控制端口(默认 `43100`)。
日志写入同目录的 `robot_comm_test.log`。

本地回环自测:

```bat
test_server.exe
robot_comm_test.exe -H 127.0.0.1 -p 43100
```

## 测试项

1. TCP 连接建立与耗时
2. PA1664 Hello/HelloAck 握手(打印会话号、能力列表、UDP/TCP 端口)
3. `robot.snapshot` 业务请求及响应

握手成功后客户端每 250 ms 发一次 Ping 保活;主机在 2 × 1 s 内收不到任何
入站帧就会回收会话,因此不发心跳会被正常断开。

## 协议要点(排障记录)

50 字节小端头:`magic(0x50413136)`、`version(1)`、`type`、`flags`、
`sessionId`、`sequence`、`requestId`、`timestampUs`、`payloadBytes`、`crc32`。

- Hello 载荷必须包含 `protocolVersion`;缺失时主机按版本不兼容断开,
  客户端只会看到 “remote host closed the connection”。
- 帧序号从 1 开始,`sequence == 0` 或序号回退都会被主机判为非法。
- 握手前只有 Hello 合法(`sessionId` 必须为 0),业务请求需携带会话号。
