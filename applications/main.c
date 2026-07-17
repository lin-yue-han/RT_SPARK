/*
 * main.c - 电机控制与加热片控制入口
 *
 * 引脚说明（在 board.h 中已配置）：
 *   左电机：PA0 (AIN1), PA1 (AIN2)
 *   右电机：PB15 (BIN1), PB14 (BIN2)
 *   软件I2C：SCL→PD10, SDA→PD8  (已在 board.h 中配置)
 *   UART1：TX→PA9, RX→PA10       (BNO055 独占，已在 board.h 中配置)
 *   UART2：TX→PA2, RX→PA3        (4G Core-Y100M，已在 board.h 中配置)
 *   UART3：TX→PB10, RX→PB11      (FinSH 控制台，已在 board.h 中配置)
 *   加热片继电器：PA8 (HEATER_CTRL) → 继电器IN（高电平触发）
 *
 * 说明：机器车挂载于电缆上，仅支持前进/后退/停止，
 * 不具备转弯能力。上电后待机，检测到覆冰概率超过
 * 阈值时自动前进开始除冰作业。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "dtu_sender.h"

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

    /* 上电默认关闭加热，必须先写关闭状态 */
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

    /* 初始停止 */
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
    /* ===== 第1步：尽早初始化 DTU（上电立即发送系统启动消息） ===== */
    if (dtu_sender_init() == RT_EOK) {
        rt_kprintf("[Main] DTU initialized, sending boot message...\n");
        /* 立即发送启动消息，证明通信链路畅通 */
        dtu_send_boot();
    } else {
        rt_kprintf("[Main] WARNING: DTU init failed\n");
    }

    /* 系统启动时初始化电机引脚 */
    motor_init();

    /* 初始化加热片继电器（上电默认关闭） */
    heater_relay_init();

    /* 上电后待机，等待覆冰检测触发自动除冰 */
    rt_kprintf("System ready. Standby for de-icing trigger.\n");
    rt_kprintf("Commands: forward, backward, stop, heater_on, heater_off\n");
    return 0;
}
