/*
 * main.c - RT_SPARK 主入口（RNDIS 双串口架构）
 *
 * 引脚说明（在 board.h 中已配置）：
 *   左电机：PA0 (AIN1), PA1 (AIN2)
 *   右电机：PB15 (BIN1), PB14 (BIN2)
 *   软件I2C：SCL→PD10, SDA→PD8  (已在 board.h 中配置)
 *   UART1：TX→PA9, RX→PA10       (ST-Link 虚拟串口 / BNO055 共享)
 *   UART2：TX→PA2, RX→PA3        (4G 模块 Core-Y100P，硬件 UART 不通)
 *   加热片继电器：PA8 (HEATER_CTRL) → 继电器IN（高电平触发）
 *
 * 数据流：
 *   BNO055/SHT3X → STM32 → 控制台 UART1 → ST-Link VCP (COM9) →
 *   bridge.js (COM9 读取) → TCP 客户端 → frp-oil.com:32762 → 网页
 *
 * 说明：
 *   关闭 FinSH/MSH，UART1 的 RX 留给 BNO055 读取。
 *   控制台输出（UART1 TX）用于输出 JSON 数据到 ST-Link VCP。
 *   4G 模块的硬件 UART 不通，改为 USB 直连电脑，由 bridge.js 转发数据。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "dtu_sender.h"
#include "gy_bn0055.h"

#define DTU_UART_DIAG_MODE 0
#define MOTOR_BOOT_SELF_TEST 0
#define MOTOR_POWER_ON_SEQUENCE_TEST 0

/* 外部函数声明 */
extern void sensor_init_all(void);
extern void sensor_monitor_start(void);
extern void galloping_start(void);
extern void dtu_report_start(void);
extern void gd_reset_detector(void);

/* 左电机引脚 */
#define LEFT_AIN1   GET_PIN(A, 0)   /* PA0 */
#define LEFT_AIN2   GET_PIN(A, 1)   /* PA1 */

/* 右电机引脚 */
#define RIGHT_BIN1  GET_PIN(B, 15)  /* PB15 */
#define RIGHT_BIN2  GET_PIN(B, 14)  /* PB14 */

/* 加热片继电器引脚：PA8 → HEATER_CTRL → 继电器IN
 * 继电器模块 VCC 接 5V (RELAY_5V)，GND 接地
 * 默认高电平触发：PA8=高 → 继电器闭合 → 加热片通电 */
#define HEATER_CTRL_PIN  GET_PIN(A, 8)   /* PA8 */

/* 触发电平定义：高电平触发时 ON=HIGH, OFF=LOW
 * 若实物为低电平触发，交换这两行即可 */
#define RELAY_ON_LEVEL   PIN_HIGH
#define RELAY_OFF_LEVEL  PIN_LOW

