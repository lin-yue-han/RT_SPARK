# RT_SPARK 无线通信方案 — 部署指南

## 架构总览

```
STM32F407VE ──UART2──→ Air778E 4G模块
                          │ 4G蜂窝网络
                          ▼
                   frp 公网中转节点（仅需公网IP，不做业务逻辑）
                          │ TCP 透传
                          ▼
                   你的电脑
                   ├── bridge.js (TCP:8090 → WS:8080)
                   └── cable-monitor.html (浏览器打开)
```

**数据不需要经过任何云服务器处理、存储。** frp 节点只做 TCP 连接转发，可以跑在任何有公网 IP 的机器上。

---

## 文件清单

```
tools/wireless_bridge/
├── bridge.js              # TCP→WebSocket 桥接脚本（Node.js）
├── package.json           # Node.js 依赖声明
├── cable-monitor.html     # 电脑端监测网页
├── frps.ini               # frp 服务端配置（公网节点）
├── frpc.ini               # frp 客户端配置（你的电脑）
├── Air778E_AT配置说明.md  # Air778E 4G模块 AT 指令配置
└── README.md              # 本文档
```

---

## 部署步骤（按顺序执行）

### 第 1 步：注册并配置 Sakura Frp（推荐）

你已经在 natfrp.com 注册了账号（用户名 `linyuehan`），接下来：

#### 1.1 实名认证

- 点击左侧菜单 **"实名认证"**
- 按提示完成认证（费用 1 元，需要支付宝/微信扫码）
- 等待审核通过（通常几分钟到几小时）

#### 1.2 创建隧道

- 点击左侧菜单 **"隧道列表"** → **"创建隧道"**
- 填写以下信息：

| 字段 | 填写内容 |
|------|---------|
| 隧道类型 | **TCP** |
| 本地端口 | `8090` |
| 远程端口 | 留空（自动分配）或选一个你喜欢的 |
| 节点 | 选一个国内节点（延迟低） |
| 备注 | `RT_SPARK` |

- 点击 **创建**
- 记下分配给你的 **公网地址**（格式：`xxx.natfrp.cloud:xxxxx`）

#### 1.3 下载启动器

- 点击左侧菜单 **"下载"**
- 下载 **Windows 启动器**（`SakuraLauncher.exe`）
- 运行启动器，用账号密码登录

#### 1.4 启动隧道

- 在启动器中找到你创建的隧道，点击 **"启动"**
- 状态变为绿色 **"运行中"** 表示隧道已连通

**此时，Sakura Frp 已经帮你做好了 frp 服务端和客户端的所有工作。**

你不需要自己下载 frps/frpc，不需要写配置文件，不需要自己部署 frp server。

---

### 替代方案：自建 frp server（如果你有自己的服务器）

如果你有学校实验室服务器或云主机，可以用传统的 frp 方案：

---

### 第 2 步：配置你的电脑（bridge.js）

#### 2.1 安装 Node.js

