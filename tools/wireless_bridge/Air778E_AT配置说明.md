# Air780E 4G 模块 AT 指令配置说明

## 目标

将 Air780E 配置为 **TCP Client 透传模式**，连接到 Sakura Frp 公网隧道，数据双向透传。

## 前置条件

- Air780E 模块已通过 UART2 (PA2-TX / PA3-RX) 连接 STM32
- SIM 卡已插入，4G 信号正常
- Sakura Frp 隧道已创建并启动（见 README.md 第 1 步）
- 记下 Sakura Frp 分配的 **公网地址**（如 `xxx.natfrp.cloud`）和 **端口号**（如 `12345`）

## 配置方式

### 方式一：通过 STM32 FinSH 控制台发送 AT 指令

在 RT-Thread 控制台输入以下命令（利用已有的 `g4_send_text()` 函数）：

```
msh /> uart_recv_init        // 先启动 UART2 接收线程
```

然后通过 MSH 或自定义命令逐条发送 AT 指令到 Air780E。

### 方式二：用 USB-TTL 模块直接连接 Air780E 调试（推荐首次配置）

断开 Air780E 与 STM32 的连接，用 USB-TTL 串口模块直接连 Air780E：

```
USB-TTL TX  →  Air780E RX
USB-TTL RX  →  Air780E TX
USB-TTL GND →  Air780E GND
```

用串口助手（如 SSCOM、MobaXterm）发送 AT 指令，波特率 115200。

---

## AT 指令配置流程

> **注意：** `<CR>` = 回车，`<LF>` = 换行。每条指令以 `\r\n` 结尾。
> `OK` 表示模块返回成功。

### 第 1 步：检查模块通信

```
AT
```
**期望返回：** `OK`

### 第 2 步：检查 SIM 卡

```
AT+CPIN?
```
**期望返回：** `+CPIN: READY` + `OK`

### 第 3 步：检查网络注册

```
AT+CGATT?
```
**期望返回：** `+CGATT: 1` + `OK`

如果返回 `+CGATT: 0`，等待 10-30 秒后重试。

### 第 4 步：查询信号质量

```
AT+CSQ
```
**期望返回：** `+CSQ: <rssi>,<ber>` + `OK`

- rssi ≥ 15 表示信号良好
- rssi = 99 表示无法检测

### 第 5 步：激活 PDP 上下文

```
AT+CGACT=1,1
```
**期望返回：** `OK`

### 第 6 步：配置 TCP 连接参数

```
AT+CTCPConfig=0,1,1,1,1,300,7200,0
```

参数说明（Air778E TCP 配置）：
- 参数1: 0 = 配置索引
- 参数2: 1 = TCP Client 模式
- 参数3: 1 = 透传模式
- 参数4: 1 = 使用域名/IP
- 参数5: 1 = 自动重连
- 参数6: 300 = 重连间隔 300 秒
- 参数7: 7200 = 心跳间隔 7200 秒
- 参数8: 0 = 不使用 SSL

**期望返回：** `OK`

> **注意：** 不同固件版本的 AT 指令可能不同。
> 如果 `AT+CTCPConfig` 不识别，使用下面的标准方式：

### 第 6 步（替代）：使用标准 Socket AT 指令

```
AT+CIPCSGP=1,"CMNET"
```
**期望返回：** `OK`

```
AT+CIPSTART="TCP","<Sakura Frp公网地址>",<Sakura Frp端口>
```

**将 `<Sakura Frp公网地址>` 和 `<Sakura Frp端口>` 替换为你创建隧道时分配的实际地址。**

例如（你的实际地址）：
```
AT+CIPSTART="TCP","frp-oil.com",32762
```

**期望返回：**
```
CONNECT OK

CONNECT
```

### 第 7 步：进入透传模式

```
AT+CIPMODE=1
```
**期望返回：** `OK`

```
AT+CIPSEND
```
**期望返回：** `>`（进入透传模式）

此时 Air780E 进入**透传模式**，STM32 通过 UART2 发送的所有数据将直接通过 4G 网络发到 Sakura Frp 隧道，再转发到电脑的 TCP Server。

### 退出透传模式

发送 `+++`（前后各 1 秒静默），模块返回命令模式。

---

## 自动连接配置（上电自动拨号）

如果希望 Air778E 上电后自动执行上述配置：

```
AT+CIPCFG="AUTOCONNECT",1
```

或使用 Air778E 的启动脚本功能：

```
AT&V              // 查看当前配置
AT&W              // 保存配置到 Flash
AT&CIPNVM=1       // 保存为上电自动执行
```

---

## 数据流验证

配置完成后，验证数据链路：

```
STM32 (dtu_sender.c)          Air780E                    Sakura Frp              电脑
     |                          |                          |                      |
     |--UART2: {"type":"heartbeat","ts":123}\n-->          |                      |
     |                          |--4G TCP-->               |                      |
     |                          |                   TCP转发-->                     |
     |                          |                          |--------TCP:8090------>|
     |                          |                          |                      |--bridge.js
     |                          |                          |                      |--WS:8080-->网页
```

### 验证步骤

1. 确保 Sakura Frp 隧道已启动（启动器状态绿色"运行中"）
2. 确保 `bridge.js` 已运行（`node bridge.js`）
3. 确保 `cable-monitor.html` 已在浏览器打开
4. STM32 上电，自动启动舞动检测和 DTU 上报
5. 网页应显示：
   - 状态变为"已连接"
   - 每 5 秒收到温湿度数据
   - 每 10 秒收到心跳包
   - 每 ~3.2 秒收到舞动特征数据（如果传感器正常）

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| Air780E 返回 `ERROR` | AT 指令不支持 | 检查固件版本，使用替代指令 |
| `CONNECT FAIL` | 网络未就绪 | 等待 30 秒后重试 `AT+CIPSTART` |
| 隧道不通 | Sakura Frp 未启动 | 检查启动器状态，确认隧道已启动 |
| bridge.js 无连接 | Sakura Frp 转发端口不对 | 确认本地端口是 8090，与 bridge.js 一致 |
| 网页 WS 连接失败 | bridge.js 未启动 | 先启动 `node bridge.js` |
| 数据乱码 | 波特率不匹配 | 确认 Air780E 和 STM32 都用 115200 |