/* 初始化加热片继电器 */
static void heater_relay_init(void)
{
    rt_pin_mode(HEATER_CTRL_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(HEATER_CTRL_PIN, RELAY_OFF_LEVEL);
    rt_kprintf("Heater relay initialized on PA8: OFF\n");
}

/* 开启加热 */
void heater_on(void)
{
    rt_pin_write(HEATER_CTRL_PIN, RELAY_ON_LEVEL);
    rt_kprintf("Heater ON (PA8=%s)\n", RELAY_ON_LEVEL == PIN_HIGH ? "HIGH" : "LOW");
}
MSH_CMD_EXPORT(heater_on, turn heater relay on);

/* 关闭加热 */
void heater_off(void)
{
    rt_pin_write(HEATER_CTRL_PIN, RELAY_OFF_LEVEL);
    rt_kprintf("Heater OFF (PA8=%s)\n", RELAY_OFF_LEVEL == PIN_LOW ? "LOW" : "HIGH");
}
MSH_CMD_EXPORT(heater_off, turn heater relay off);

/* 加热超时回调：14秒后自动关闭（上电默认行为） */
static void heater_timeout(void *parameter)
{
    heater_off();
    rt_kprintf("[Heater] Auto off after 14 seconds (power-on default)\n");
}

static rt_timer_t g_heater_timer = RT_NULL;

/* 初始化电机引脚（只需调用一次） */
static void motor_init(void)
{
    rt_pin_mode(LEFT_AIN1, PIN_MODE_OUTPUT);
    rt_pin_mode(LEFT_AIN2, PIN_MODE_OUTPUT);
    rt_pin_mode(RIGHT_BIN1, PIN_MODE_OUTPUT);
    rt_pin_mode(RIGHT_BIN2, PIN_MODE_OUTPUT);

    rt_pin_write(LEFT_AIN1, PIN_LOW);
    rt_pin_write(LEFT_AIN2, PIN_LOW);
    rt_pin_write(RIGHT_BIN1, PIN_LOW);
    rt_pin_write(RIGHT_BIN2, PIN_LOW);

    rt_kprintf("Motor pins initialized\n");
}

/* 左电机控制（speed > 0：正转；speed < 0：反转；speed == 0：停止） */
static void left_motor(int speed)
{
    if (speed > 0) {
        rt_pin_write(LEFT_AIN1, PIN_HIGH);
        rt_pin_write(LEFT_AIN2, PIN_LOW);
    } else if (speed < 0) {
        rt_pin_write(LEFT_AIN1, PIN_LOW);
        rt_pin_write(LEFT_AIN2, PIN_HIGH);
    } else {
        /* TB6612: IN1=HIGH, IN2=HIGH enables short-brake for a harder stop. */
        rt_pin_write(LEFT_AIN1, PIN_HIGH);
        rt_pin_write(LEFT_AIN2, PIN_HIGH);
    }
}

/* 右电机控制（speed > 0：正转；speed < 0：反转；speed == 0：停止） */
static void right_motor(int speed)
{
    if (speed > 0) {
        rt_pin_write(RIGHT_BIN1, PIN_HIGH);
        rt_pin_write(RIGHT_BIN2, PIN_LOW);
    } else if (speed < 0) {
        rt_pin_write(RIGHT_BIN1, PIN_LOW);
        rt_pin_write(RIGHT_BIN2, PIN_HIGH);
    } else {
        /* TB6612: IN1=HIGH, IN2=HIGH enables short-brake for a harder stop. */
        rt_pin_write(RIGHT_BIN1, PIN_HIGH);
        rt_pin_write(RIGHT_BIN2, PIN_HIGH);
    }
}

/* ========== 运动命令（电缆上仅前进/后退/停止） ========== */
static void forward(void)
{
    left_motor(100);
    right_motor(100);
    rt_kprintf("Forward\n");
}
MSH_CMD_EXPORT(forward, go forward);

static void backward(void)
{
    left_motor(-100);
    right_motor(-100);
    rt_kprintf("Backward\n");
}
MSH_CMD_EXPORT(backward, go backward);

void stop(void)
{
    left_motor(0);
    right_motor(0);
    rt_kprintf("Stop\n");
}
MSH_CMD_EXPORT(stop, stop);

/* ========== 电机状态与定时器（全局变量必须在函数之前定义） ========== */
static volatile int g_motor_position = 0;   /* 当前位置 0~100% */
static volatile int g_motor_state = 0;       /* 0=stop, 1=forward, 2=backward */
static rt_timer_t g_motor_timer = RT_NULL;

/* 电机超时回调：6秒后自动停止 */
void motor_timeout(void *parameter)
{
    stop();
    g_motor_state = 0;
    rt_kprintf("[Motor] Auto stop after 6 seconds\n");
}

/* 启动电机并设置6秒定时器（供外部调用） */
void start_motor_with_timeout(int dir)
{
    /* 停止现有定时器 */
    /* 启动电机 */
    if (dir == 1) {
        forward();
        g_motor_state = 1;
    } else if (dir == -1) {
        backward();
        g_motor_state = 2;
    } else {
        stop();
        g_motor_state = 0;
        return;
    }

    /* 创建 6 秒定时器 */
}

/* ========== 远程命令接收与电机状态管理 ========== */

static void motor_set_state(const char *state, int pos)
{
    if (rt_strcmp(state, "forward") == 0) {
        forward();
        g_motor_state = 1;
    } else if (rt_strcmp(state, "backward") == 0) {
        backward();
        g_motor_state = 2;
    } else if (rt_strcmp(state, "stop") == 0) {
        stop();
        g_motor_state = 0;
    }
    g_motor_position = pos;
}

static const char *motor_state_name(void)
{
    switch (g_motor_state) {
        case 1:  return "forward";
        case 2:  return "backward";
        default: return "stop";
    }
}

/**
 * @brief 远程命令接收线程
 *        从 gy_bn0055.c 提取的 UART1 命令缓冲区中读取命令并执行
 */
static void remote_cmd_thread_entry(void *parameter)
{
    char cmd[CMD_BUF_SIZE];
    rt_kprintf("[RemoteCMD] Command receiver thread started\n");

    while (1) {
        /* 等待命令（阻塞，最长 500ms） */
        int ret = cmd_get_remote_command(cmd, 500);
        if (ret != RT_EOK) {
            continue;
        }

        rt_kprintf("[RemoteCMD] Received: %s\n", cmd);

        /* 解析并执行命令 */
        if (rt_strcmp(cmd, "forward") == 0) {
            start_motor_with_timeout(1);
            dtu_send_motor("forward", g_motor_position);
            rt_kprintf("[RemoteCMD] -> FORWARD executed (6s auto-stop)\n");
        }
        else if (rt_strcmp(cmd, "backward") == 0) {
            start_motor_with_timeout(-1);
            dtu_send_motor("backward", g_motor_position);
            rt_kprintf("[RemoteCMD] -> BACKWARD executed (6s auto-stop)\n");
        }
        else if (rt_strcmp(cmd, "stop") == 0) {
            start_motor_with_timeout(0);
            dtu_send_motor("stop", g_motor_position);
            rt_kprintf("[RemoteCMD] -> STOP executed\n");
        }
        else if (rt_strcmp(cmd, "heater_on") == 0) {
            heater_on();
            rt_kprintf("[RemoteCMD] -> HEATER ON executed\n");
        }
        else if (rt_strcmp(cmd, "heater_off") == 0) {
            heater_off();
            rt_kprintf("[RemoteCMD] -> HEATER OFF executed\n");
        }
        else if (rt_strcmp(cmd, "reset_detector") == 0) {
            gd_reset_detector();
            rt_kprintf("[RemoteCMD] -> DETECTOR RESET executed\n");
        }
        else {
            rt_kprintf("[RemoteCMD] Unknown command: %s\n", cmd);
        }
    }
}

/* 上电电机自动测试序列：先停5秒，再前进10秒，然后后退12秒 */
static void power_on_motor_thread(void *parameter)
{
    rt_kprintf("[PowerOn] Starting motor sequence: wait 5s, forward 10s, then backward 12s\n");

    /* 先停5秒 */
    rt_thread_mdelay(5000);

    /* 前进13秒 */
    forward();
    g_motor_state = 1;
    rt_thread_mdelay(13000);

    /* 立刻后退16秒 */
    backward();
    g_motor_state = 2;
    rt_thread_mdelay(16000);

    /* 停止 */
    stop();
    g_motor_state = 0;
    rt_kprintf("[PowerOn] Motor sequence completed\n");
}

#if MOTOR_BOOT_SELF_TEST
static void motor_boot_self_test_thread(void *parameter)
{
    RT_UNUSED(parameter);

    rt_kprintf("[MotorTest] Boot self-test: wait 3s, forward 3s, stop\n");
    rt_thread_mdelay(3000);
    forward();
    g_motor_state = 1;
    rt_thread_mdelay(3000);
    stop();
    g_motor_state = 0;
    rt_kprintf("[MotorTest] Boot self-test completed\n");
}
#endif

#if DTU_UART_DIAG_MODE
static void dtu_diag_drain(rt_device_t dtu, int duration_ms)
{
    char buf[96];
    int got = 0;
    rt_tick_t end_tick = rt_tick_get() + rt_tick_from_millisecond(duration_ms);

    while ((rt_int32_t)(end_tick - rt_tick_get()) > 0) {
        int n = rt_device_read(dtu, 0, buf, sizeof(buf));
        if (n > 0) {
            int i;
            got = 1;
            rt_kprintf("[DTU-DIAG] RX(%d): ", n);
            for (i = 0; i < n; i++) {
                char ch = buf[i];
                if (ch == '\r') {
                    rt_kprintf("\\r");
                } else if (ch == '\n') {
                    rt_kprintf("\\n");
                } else if (ch >= 32 && ch <= 126) {
                    rt_kprintf("%c", ch);
                } else {
                    rt_kprintf("\\x%02X", (unsigned char)ch);
                }
            }
            rt_kprintf("\n");
        }
        rt_thread_mdelay(20);
    }

    if (!got) {
        rt_kprintf("[DTU-DIAG] no RX in %dms\n", duration_ms);
    }
}

static void dtu_diag_send_line(rt_device_t dtu, const char *line)
{
    rt_kprintf("[DTU-DIAG] TX: %s\n", line);
    rt_device_write(dtu, 0, line, rt_strlen(line));
    rt_device_write(dtu, 0, "\r\n", 2);
    dtu_diag_drain(dtu, 1800);
}

static int dtu_uart_diag_main(void)
{
    rt_device_t console_dev = rt_console_get_device();
    rt_device_t dtu = RT_NULL;
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    static const char *probe_names[] = {
        "A",
        "CR",
        "LF",
        "A_CR",
        "A_LF",
        "A_CRLF",
        "CONFIG_CRLF",
    };
    static const char *probe_data[] = {
        "A",
        "\r",
        "\n",
        "A\r",
        "A\n",
        "A\r\n",
        "config,get,tcp\r\n",
    };
    int i;

    if (console_dev != RT_NULL) {
        rt_device_close(console_dev);
        rt_device_open(console_dev, RT_DEVICE_OFLAG_RDWR);
    }

    rt_kprintf("\n\n========================================\n");
    rt_kprintf("   RT_SPARK DTU UART2 DIAG MODE\n");
    rt_kprintf("   COM9 <-> UART2, 115200 8N1\n");
    rt_kprintf("========================================\n");

    dtu = rt_device_find("uart2");
    if (dtu == RT_NULL) {
        rt_kprintf("[DTU-DIAG] ERROR: uart2 not found\n");
        return -1;
    }

    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity = PARITY_NONE;
    if (rt_device_open(dtu, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[DTU-DIAG] ERROR: open uart2 failed\n");
        return -1;
    }

    cfg.baud_rate = BAUD_RATE_115200;
    rt_device_control(dtu, RT_DEVICE_CTRL_CONFIG, &cfg);
    rt_kprintf("[DTU-DIAG] uart2 opened, starting loopback signature test in 3s\n");
    rt_thread_mdelay(3000);

    for (i = 0; i < (int)(sizeof(probe_data) / sizeof(probe_data[0])); i++) {
        rt_kprintf("[DTU-DIAG] TX-SIG %s len=%d\n", probe_names[i], (int)rt_strlen(probe_data[i]));
        rt_device_write(dtu, 0, probe_data[i], rt_strlen(probe_data[i]));
        dtu_diag_drain(dtu, 1200);
        rt_thread_mdelay(800);
    }

    rt_kprintf("[DTU-DIAG] probe sequence done; entering passive bridge loop\n");
    while (1) {
        char buf[96];
        int n;

        n = rt_device_read(dtu, 0, buf, sizeof(buf));
        if (n > 0) {
            rt_kprintf("[DTU-DIAG] RX-LIVE(%d): ", n);
            if (console_dev != RT_NULL) {
                rt_device_write(console_dev, 0, buf, n);
            }
            rt_kprintf("\n");
        }

        if (console_dev != RT_NULL) {
            n = rt_device_read(console_dev, 0, buf, sizeof(buf));
            if (n > 0) {
                rt_device_write(dtu, 0, buf, n);
            }
        }

        rt_thread_mdelay(20);
    }
}
#endif

int main(void)
{
#if DTU_UART_DIAG_MODE
    return dtu_uart_diag_main();
#endif

    /* ===== 第0步：最早打印，确认终端通路 ===== */
    rt_kprintf("\n\n");
    rt_kprintf("========================================\n");
    rt_kprintf("   RT_SPARK Booting... (RNDIS Arch)\n");
    rt_kprintf("========================================\n");

    /* BNO055 now uses UART3, keep UART1/ST-Link console open for logs. */
    rt_kprintf("[Main] Console stays on UART1; BNO055 uses UART3\n");

    /* ===== 第1步：初始化电机和加热片 ===== */
    motor_init();
#if MOTOR_POWER_ON_SEQUENCE_TEST
    {
        rt_thread_t power_on_thread = rt_thread_create(
            "power_motor",
            power_on_motor_thread,
            RT_NULL,
            1024,
            5,
            10
        );
        if (power_on_thread != RT_NULL) {
            rt_thread_startup(power_on_thread);
            rt_kprintf("[Main] Power-on motor sequence thread started EARLY\n");
        } else {
            rt_kprintf("[Main] ERROR: Failed to create power-on motor thread!\n");
        }
    }
#endif
    heater_relay_init();

    /* 上电默认加热14秒后自动关闭 */
    heater_on();
    g_heater_timer = rt_timer_create("heater_t", heater_timeout, RT_NULL,
        rt_tick_from_millisecond(14000), RT_TIMER_FLAG_ONE_SHOT);
    if (g_heater_timer != RT_NULL) {
        rt_timer_start(g_heater_timer);
        rt_kprintf("[Main] Power-on heater ON, auto-off in 14s\n");
    } else {
        rt_kprintf("[Main] ERROR: Failed to create heater timer!\n");
    }

    /* ===== 第2步：初始化传感器（BNO055 独占 UART1 RX） ===== */
    sensor_init_all();

    rt_kprintf("[Main] Console active\n");

    /* ===== 第2.6步：初始化远程命令接收信号量 ===== */
    cmd_sem_init();
    rt_kprintf("[Main] Remote command semaphore initialized\n");

    /* ===== 第3步：启动传感器实时监控 ===== */
    sensor_monitor_start();

    /* ===== 第4步：启动舞动检测和 DTU 上报 ===== */
    rt_kprintf("[Main] Starting galloping detection...\n");
    galloping_start();
    dtu_report_start();

    /* ===== 第4.5步：启动远程命令接收线程 ===== */
    rt_kprintf("[Main] Starting remote command receiver...\n");
    rt_thread_t cmd_thread = rt_thread_create(
        "remote_cmd",
        remote_cmd_thread_entry,
        RT_NULL,
        1024,
        20,  /* 优先级低于传感器线程，高于空闲线程 */
        10
    );
    if (cmd_thread != RT_NULL) {
        rt_thread_startup(cmd_thread);
        rt_kprintf("[Main] Remote command receiver thread started\n");
    } else {
        rt_kprintf("[Main] ERROR: Failed to create remote command thread!\n");
    }

    /* ===== 第4.6步：启动 UART2 无线命令接收（Air778E 4G）===== */
    rt_kprintf("[Main] UART2 AT sender owns 4G module; wireless command receiver disabled\n");

    /* ===== 第5步：上电立即启动电机（前进/后退长序列测试） ===== */
#if !MOTOR_POWER_ON_SEQUENCE_TEST
    rt_kprintf("[Main] Power-on motor sequence disabled; waiting for wireless commands\n");
#endif
#if MOTOR_BOOT_SELF_TEST
    {
        rt_thread_t motor_test_thread = rt_thread_create(
            "motor_test",
            motor_boot_self_test_thread,
            RT_NULL,
            1024,
            19,
            10
        );
        if (motor_test_thread != RT_NULL) {
            rt_thread_startup(motor_test_thread);
            rt_kprintf("[Main] Motor boot self-test thread started\n");
        } else {
            rt_kprintf("[Main] ERROR: Failed to create motor boot self-test thread!\n");
        }
    }
#endif

    rt_kprintf("[Main] System running. Type 'stop' to stop motors.\n");

    return 0;
}
