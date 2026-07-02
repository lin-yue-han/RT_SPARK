# RT_SPARK — 高空电缆维护机器车

基于 **RT-Thread** 实时操作系统的高空电缆维护机器车系统，运行于**星火一号开发板（STM32F407VE）**，实现电缆舞动监督与覆冰自动除冰功能。

---

## 功能概览

- **电缆舞动监督**：BNO055 九轴 IMU（UART1）20Hz 采集，三层信号处理管道（MAF 滤波 → 指数平滑去直流 → 特征提取 → 规则引擎）将舞动分为 IDLE / BREEZE / MODERATE / SEVERE / ICE 五级并告警
- **覆冰自动除冰**：SHT3X 温湿度传感器（软件 I2C1）持续检测，温度 < 0°C 且湿度 > 85% 时自动触发电机前进执行除冰
- **4G 远程通信**（预留）：Air780E Cat.1 核心板，AT 指令透传，将舞动状态和覆冰数据上报云端
- **FinSH 命令行**：支持 `forward` / `backward` / `stop` / `bno055_test` / `sht3x_test` 等调试命令

---

## 硬件配置

| 模块 | 型号 | 接口 |
|------|------|------|
| 主控 | STM32F407VE 星火一号 | — |
| IMU | BNO055 | UART1，PB6(TX)/PB7(RX)，115200 baud |
| 温湿度 | SHT3X-DIS | 软件 I2C1，SCL→PD10 / SDA→PD8 |
| 电机驱动 | TB6612 × 1 | 开环 GPIO 方向控制 |
| 左电机 | MG513 | AIN1→PA0 / AIN2→PA1 |
| 右电机 | MG513 | BIN1→PB15 / BIN2→PB14 |
| 4G 通信 | Air780E Cat.1 | UART（预留，待分配引脚） |
| 供电 | 4 × 1.5V 干电池（6V） | → LDO → 3.3V |

---

## 机械结构

- 3D 打印制动关键部件，光面黑色亚克力板方形外壳
- 双主动轮固定于电缆正上方提供驱动，双从动轮布置于斜下方防止翻车
- 主要零件集中于电缆下方，重心低，运行稳定
- 挂载于输电导线上，仅支持前进 / 后退 / 停止，无转向能力

---

## 软件架构

```
RT_SPARK/
├── applications/
│   ├── main.c              # 系统入口，电机 GPIO 控制
│   ├── motor.h             # 电机控制接口声明
│   ├── sht3x.c / .h        # SHT3X 驱动（软件 I2C + CRC-8 校验）
│   ├── gy_bn0055.c / .h    # BNO055 UART 驱动（文本帧解析状态机）
│   ├── galloping_detect.c  # 舞动检测核心算法（三层管道）
│   ├── galloping_detect.h  # 算法接口与数据结构定义
│   ├── galloping_app.c     # 舞动检测应用线程（20Hz 采集）
│   ├── sensor_app.c        # 覆冰检测应用（含自动触发逻辑）
│   └── uart_recv.c         # UART 接收（预留）
├── drivers/
│   ├── board.h             # 引脚宏定义
│   └── ...
├── rt-thread/              # RT-Thread 内核源码
├── libraries/              # STM32 HAL 库
└── rtconfig.h              # RT-Thread 功能配置
```

---

## 如何编译烧录

1. 安装 [RT-Thread Studio](https://www.rt-thread.org/studio.html)
2. 导入工程：`File → Import → RT-Thread Studio Project → 选择本目录`
3. 选择 Debug 配置，点击 Build（锤子图标）
4. 连接星火一号开发板（ST-Link），点击 Debug 或 Download 烧录

---

## 舞动检测算法原理

核心算法位于 `galloping_detect.c`，采用三层处理管道：

1. **MAF 滤波**（N=8 窗口）：消除高频噪声
2. **指数平滑去直流**（α=0.001）：分离动态分量与静态偏置
3. **特征提取**（64 点窗口，20Hz 采样）：
   - 峰峰值 $A_{pp}$
   - 过零法测频 $f_{zc}$
   - RMS 振幅
   - 位移估算 $A_{disp} \approx A_{pp} / (2\omega^2)$
   - 扭转角 $\theta_{twist}$
4. **规则引擎**：五级分类 IDLE → BREEZE → MODERATE → SEVERE → ICE

---

## License

MIT License — 自由使用、修改和分发，保留原始版权声明即可。详见 [LICENSE](LICENSE)。

---

## 作者

本项目为课程大作业项目，欢迎 Issue 和 PR。
