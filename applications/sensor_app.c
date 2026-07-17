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
sht3x_data_t  sht3x_data;
bn0055_data_t bn0055_data;
int           g_sensor_ready = 0;   /* 初始化完成标志 */

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

/* ================================================================
 * 上电自动初始化（INIT_APP_EXPORT 会在 main() 之后自动调用）
 * ================================================================ */

/**
 * @brief 上电自动初始化传感器
 *        无需手动输入 sensor_init 命令
 * @return 0 成功（INIT_APP_EXPORT 约定返回值）
 */
static int sensor_auto_init(void)
{
    int ret;

    rt_kprintf("[AutoInit] Sensor init start...\n");

    ret = sht3x_init();
    if (ret != RT_EOK) {
        rt_kprintf("[AutoInit] SHT3X init FAILED! (err=%d)\n", ret);
    } else {
        rt_kprintf("[AutoInit] SHT3X OK\n");
    }

    ret = bn0055_init();
    if (ret != RT_EOK) {
        rt_kprintf("[AutoInit] BNO055 init FAILED! (err=%d)\n", ret);
    } else {
        rt_kprintf("[AutoInit] BNO055 OK\n");
    }

    g_sensor_ready = 1;
    rt_kprintf("[AutoInit] Sensor init done. ready=%d\n", g_sensor_ready);

    /* 启动实时打印线程（每秒输出传感器数据） */
    sensor_monitor_start();

    return 0;
}
INIT_APP_EXPORT(sensor_auto_init);

/* ================================================================
 * 实时监控线程（上电自动启动，每秒打印，不阻塞终端）
 * ================================================================ */

static rt_thread_t g_monitor_thread = RT_NULL;
static int g_monitor_running = 0;

static void sensor_monitor_entry(void *parameter)
{
    int count = 0;

    rt_kprintf("\n");
    rt_kprintf("==================================================\n");
    rt_kprintf("  Sensor Monitor Started (1s interval)\n");
    rt_kprintf("  Type 'sensor_monitor_stop' to stop\n");
    rt_kprintf("==================================================\n\n");

    while (g_monitor_running) {
        count++;
        rt_kprintf("---- [%d] ----\n", count);

        if (sht3x_read(&sht3x_data) == RT_EOK) {
            rt_kprintf("  [SHT3X] Temp: %.2f C  |  Humi: %.2f %%RH\n",
                       (double)sht3x_data.temperature,
                       (double)sht3x_data.humidity);
        } else {
            rt_kprintf("  [SHT3X] Read FAILED\n");
        }

        if (bn0055_read(&bn0055_data) == RT_EOK) {
            rt_kprintf("  [BNO055] Accel X: %+.2f  Y: %+.2f  Z: %+.2f m/s^2\n",
                       (double)bn0055_data.accel_x,
                       (double)bn0055_data.accel_y,
                       (double)bn0055_data.accel_z);
            rt_kprintf("  [BNO055] Gyro  X: %+.2f  Y: %+.2f  Z: %+.2f deg/s\n",
                       (double)bn0055_data.gyro_x,
                       (double)bn0055_data.gyro_y,
                       (double)bn0055_data.gyro_z);
            rt_kprintf("  [BNO055] Euler H: %+.1f  R: %+.1f  P: %+.1f deg\n",
                       (double)bn0055_data.euler_heading,
                       (double)bn0055_data.euler_roll,
                       (double)bn0055_data.euler_pitch);
        } else {
            rt_kprintf("  [BNO055] Read FAILED\n");
        }

        rt_thread_mdelay(1000);
    }

    rt_kprintf("[Monitor] Stopped.\n");
}

/**
 * @brief 启动实时监控线程
 */
static void sensor_monitor_start(void)
{
    if (g_monitor_running) {
        rt_kprintf("[Monitor] Already running\n");
        return;
    }

    g_monitor_running = 1;
    g_monitor_thread = rt_thread_create(
        "sensor_mon",
        sensor_monitor_entry,
        RT_NULL,
        2048,
        RT_THREAD_PRIORITY_MAX - 3,
        20);

    if (g_monitor_thread != RT_NULL) {
        rt_thread_startup(g_monitor_thread);
    } else {
        rt_kprintf("[Monitor] Thread create FAILED\n");
        g_monitor_running = 0;
    }
}

/**
 * @brief 停止监控线程
 *        msh: sensor_monitor_stop
 */
static void sensor_monitor_stop(void)
{
    g_monitor_running = 0;
    rt_kprintf("[Monitor] Stopping...\n");
}
MSH_CMD_EXPORT(sensor_monitor_stop, stop real-time sensor monitor);

/**
 * @brief 重新启动监控
 *        msh: sensor_monitor
 */
static void sensor_monitor_cmd(void)
{
    sensor_monitor_start();
}
MSH_CMD_EXPORT(sensor_monitor_cmd, start real-time sensor monitor);
