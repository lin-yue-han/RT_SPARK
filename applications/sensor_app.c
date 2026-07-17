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
#include "dtu_sender.h"

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
        rt_kprintf("[SHT3X] Temp: %d.%02d C, Humi: %d.%02d %%RH\n",
                   (int)sht3x_data.temperature,
                   (int)((sht3x_data.temperature - (int)sht3x_data.temperature) * 100),
                   (int)sht3x_data.humidity,
                   (int)((sht3x_data.humidity - (int)sht3x_data.humidity) * 100));
    } else {
        rt_kprintf("[SHT3X] Read failed (err=%d)\n", ret);
    }

    /* 读取 BN0055 */
    ret = bn0055_read(&bn0055_data);
    if (ret == RT_EOK) {
        rt_kprintf("[BN0055] Acc: %d.%02d,%d.%02d,%d.%02d  Eul: %d.%02d,%d.%02d,%d.%02d\n",
                   (int)bn0055_data.accel_x,
                   (int)((bn0055_data.accel_x - (int)bn0055_data.accel_x) * 100),
                   (int)bn0055_data.accel_y,
                   (int)((bn0055_data.accel_y - (int)bn0055_data.accel_y) * 100),
                   (int)bn0055_data.accel_z,
                   (int)((bn0055_data.accel_z - (int)bn0055_data.accel_z) * 100),
                   (int)bn0055_data.euler_heading,
                   (int)((bn0055_data.euler_heading - (int)bn0055_data.euler_heading) * 100),
                   (int)bn0055_data.euler_roll,
                   (int)((bn0055_data.euler_roll - (int)bn0055_data.euler_roll) * 100),
                   (int)bn0055_data.euler_pitch,
                   (int)((bn0055_data.euler_pitch - (int)bn0055_data.euler_pitch) * 100));
    } else {
        rt_kprintf("[BN0055] Read failed (err=%d)\n", ret);
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
 * 传感器初始化函数（供 main() 调用）
 * ================================================================ */

/* 前向声明 */
void sensor_monitor_start(void);

/**
 * @brief 初始化传感器（由 main() 调用）
 */
void sensor_init_all(void)
{
    int ret;

    rt_kprintf("[Sensor] Init start...\n");

    ret = sht3x_init();
    if (ret != RT_EOK) {
        rt_kprintf("[Sensor] SHT3X init FAILED! (err=%d)\n", ret);
    } else {
        rt_kprintf("[Sensor] SHT3X OK\n");
    }

    /* BNO055 接 UART3(PB10/PB11)，不再与 FinSH 控制台(UART1)冲突 */
    ret = bn0055_init();
    if (ret != RT_EOK) {
        rt_kprintf("[Sensor] BNO055 init FAILED! (err=%d)\n", ret);
    } else {
        rt_kprintf("[Sensor] BNO055 OK\n");
    }

    g_sensor_ready = 1;
    rt_kprintf("[Sensor] Init done.\n");
}

/* ================================================================
 * 实时监控线程（上电自动启动，每秒打印，不阻塞终端）
 * ================================================================ */

static rt_thread_t g_monitor_thread = RT_NULL;
static int g_monitor_running = 0;

static void sensor_monitor_entry(void *parameter)
{
    int count = 0;
    int dtu_ready = 0;

    rt_kprintf("\n");
    rt_kprintf("==================================================\n");
    rt_kprintf("  Sensor Monitor Started (1s interval)\n");
    rt_kprintf("  DTU: sending env data every 5s\n");
    rt_kprintf("==================================================\n\n");

    /* 初始化 DTU */
    if (dtu_sender_init() == RT_EOK) {
        dtu_ready = 1;
        dtu_send_boot();  /* 发送上电消息 */
    } else {
        rt_kprintf("[Monitor] DTU init FAILED, data will not be sent\n");
    }

    while (g_monitor_running) {
        count++;
        rt_kprintf("---- [%d] ----\n", count);

        if (sht3x_read(&sht3x_data) == RT_EOK) {
            rt_kprintf("  [SHT3X] Temp: %d.%02d C  |  Humi: %d.%02d %%RH\n",
                       (int)sht3x_data.temperature,
                       (int)((sht3x_data.temperature - (int)sht3x_data.temperature) * 100),
                       (int)sht3x_data.humidity,
                       (int)((sht3x_data.humidity - (int)sht3x_data.humidity) * 100));

            /* 每 5 秒通过 DTU 发送一次温湿度 */
            if (dtu_ready && (count % 5 == 0)) {
                dtu_send_env(sht3x_data.temperature, sht3x_data.humidity);
            }
        } else {
            rt_kprintf("  [SHT3X] Read FAILED\n");
        }

        /* 读取 BNO055 */
        if (bn0055_read(&bn0055_data) == RT_EOK) {
            rt_kprintf("  [BN0055] Acc: %d.%02d,%d.%02d,%d.%02d (m/s^2)\n",
                       (int)bn0055_data.accel_x,
                       (int)((bn0055_data.accel_x - (int)bn0055_data.accel_x) * 100),
                       (int)bn0055_data.accel_y,
                       (int)((bn0055_data.accel_y - (int)bn0055_data.accel_y) * 100),
                       (int)bn0055_data.accel_z,
                       (int)((bn0055_data.accel_z - (int)bn0055_data.accel_z) * 100));
        } else {
            rt_kprintf("  [BN0055] Read FAILED\n");
        }

        /* 每 10 秒发送一次心跳 */
        if (dtu_ready && (count % 10 == 0)) {
            dtu_send_heartbeat();
        }

        rt_thread_mdelay(1000);
    }

    rt_kprintf("[Monitor] Stopped.\n");
}

/**
 * @brief 启动实时监控线程
 */
void sensor_monitor_start(void)
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
