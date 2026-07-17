# RT_SPARK RNDIS 双串口架构交接文档

## 日期
2026-07-17

## 核心问题
4G 模块（Air780E / Core-Y100P）的硬件 UART（UART2）不通，但 USB 虚拟串口和 RNDIS 网络完全正常。

## 解决方案：RNDIS 双串口架构

### 数据流
```
BNO055/SHT3X → STM32 (UART1) → ST-Link VCP (COM9) →
bridge.js (COM9 读取) → TCP 客户端 → frp-oil.com:32762 → 网页
```

### 关键变更

#### 1. STM32 端（dtu_sender.c/h）
- **移除 UART2 依赖**：不再通过 PA2/PA3 向 4G 模块发送数据
- **改为控制台输出**：使用 `rt_console_get_device()` 获取 ST-Link VCP 设备句柄
- **JSON 输出**：所有数据（舞动特征、温湿度、心跳、电机状态）通过 UART1 TX 输出到 COM9

#### 2. STM32 端（main.c）
- **恢复为正常入口**：移除 v6 波特率扫描固件
- **自动启动序列**：
  1. 初始化电机和加热片
  2. 初始化传感器（SHT3X + BNO055）
  3. 启动传感器实时监控
  4. 启动舞动检测（galloping_start）
  5. 启动 DTU 定时上报（dtu_report_start）
  6. 启动电机前进

#### 3. STM32 端（galloping_app.c）
- `galloping_start()` 和 `dtu_report_start()` 去掉 `static` 修饰符，供 main.c 调用

#### 4. 电脑端（bridge.js）
- **COM9 读取**：使用 Node.js `fs.open` / `fs.read` 读取 ST-Link VCP
- **JSON 解析**：过滤非 JSON 数据（如 rt_kprintf 输出），只转发有效 JSON
- **TCP 客户端**：连接到 `frp-oil.com:32762`，将 JSON 数据发送到 frp 服务器
- **WebSocket 服务器**：保留 `ws://localhost:8080` 供本地调试网页连接
- **命令转发**：支持网页/WebSocket 发送命令，通过 COM9 写入 STM32

## 使用方法

### 1. 编译并烧录 STM32 固件
```bash
# 在 RT-Thread Studio 中编译
scons -j4

# 烧录到星火一号（通过 ST-Link）
```

### 2. 启动 bridge.js
```bash
# 进入 bridge.js 目录
cd tools/wireless_bridge

# 启动桥接服务（默认参数）
node bridge.js

# 或指定参数
node bridge.js --com-port=\\.\COM9 --tcp-host=frp-oil.com --tcp-port=32762 --ws-port=8080
```

### 3. 验证数据流

#### 检查 COM9 输出
打开 SSCOM 或 YEDTestTools，连接 COM9（115200），应看到 JSON 数据输出：
```json
{"type":"boot","ts":12345,"msg":"RT_SPARK system started","version":"1.0"}
{"type":"heartbeat","ts":12345}
{"type":"env","ts":12345,"temperature":25.3,"humidity":60.5}
{"type":"galloping","ts":12345,"state":"IDLE","amp_dominant":0.123,...}
```

#### 检查 bridge.js 日志
```
[15:30:00] [COM] \\.\COM9 已打开 (fd=3)
[15:30:01] [DATA] 系统启动: RT_SPARK system started
[15:30:05] [DATA] 心跳 #1
[15:30:10] [DATA] 温湿度: 25.3C  60.5%RH
[15:30:15] [DATA] 舞动: state=IDLE amp=0.123 freq=0.5Hz
```

#### 检查 WebSocket
打开浏览器访问 `http://localhost:8080/health`，或连接 `ws://localhost:8080`。

#### 检查 frp 连接
bridge.js 日志中应显示 `[TCP] 已连接到 frp-oil.com:32762`。

## 注意事项

1. **BNO055 与 UART1 共享**：BNO055 通过 UART1（PA9/PA10）与 STM32 通信。由于关闭 FinSH/MSH，UART1 RX 留给 BNO055，UART1 TX 用于控制台输出。BNO055 驱动有超时保护，不会解析 JSON 文本。

2. **COM 口参数**：ST-Link VCP 默认波特率 115200, 8N1。确保设备管理器中 COM9 参数正确。

3. **数据重复**：sensor_monitor 线程和 dtu_report 线程都会发送温湿度/心跳数据，导致少量重复。不影响功能，可在后续优化中合并。

4. **4G 模块 USB**：模块的 USB 线保持连接电脑，提供 RNDIS 网络。模块不需要通过 UART2 与 STM32 通信。

## 故障排查

### COM9 无法打开
- 检查设备管理器中是否有 "STMicroelectronics STLink Virtual COM Port (COM9)"
- 检查是否有其他程序占用了 COM9（如 SSCOM、YEDTestTools）
- 尝试重新插拔 ST-Link

### 无 JSON 数据输出
- 检查 STM32 是否已烧录新固件
- 检查 rtconfig.h 中 `RT_USING_CONSOLE` 和 `RT_CONSOLE_DEVICE_NAME "uart1"` 是否启用
- 检查 BNO055 是否初始化成功（看 rt_kprintf 输出）

### TCP 连接失败
- 检查网络连接（模块 RNDIS 网络是否已启用）
- 检查 frp 服务器地址和端口是否正确
- 检查防火墙是否阻止了出站 TCP 连接

### WebSocket 连接失败
- 检查端口 8080 是否被占用
- 使用 `--ws-port=8081` 指定其他端口

## 后续优化建议

1. 将 sensor_monitor 和 dtu_report 合并，避免重复发送数据
2. 在 bridge.js 中添加数据缓存/重发机制，提高可靠性
3. 使用 HTTP POST 代替 TCP 直连，提高与 frp 的兼容性
4. 添加数据校验（CRC/校验和），防止传输错误

## 提交记录
GitHub: `175e50e` - feat: RNDIS dual-serial architecture
