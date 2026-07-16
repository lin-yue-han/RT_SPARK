# RT_SPARK 硬件接口与结构总结

> 生成日期：2026-07-13
> 项目：RT_SPARK — 高空电缆舞动检测与除冰巡检机器人
> 硬件平台：星火一号 STM32F407 开发板 + RT-Thread

---

## 一、核心芯片

| 项目 | 当前代码配置 | 原理图实际芯片 | 备注 |
|------|------------|--------------|------|
| MCU 型号 | STM32F407VE（100脚） | STM32F407ZGT6（144脚） | 需同步修改 |
| ROM | 512KB | 1MB | ZGT6 资源更丰富 |
| RAM | 128KB | 192KB | — |
| 主频 | 168MHz | 168MHz | 一致 |
| 时钟源 | HSI（内部16MHz） | 无外部晶振 | HSI精度±1%，建议后续加8MHz HSE |

---

## 二、引脚分配总表

### 2.1 UART 串口

| 串口 | 用途 | TX 引脚 | RX 引脚 | 波特率 | 代码配置 | 原理图实际 | 是否匹配 |
|------|------|---------|---------|--------|---------|-----------|---------|
| UART1 | BNO055 IMU 传感器 | PB6 | PB7 | 115200 | ✅ 已配置 | PA9/PA10 | ❌ 不匹配，需改代码 |
| UART4 | ESP32-C3 / 4G 模块 | PA0 | PA1 | 115200 | ✅ 已配置 | PA2/PA3 | ❌ 不匹配，需改代码 |
| UART2 | — | — | — | — | ❌ 未配置 | PA2/PA3（ESP32用） | 需新增配置 |

**冲突说明**：代码中 UART4 使用 PA0/PA1，与左电机（PA0/PA1）引脚冲突。原理图已将 ESP32-C3 移至 PA2/PA3，代码需同步改为 UART2。

### 2.2 电机控制（GPIO 开环）

| 电机 | 方向引脚 | STM32 引脚 | TB6612FNG 引脚 | 功能 | 代码宏定义 |
|------|---------|-----------|---------------|------|-----------|
| 左电机 | AIN1 | PA0 | pin21 (AIN1) | 正转/方向1 | LEFT_AIN1 |
| 左电机 | AIN2 | PA1 | pin22 (AIN2) | 反转/方向2 | LEFT_AIN2 |
| 右电机 | BIN1 | PB15 | pin16 (BIN1) | 正转/方向1 | RIGHT_BIN1 |
| 右电机 | BIN2 | PB14 | pin15 (BIN2) | 反转/方向2 | RIGHT_BIN2 |

> **注意**：代码使用纯 GPIO 控制，无 PWM 调速。PWMA/PWMB 在原理图中悬空，需接 +3.3V（全速运行）。

### 2.3 I2C（软件模拟）

| 总线 | 用途 | SCL 引脚 | SDA 引脚 | 地址 | 代码配置 | 原理图 | 备注 |
|------|------|---------|---------|------|---------|--------|------|
| 软件 I2C1 | SHT3X 温湿度 | PD10 | PD8 | 0x44 | ✅ 已配置 | PD10/PD8 | ❌ 缺上拉电阻 |

> **关键问题**：SHT3X 的 SCL/SDA 线上缺少 4.7kΩ 上拉电阻到 +3.3V，I2C 无法正常通信。

### 2.4 SWD 调试接口

| 引脚 | 功能 | 备注 |
|------|------|------|
| PA13 | SWDIO | 原理图缺少排针 |
| PA14 | SWCLK | 原理图缺少排针 |

> **关键问题**：没有 SWD 排针就无法烧录程序和在线调试。建议加 4Pin 排针（VCC/GND/SWDIO/SWCLK）。

### 2.5 PWM（预留，当前未使用）

| 外设 | 通道 | 引脚 | 状态 |
|------|------|------|------|
| TIM1 CH1 | PWM1_CH1 | PE9 | 已配置，未使用 |
| TIM1 CH2 | PWM1_CH2 | PE11 | 已配置，未使用 |
| TIM4 CH3 | PWM4_CH3 | — | 已配置，未使用 |
| TIM4 CH4 | PWM4_CH4 | — | 已配置，未使用 |

---

## 三、外设模块接线详表

### 3.1 GY-BNO055（九轴 IMU 传感器）

| 项目 | 详情 |
|------|------|
| 接口方式 | UART（NMEA 协议，115200 baud） |
| 原理图引脚 | PA9(TX) / PA10(RX) |
| 代码引脚 | PB6(TX) / PB7(RX) ← 需改为 PA9/PA10 |
| 数据字段 | ACC（加速度）、GYR（角速度）、MAG（磁场）、EUL（欧拉角）、QUAT（四元数） |
| 帧格式 | NMEA 风格：`$BNO055,ACC,<x>,<y>,<z>*XX\r\n` |
| 重试机制 | 3 次重试，失败返回错误码 |
| 中断接收 | 逐字节中断回调 + 信号量驱动 |
| PS0/PS1 | 选择 UART 模式：PS0=1, PS1=0（原理图需确认） |
| 外部晶振 | 32.768kHz（原理图未加，建议后续补充） |

