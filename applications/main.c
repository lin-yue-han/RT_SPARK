/*
 * main.c - 电机控制入口
 *
 * 引脚说明（在 board.h 中已配置）：
 *   左电机：PA0 (AIN1), PA1 (AIN2)
 *   右电机：PB15 (BIN1), PB14 (BIN2)
 *   软件I2C：SCL→PD10, SDA→PD8  (已在 board.h 中配置)
 *   UART1：TX→PB6, RX→PB7        (已在 board.h 中配置)
 *
 * 说明：机器车挂载于电缆上，仅支持前进/后退/停止，
 * 不具备转弯能力。上电后待机，检测到覆冰概率超过
 * 阈值时自动前进开始除冰作业。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 左电机引脚 */
#define LEFT_AIN1   GET_PIN(A, 0)   /* PA0 */
#define LEFT_AIN2   GET_PIN(A, 1)   /* PA1 */

/* 右电机引脚 */
#define RIGHT_BIN1  GET_PIN(B, 15)  /* PB15 */
#define RIGHT_BIN2  GET_PIN(B, 14)  /* PB14 */

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
    /* 系统启动时初始化电机引脚（仅一次） */
    motor_init();

    /* 上电后待机，等待覆冰检测触发自动除冰 */
    rt_kprintf("Motor control ready. Standby for de-icing trigger.\n");
    rt_kprintf("Commands: forward, backward, stop\n");
    return 0;
}
