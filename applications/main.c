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

/* 外部函数声明 */
extern void sensor_init_all(void);
extern void sensor_monitor_start(void);
extern void galloping_start(void);
extern void dtu_report_start(void);

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
static void heater_on(void)
{
    rt_pin_write(HEATER_CTRL_PIN, RELAY_ON_LEVEL);
    rt_kprintf("Heater ON (PA8=%s)\n", RELAY_ON_LEVEL == PIN_HIGH ? "HIGH" : "LOW");
}
MSH_CMD_EXPORT(heater_on, turn heater relay on);

/* 关闭加热 */
static void heater_off(void)
{
    rt_pin_write(HEATER_CTRL_PIN, RELAY_OFF_LEVEL);
    rt_kprintf("Heater OFF (PA8=%s)\n", RELAY_OFF_LEVEL == PIN_LOW ? "LOW" : "HIGH");
}
MSH_CMD_EXPORT(heater_off, turn heater relay off);

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
        rt_pin_write(LEFT_AIN1, PIN_LOW);
        rt_pin_write(LEFT_AIN2, PIN_LOW);
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
        rt_pin_write(RIGHT_BIN1, PIN_LOW);
        rt_pin_write(RIGHT_BIN2, PIN_LOW);
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

static void stop(void)
{
    left_motor(0);
    right_motor(0);
    rt_kprintf("Stop\n");
}
MSH_CMD_EXPORT(stop, stop);

int main(void)
{
    /* ===== 第0步：最早打印，确认终端通路 ===== */
    rt_kprintf("\n\n");
    rt_kprintf("========================================\n");
    rt_kprintf("   RT_SPARK Booting... (RNDIS Arch)\n");
    rt_kprintf("========================================\n");

    /* ===== 第1步：初始化电机和加热片 ===== */
    motor_init();
    heater_relay_init();

    /* ===== 第2步：初始化传感器 ===== */
    sensor_init_all();

    /* ===== 第3步：启动传感器实时监控 ===== */
    sensor_monitor_start();

    /* ===== 第4步：启动舞动检测和 DTU 上报 ===== */
    rt_kprintf("[Main] Starting galloping detection...\n");
    galloping_start();
    dtu_report_start();

    /* ===== 第5步：上电立即启动电机 ===== */
    rt_kprintf("[Main] Starting motors...\n");
    forward();

    rt_kprintf("[Main] System running. Type 'stop' to stop motors.\n");

    return 0;
}