**数据结构**：

```c
typedef struct {
    float accel_x, accel_y, accel_z;     // 加速度 (m/s²)
    float gyro_x, gyro_y, gyro_z;        // 角速度 (°/s)
    float mag_x, mag_y, mag_z;           // 磁场 (μT)
    float euler_heading, euler_roll, euler_pitch; // 欧拉角 (°)
    float quaternion_w, quaternion_x, quaternion_y, quaternion_z; // 四元数
    uint8_t system_status, error;
} bn0055_data_t;
```

### 3.2 SHT3X-DIS（温湿度传感器）

| 项目 | 详情 |
|------|------|
| 接口方式 | 软件 I2C（SCL=PD10, SDA=PD8） |
| I2C 地址 | 0x44（默认地址A） |
| 测量命令 | 0x2400（高重复性，Clock Stretching Disabled） |
| CRC 校验 | CRC-8（多项式 0x31，初始值 0xFF） |
| 温度范围 | -40°C ~ 125°C，精度 ±0.3°C |
| 湿度范围 | 0% ~ 100%RH，精度 ±2%RH |
| 上拉电阻 | ❌ 缺少 4.7kΩ 上拉（必须补） |
| NRESET 引脚 | ❌ 悬空（需接 +3.3V） |

**数据结构**：

```c
typedef struct {
    float temperature;    // 温度 (℃)
    float humidity;       // 湿度 (%RH)
    uint16_t status;      // 状态寄存器
    uint8_t error;        // 错误标志
} sht3x_data_t;
```

### 3.3 TB6612FNG（双通道电机驱动）

| 项目 | 详情 |
|------|------|
| 封装 | SSOP24 |
| 通道 A | 左电机：AIN1/AIN2 控制，AO1/AO2 输出 |
| 通道 B | 右电机：BIN1/BIN2 控制，BO1/BO2 输出 |
| 逻辑电源 VCC | +3.3V（pin20） |
| 电机电源 VM | 应接 +6V（⚠️ 需确认是否误接 +3.3V） |
| STBY (pin19) | ❌ 悬空（需接 +3.3V） |
| PWMA (pin23) | ❌ 悬空（需接 +3.3V，全速运行） |
| PWMB (pin14) | ❌ 悬空（需接 +3.3V，全速运行） |

**引脚对照表**：

| TB6612FNG 引脚号 | 引脚名 | 类型 | 当前接线 | 应接 |
|:---:|:---:|:---:|:---|:---|
| 1 | AIN1 | 输入 | — | STM32 PA0（左电机正转） |
| 2 | AIN2 | 输入 | — | STM32 PA1（左电机反转） |
| 3 | PWMA | 输入 | 悬空 ❌ | +3.3V |
| 4 | STBY | 输入 | 悬空 ❌ | +3.3V |
| 5 | BIN1 | 输入 | — | STM32 PB15（右电机正转） |
| 6 | BIN2 | 输入 | — | STM32 PB14（右电机反转） |
| 7 | PWMB | 输入 | 悬空 ❌ | +3.3V |
| 11 | BO1 | 输出 | — | 右电机接口 |
| 12 | BO2 | 输出 | — | 右电机接口 |
| 15 | BIN2 | 输入 | STM32 PB14 ✅ | — |
| 16 | BIN1 | 输入 | STM32 PB15 ✅ | — |
| 20 | VCC | 电源 | +3.3V ✅ | — |
| 21 | AIN2 | 输入 | STM32 PA1 ✅ | — |
| 22 | AIN1 | 输入 | STM32 PA0 ✅ | — |
| 21/22 | AO1/AO2 | 输出 | 左电机接口 ✅ | — |

> ⚠️ 右电机当前接到 AO2（pin5/pin6），应改接到 BO1/BO2（pin11/pin12）。

### 3.4 ESP32-C3 / 4G 通信模块

| 项目 | 详情 |
|------|------|
| 接口方式 | UART |
| 原理图引脚 | PA2(TX) / PA3(RX) |
| 代码引脚 | PA0/PA1（UART4） ← 需改为 PA2/PA3（UART2） |
| 波特率 | 115200 |
| 接收方式 | 中断回调 + 信号量 + 线程 |
| 线程栈 | 1024B，优先级16 |
| 供电 | AMS1117-3.3（⚠️ 4G峰值2A，AMS1117仅1A，后续需换DCDC） |

### 3.5 AMS1117-3.3（电源稳压器）

