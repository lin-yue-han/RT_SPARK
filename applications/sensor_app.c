/*
 * sensor_app.c - 传感器应用入口（温湿度 + 加速度/陀螺仪）
 *
 * 使用方法：
 *   sensor_init  → 初始化 SHT3X + BNO055（需在系统启动后手动调用）
 *   sensor_test  → 单次读取
 *   sensor_loop  → 循环读取（每秒）
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "sht3x.h"
#include "gy_bn0055.h"

/* 全局数据存储 */
static sht3x_data_t  sht3x_data;
static bn0055_data_t bn0055_data;
static int           g_sensor_ready = 0;   /* 初始化完成标志 */

/**
 * @brief 读取所有传感器并打印
 */
static void sensor_read_all(void)
{
    int ret;

    if (!g_sensor_ready) {
        rt_kprintf("[Sensor] Not initialized! Run 'sensor_init' first.\n");
        return;
    }

    /* 读取 SHT3X */
    ret = sht3x_read(&sht3x_data);
    if (ret == RT_EOK) {
        rt_kprintf("[SHT3X] Temp: %.2f C, Humi: %.2f %%RH\n",
                   (double)sht3x_data.temperature,
                   (double)sht3x_data.humidity);
    } else {
        rt_kprintf("[SHT3X] Read failed (err=%d)\n", ret);
    }

    /* 读取 BN0055 */
    ret = bn0055_read(&bn0055_data);
    if (ret == RT_EOK) {
        rt_kprintf("[BN0055] Accel: %.2f, %.2f, %.2f m/s^2\n",
                   (double)bn0055_data.accel_x,
                   (double)bn0055_data.accel_y,
                   (double)bn0055_data.accel_z);
        rt_kprintf("[BN0055] Gyro:  %.2f, %.2f, %.2f deg/s\n",
                   (double)bn0055_data.gyro_x,
                   (double)bn0055_data.gyro_y,
                   (double)bn0055_data.gyro_z);
        rt_kprintf("[BN0055] Euler: H=%.1f R=%.1f P=%.1f\n",
                   (double)bn0055_data.euler_heading,
                   (double)bn0055_data.euler_roll,
                   (double)bn0055_data.euler_pitch);
    } else {
        rt_kprintf("[BN0055] Read failed\n");
    }
}

/* ================================================================
 * MSH 命令
 * ================================================================ */

/**
 * @brief 初始化传感器（需在 finsh 就绪后手动调用）
 *        msh: sensor_init
 */
static void sensor_init_cmd(void)
{
    int ret;

    if (g_sensor_ready) {
        rt_kprintf("[Sensor] Already initialized\n");
        return;
    }

    rt_kprintf("=== Sensor Init Start ===\n");

    ret = sht3x_init();
    if (ret != RT_EOK) {
        rt_kprintf("[Sensor] SHT3X init FAILED!\n");
    }

    ret = bn0055_init();
    if (ret != RT_EOK) {
        rt_kprintf("[Sensor] BNO055 init FAILED!\n");
    }

    g_sensor_ready = 1;
    rt_kprintf("=== Sensor Init Done ===\n");
    rt_kprintf("Commands: sensor_test, sensor_loop\n");
}
MSH_CMD_EXPORT(sensor_init_cmd, initialize SHT3X and BNO055 sensors);

/**
 * @brief 单次读取
 *        msh: sensor_test
 */
static void sensor_test(void)
{
    rt_kprintf("========== Sensor Test ==========\n");
    sensor_read_all();
    rt_kprintf("=================================\n");
}
MSH_CMD_EXPORT(sensor_test, read all sensors once);

/**
 * @brief 循环读取
 *        msh: sensor_loop
 */
static void sensor_loop(void)
{
    int count = 0;
    rt_kprintf("========== Sensor Loop Start ==========\n");
    while (1) {
        rt_kprintf("[%d] --------\n", ++count);
        sensor_read_all();
        rt_thread_mdelay(1000);
    }
}
MSH_CMD_EXPORT(sensor_loop, loop read all sensors every second);

/* 不再使用 INIT_APP_EXPORT，改为手动 sensor_init 命令 */