如果没装过 Node.js，去 [nodejs.org](https://nodejs.org) 下载 LTS 版本安装。

验证安装：
```bash
node --version   # 应输出 v18.x 或更高
```

#### 2.2 启动 bridge.js

`bridge.js` 是**零依赖**的纯 Node.js 实现，不需要 `npm install`，直接运行：

```bash
cd D:/AI_Intro/RT-ThreadStudio/workspace/RT_SPARK/tools/wireless_bridge
node bridge.js
```

看到以下输出表示启动成功：
```
[TCP] TCP Server 监听 0.0.0.0:8090  (等待 Air778E / frp 连接)
[WS]  WebSocket Server 监听 ws://localhost:8080  (等待网页连接)
[SYS] ==========================================
[SYS]   RT_SPARK Wireless Bridge 已启动
```

> **注意：** 如果你用 Sakura Frp，不需要启动 frpc！Sakura Frp 启动器已经帮你做了 frp 客户端的工作。bridge.js 只需要监听本地 8090 端口，Sakura Frp 会自动把公网流量转发过来。

---

### 第 3 步：配置 Air780E 4G 模块

按照 `Air778E_AT配置说明.md` 中的步骤操作。

**核心是把 Air780E 配置为 TCP Client 透传模式**，目标地址为 Sakura Frp 分配的公网地址。

关键 AT 指令（速查）：
```
AT+CGATT?                                                    // 确认网络就绪
AT+CIPSTART="TCP","<Sakura Frp公网地址>",<Sakura Frp端口>    // 连接 frp 节点
AT+CIPMODE=1                                                 // 设置透传模式
AT+CIPSEND                                                   // 进入透传
```

> 把 `<Sakura Frp公网地址>` 和 `<Sakura Frp端口>` 替换为你创建隧道时分配的实际地址。

---

### 第 4 步：打开网页

用 Chrome 或 Edge 浏览器打开 `cable-monitor.html`：

```
方式一：直接双击 cable-monitor.html 打开
方式二：浏览器地址栏输入 file:///D:/AI_Intro/RT-ThreadStudio/workspace/RT_SPARK/tools/wireless_bridge/cable-monitor.html
```

页面左上角状态变为绿色"已连接"后，就能实时看到数据。

---

### 第 5 步：STM32 上电

STM32 上电后自动启动：
1. 传感器初始化（BNO055 + SHT3X）
2. 舞动检测线程（20Hz 采集）
3. DTU 数据上报线程（温湿度 5s/次，心跳 10s/次）

数据通过 UART2 → Air778E → 4G → frp → bridge.js → WebSocket → 网页，全链路自动传输。

**STM32 端代码不需要任何修改。**

---

## 完整启动顺序（使用 Sakura Frp）

每次使用时，按以下顺序启动：

| 顺序 | 在哪里 | 做什么 |
|------|--------|--------|
| 1 | 你的电脑 | 启动 Sakura Frp 启动器，启动隧道 |
| 2 | 你的电脑 | 启动 bridge.js：`node bridge.js` |
| 3 | 你的电脑 | 打开网页：`cable-monitor.html` |
| 4 | 开发板 | STM32 上电 |

> **不需要启动 frpc！** Sakura Frp 启动器已经帮你做了 frp 客户端的所有工作。

---

## 功能说明

### 网页显示内容

| 面板 | 数据来源 | 更新频率 |
|------|---------|---------|
| 舞动状态 | `dtu_send_galloping()` | 每 ~3.2 秒（一个分析窗口） |
| 环境监测 | `dtu_send_env()` | 每 5 秒 |
| 电机状态 | `dtu_send_motor()` | 状态变化时 |
| 心跳 | `dtu_send_heartbeat()` | 每 10 秒 |

### 网页控制功能

| 按钮 | 发送的命令 | STM32 执行 |
|------|-----------|-----------|
| 前进 | `forward` | 双电机正转 |
| 后退 | `backward` | 双电机反转 |
| 停止 | `stop` | 双电机停止 |
| 加热 开 | `heater_on` | 继电器闭合 |
| 加热 关 | `heater_off` | 继电器断开 |

> **注意：** 网页发送命令目前会通过 bridge.js → TCP → Air778E → UART2 透传到 STM32。
> STM32 端需要添加 UART2 命令解析逻辑来响应这些命令（当前 `uart_recv.c` 只打印不执行）。
> 如需实现双向控制，在 `uart_recv.c` 的 `serial_recv_thread_entry` 中添加命令解析即可。

---

## 故障排查

### frp 相关

| 现象 | 原因 | 解决 |
|------|------|------|
| frpc 提示 `login to server failed` | token 不一致或端口不对 | 检查 frps.ini 和 frpc.ini 的 token、端口 |
| frpc 提示 `proxy already exists` | 端口被其他 frpc 占用 | 换一个 remote_port |
| frp 面板打不开 | 防火墙拦截 | 公网节点开放 7500 端口 |

### bridge.js 相关

| 现象 | 原因 | 解决 |
|------|------|------|
| `Error: listen EADDRINUSE` | 端口被占用 | `--tcp-port=9091` 或 `--ws-port=8081` |
| `Cannot find module 'ws'` | 依赖未安装 | 运行 `npm install` |
| TCP 有连接但无数据 | Air778E 未进入透传 | 检查 AT 指令配置 |

### Air778E 相关

| 现象 | 原因 | 解决 |
|------|------|------|
| `AT` 返回 `ERROR` | 波特率不对 | 尝试 9600 / 115200 / 921600 |
| `+CGATT: 0` | SIM 卡未激活 | 等待 30 秒，检查 SIM 卡和信号 |
| `CONNECT FAIL` | 目标地址不通 | 检查 frp 是否正常运行 |
| 透传模式无数据 | STM32 未发送数据 | 用 FinSH 输入 `galloping_stat` 确认 STM32 有数据 |

### 网页相关

| 现象 | 原因 | 解决 |
|------|------|------|
| WebSocket 连接失败 | bridge.js 未启动 | 先 `node bridge.js` |
| 状态一直显示"未连接" | 端口不对 | 确认 bridge.js 在 8080 端口 |
| 有连接但无数据 | Air778E 未连接 | 检查 4G 模块和 frp 链路 |

---

## 端口规划

| 端口 | 协议 | 用途 | 在哪里 |
|------|------|------|--------|
| 8090 | TCP | Air780E → Sakura Frp → bridge.js 数据传输 | 电脑 |
| 8080 | WebSocket | 网页 ↔ bridge.js | 电脑 |

> frp 的 7000/7500 等端口由 Sakura Frp 管理，你不需要关心。

---

## 安全提示

- Sakura Frp 的隧道建议设置访问密码（在隧道配置中开启）
- bridge.js 的 TCP 端口不要暴露到公网（Sakura Frp 已做转发，不需要电脑开放端口给公网）
- 如果只是调试用途，用完可以关闭 Sakura Frp 隧道，避免端口暴露