| 项目 | 详情 |
|------|------|
| 输入 | +6V（pin3 VIN） |
| 输出 | +3.3V（pin2+pin4 VOUT） |
| 地 | GND（pin1） |
| 最大电流 | 1A |
| ❌ 缺少电容 | 输入端需 10µF 陶瓷电容、输出端需 10µF 陶瓷电容 |

---

## 四、软件线程与命令一览

### 4.1 系统线程

| 线程名 | 入口函数 | 栈大小 | 优先级 | 周期 | 功能 |
|--------|---------|:------:|:------:|:----:|------|
| galloping | galloping_thread_entry | 2048B | 12 | 50ms | BNO055采集→舞动检测 |
| uart4_rx | serial_recv_thread_entry | 1024B | 16 | — | ESP32-C3 数据接收 |

### 4.2 MSH 命令一览

| 命令 | 所属文件 | 功能 | 前置条件 |
|------|---------|------|---------|
| `forward` | main.c | 双电机正转（前进） | motor_init 已执行 |
| `backward` | main.c | 双电机反转（后退） | motor_init 已执行 |
| `stop` | main.c | 双电机停止 | — |
| `sensor_init_cmd` | sensor_app.c | 初始化 SHT3X + BNO055 | 手动调用 |
| `sensor_test` | sensor_app.c | 单次读取所有传感器 | sensor_init 后 |
| `sensor_loop` | sensor_app.c | 每秒循环读取传感器 | sensor_init 后 |
| `galloping_start` | galloping_app.c | 启动舞动检测线程 | sensor_init 后 |
| `galloping_stop` | galloping_app.c | 停止舞动检测 | — |
| `galloping_stat` | galloping_app.c | 显示最近一次检测结果 | galloping_start 后 |
| `galloping_reset` | galloping_app.c | 重置检测器（清空窗口） | galloping_start 后 |
| `uart_recv_init` | uart_recv.c | 启动 UART 接收线程 | 手动调用 |

### 4.3 启动顺序

```
上电 → RT-Thread 启动 → main() 执行 motor_init()
     → finsh/MSH 控制台就绪
     → 用户手动输入: sensor_init_cmd → 初始化传感器
     → 用户手动输入: galloping_start → 启动舞动检测
     → 用户手动输入: uart_recv_init → 启动 ESP32 通信（如需）
```

---

## 五、舞动检测算法管道

### 5.1 三层处理流程

```
原始加速度 (m/s²)
    ↓
[第一层] 滑动均值滤波 (MAF, N=8, O(1)环形队列)
    ↓ 抑制高频噪声，保留 0.1~3Hz 舞动信号
[第二层] 指数平滑去直流 (α=0.001, τ≈50s)
    ↓ 分离重力投影 (9.81 m/s²)，提取动态加速度
[第三层] 特征提取 + 规则引擎分类 (64点窗口 ≈ 3.2s)
    ↓ 振幅/位移/频率/能量/扭转 → 5级状态判定
舞动状态输出
```

### 5.2 特征向量

| 特征类别 | 具体指标 | 单位 |
|---------|---------|------|
| 振幅 | X/Y/Z 轴峰-峰值、主导轴振幅 | m/s² |
| 位移 | 估算位移幅值（二次积分） | m |
| 频率 | 主导频率（过零法）、过零率 | Hz, 次/秒 |
| 能量 | 加速度 RMS、振动能量指标 | m/s², — |
| 扭转 | 扭转角度变化（roll/pitch 变化） | ° |
| 统计 | 合加速度均值、标准差 | m/s² |

### 5.3 五级状态分类

| 状态 | 含义 | 判定条件 | 告警级别 |
|:---:|:---|:---|:---:|
| IDLE | 静止/无振动 | 其余情况 | OK |
| BREEZE | 微风振动 | f>1.5Hz 且 A>0.05g | INFO |
| MODERATE | 中等舞动 | f<1.5Hz 且 A>0.15g | WARN |
| SEVERE | 剧烈舞动 | f<0.5Hz 且 A>0.4g | ALERT |
| ICE | 覆冰舞动 | f<0.3Hz 且 A>0.2g 且 θ>15° | DANGER |
| UNKNOWN | 传感器异常 | 置信度 < 0.5 | — |

---

## 六、电源架构

```
+6V（电池/电源适配器）
  ├──→ AMS1117-3.3 ──→ +3.3V
  │     ├── STM32F407 VDD/VDDA
  │     ├── BNO055 VDD
  │     ├── SHT3X VDD
  │     ├── TB6612FNG VCC (pin20)
  │     ├── TB6612FNG STBY (pin19) ← 需补接线
  │     ├── TB6612FNG PWMA (pin23) ← 需补接线
  │     ├── TB6612FNG PWMB (pin14) ← 需补接线
  │     └── ESP32-C3 VDD
  │
  └──→ TB6612FNG VM (pin?) ← 应接+6V，需确认
       ├── 左电机 (AO1/AO2)
       └── 右电机 (BO1/BO2)
```

