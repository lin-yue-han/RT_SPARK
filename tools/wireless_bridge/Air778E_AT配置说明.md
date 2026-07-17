# 银尔达 Core-Y100M DTU 配置说明

## 重要：这不是标准 AT 指令模块

银尔达 Core-Y100M 运行的是**银尔达私有 DTU 透传固件**，使用 `config,set,tcp` 等私有命令，**不支持**标准 AT 指令（`AT`、`AT+CPIN?`、`AT+CIPSTART` 等全部无效）。

## 目标

将 DTU 配置为 **TCP Client 透传模式**，上电自动连接 `frp-oil.com:32762`，之后 STM32 通过 UART2 发送的纯 JSON 数据会自动透传到远端。

## 前置条件

- DTU 模块通过 UART2 (PA2-TX / PA3-RX) 连接 STM32
- SIM 卡已插入，4G 信号正常
- Sakura Frp 隧道已创建并启动（见 README.md）
- **USB-TTL 串口工具**（用于一次性配置 DTU）

## 配置方式

### 唯一方式：USB-TTL 串口工具直连 DTU

断开 DTU 与 STM32 的连接，用 USB-TTL 串口模块直接连 DTU：

```
USB-TTL TX  →  DTU RX
USB-TTL RX  →  DTU TX
USB-TTL GND →  DTU GND
```

用串口助手（SSCOM、MobaXterm 等）发送命令，**波特率 115200**。

---

## 配置命令

### 第 1 步：设置 TCP 透传参数

```
config,set,tcp,1,ttluart,0,0,,0,frp-oil.com,32762,0,,0,,0,0,0,0,0
```

参数说明（按位置）：

| 位置 | 参数 | 值 | 说明 |
|------|------|-----|------|
| 1 | 通道 | tcp | TCP 通道 |
| 2 | 通道号 | 1 | 通道 1 |
| 3 | 数据源 | ttluart | 串口透传（TTL UART） |
| 4 | 串口号 | 0 | UART0 |
| 5 | 串口波特率 | 0 | 默认（与模块当前波特率一致） |
| 6 | 数据位 | (空) | 默认 8 |
| 7 | 校验位 | (空) | 默认无校验 |
| 8 | 停止位 | 0 | 默认 1 |
| 9 | 服务器地址 | frp-oil.com | Sakura Frp 公网地址 |
| 10 | 服务器端口 | 32762 | Sakura Frp 隧道端口 |
| 11 | 注册包 | 0 | 不发送注册包 |
| 12 | 心跳包 | (空) | 无心跳包（由 STM32 发送 JSON 心跳） |
| 13 | 心跳间隔 | 0 | 不启用模块心跳 |
| 14 | 协议 | (空) | 默认 TCP |
| 15-18 | 保留 | 0,0,0,0,0 | 默认值 |

### 第 2 步：保存配置

```
config,set,save
```

### 第 3 步：重启模块

```
config,set,reboot
```

模块重启后会自动连接 `frp-oil.com:32762`，之后 STM32 通过 UART2 发送的任何数据都会透传到远端。

---

## 配置完成后的行为

- DTU 上电 → 自动注册 4G 网络 → 自动 TCP 连接 `frp-oil.com:32762`
- 连接断开后自动重连
- STM32 只需通过 UART2 发送纯 JSON 数据（`\n` 结尾），DTU 自动透传
- **STM32 不需要发送任何 AT 指令**

---

## 数据流

```
STM32 (UART2 TX)              DTU (Core-Y100M)            Sakura Frp              电脑
     |                          |                          |                      |
     |--{"type":"env",...}\n--> |                          |                      |
     |                          |--4G TCP----------------> |                      |
     |                          |                   TCP 转发-->                    |
     |                          |                          |--------TCP:8090------>|
     |                          |                          |                      |--bridge.js
     |                          |                          |                      |--WS:8080-->网页
```

---

## 验证步骤

1. 确保 Sakura Frp 隧道已启动（启动器状态绿色"运行中"）
2. 确保 `bridge.js` 已运行（`node bridge.js`）
3. 确保 `cable-monitor.html` 已在浏览器打开
4. DTU 上电（指示灯正常后等待约 30 秒让 4G 注册）
5. STM32 上电，自动初始化 DTU 并开始发送数据
6. 网页应显示"已连接"，开始收到温湿度、心跳、舞动数据

---

## 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| DTU 不亮灯 | 供电不足 | 使用 5V/2A USB 供电，不要用电池+升压模块 |
| 配置命令无响应 | 波特率不对 | 确认串口助手波特率为 115200 |
| bridge.js 无连接 | DTU 未成功连接 frp | 检查 SIM 卡、4G 信号、frp 隧道状态 |
| 网页 WS 连接失败 | bridge.js 未启动 | 先启动 `node bridge.js` |
| 数据乱码 | 波特率不匹配 | 确认 DTU 和 STM32 都用 115200 |