---

## 七、已知问题与修改清单

### 7.1 P0 — 必须修改（不改板子无法正常工作）

| # | 问题 | 影响 | 修改位置 | 具体操作 |
|---|------|------|---------|---------|
| 1 | TB6612FNG STBY/PWMA/PWMB 悬空 | 电机驱动芯片不工作 | 嘉立创原理图 | pin19/pin23/pin14 接 +3.3V |
| 2 | 右电机接错输出脚 | 右电机不转 | 嘉立创原理图 | H3 从 AO2 改接到 BO1/BO2 (pin11/pin12) |
| 3 | SHT3X I2C 缺上拉电阻 | 温湿度传感器无法通信 | 嘉立创原理图 | SCL/SDA 各加 4.7kΩ 到 +3.3V |
| 4 | UART4 与左电机引脚冲突 | ESP32 通信+电机互相干扰 | board.h + uart_recv.c | UART4→UART2, PA2/PA3 |
| 5 | BNO055 UART 引脚不匹配 | IMU 传感器无法通信 | board.h | PB6/PB7→PA9/PA10 |

### 7.2 P1 — 强烈建议修改

| # | 问题 | 影响 | 修改位置 | 具体操作 |
|---|------|------|---------|---------|
| 6 | 缺 SWD 调试排针 | 无法烧录/在线调试 | 嘉立创原理图 | 加 4Pin 排针 (VCC/GND/PA13/PA14) |
| 7 | 芯片型号不一致 | BSP 配置可能偏差 | board.h | STM32F407VE→STM32F407ZGT6 |
| 8 | TB6612FNG VM 可能接 +3.3V | 电机电压不够 | 嘉立创原理图 | VM 接 +6V net |
| 9 | AMS1117 缺去耦电容 | LDO 输出不稳定，STM32 可能复位 | 嘉立创原理图 | VIN旁10µF、VOUT旁10µF |

### 7.3 P2 — 建议后续补充

| # | 问题 | 建议 |
|---|------|------|
| 10 | SHT3X NRESET 悬空 | 接 +3.3V |
| 11 | 无 HSE 外部晶振 | 加 8MHz 晶振 + 20pF 负载电容 |
| 12 | BNO055 无 32.768kHz 晶振 | 加 32.768kHz 晶振 + 22pF 负载电容 |
| 13 | 4G 模块供电不足 | AMS1117 仅 1A，4G 峰值 2A，换 DCDC |
| 14 | VDDA/VREF 滤波 | 加磁珠 + 电容滤波 |

---

## 八、代码修改清单（我可直接帮你改）

### 文件 1：`drivers/board.h`

```c
/* 芯片型号 */
#define CHIP_NAME_STM32F407ZGT6   // 原来是 STM32F407VE

/* UART1：BNO055 → PA9/PA10（匹配原理图） */
#define BSP_USING_UART1
#define BSP_UART1_TX_PIN       "PA9"    // 原来是 PB6
#define BSP_UART1_RX_PIN       "PA10"   // 原来是 PB7

/* UART2：ESP32-C3 → PA2/PA3（匹配原理图） */
#define BSP_USING_UART2
#define BSP_UART2_TX_PIN       "PA2"
#define BSP_UART2_RX_PIN       "PA3"

/* 注释掉 UART4（已改为 UART2） */
// #define BSP_USING_UART4
// #define BSP_UART4_TX_PIN       "PA0"
// #define BSP_UART4_RX_PIN       "PA1"
```

### 文件 2：`applications/uart_recv.c`

```c
#define UART_NAME       "uart2"     // 原来是 "uart4"
// 信号量名 "uart4_sem" → "uart2_sem"
// 线程名 "uart4_rx" → "uart2_rx"
// 日志中 "UART4" → "UART2"
```

---

## 九、文件结构一览

| 文件 | 行数 | 职责 | 关键 API |
|------|:----:|:---|:---|
| main.c | 108 | 电机控制 + 3条 MSH 命令 | forward/backward/stop |
| gy_bn0055.c/h | 269+46 | BNO055 UART 驱动 | bn0055_init/bn0055_read |
| sht3x.c/h | 206+45 | SHT3X I2C 驱动 | sht3x_init/sht3x_read |
| galloping_detect.c/h | 449+156 | 舞动检测算法核心 | gd_create/gd_feed/gd_analyze |
| galloping_app.c | 219 | 舞动检测应用层 | galloping_start/stop/stat/reset |
| sensor_app.c | 126 | 传感器统一初始化+读取 | sensor_init_cmd/test/loop |
| uart_recv.c | 123 | ESP32-C3 UART 接收 | uart_recv_init |
| board.h | 390 | BSP 引脚/时钟配置 | — |
