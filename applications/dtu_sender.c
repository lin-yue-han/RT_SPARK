/*
 * dtu_sender.c - 4G DTU 数据发送模块实现（RNDIS 双串口架构）
 *
 * 修改说明：
 *   原方案通过 UART2 向 4G 模块发送数据，但模块硬件 UART 不通。
 *   新方案：通过 RT-Thread 控制台（ST-Link VCP / COM9）输出 JSON 数据，
 *   由电脑上的 bridge.js 读取 COM9 并通过网络转发到 frp 服务器。
 *
 * 数据流：
 *   STM32 (BNO055/SHT3X) → 控制台 UART1 → ST-Link VCP (COM9) →
 *   bridge.js (COM9 读取) → TCP 客户端 → frp-oil.com:32762 → 网页
 *
 * 设计要点：
 *   1. 使用 rt_console_get_device() 获取控制台设备句柄
 *   2. 使用 rt_device_write() 直接输出 JSON，不需要读取返回
 *   3. 每行一条 JSON，\n 结尾，方便接收端按行解析
 *   4. 使用 rt_snprintf 构造 JSON，避免动态内存分配
 *   5. 发送失败时静默处理，不阻塞主线程
 */

#include "dtu_sender.h"
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>

/* 最大单条 JSON 长度 */
#define DTU_JSON_BUF_SIZE   512

/* 全局控制台设备句柄 */
static rt_device_t g_dtu_console = RT_NULL;

/* ================================================================
 * 初始化
 * ================================================================ */

int dtu_sender_init(void)
{
    if (g_dtu_console != RT_NULL) {
        return RT_EOK;  /* 已初始化 */
    }

    g_dtu_console = rt_console_get_device();
    if (g_dtu_console == RT_NULL) {
        rt_kprintf("[DTU] ERROR: console device not found!\n");
        return -RT_ERROR;
    }

    rt_kprintf("[DTU] Initialized on console (ST-Link VCP) - JSON output mode\n");

    return RT_EOK;
}

int dtu_is_ready(void)
{
    return (g_dtu_console != RT_NULL) ? 1 : 0;
}

/* ================================================================
 * 底层发送
 * ================================================================ */

/**
 * @brief 发送原始数据到控制台设备（ST-Link VCP）
 * @param data  数据指针
 * @param len   数据长度
 * @return 实际发送字节数，<0 表示失败
 */
static int dtu_write(const char *data, int len)
{
    if (g_dtu_console == RT_NULL || data == RT_NULL || len <= 0) {
        return -RT_ERROR;
    }

    int ret = rt_device_write(g_dtu_console, 0, data, len);
    return ret;
}

/* ================================================================
 * 业务数据发送
 * ================================================================ */

int dtu_send_galloping(const gd_feature_t *feat, galloping_state_t state)
{
    if (g_dtu_console == RT_NULL || feat == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"galloping\","
        "\"ts\":%lu,"
        "\"state\":\"%s\","
        "\"amp_dominant\":%.3f,"
        "\"amp_x_pp\":%.3f,"
        "\"amp_y_pp\":%.3f,"
        "\"amp_z_pp\":%.3f,"
        "\"displacement_est\":%.4f,"
        "\"dominant_freq\":%.3f,"
        "\"zero_cross_rate\":%.1f,"
        "\"rms_accel\":%.3f,"
        "\"vibr_energy\":%.3f,"
        "\"torsion_deg\":%.2f,"
        "\"confidence\":%.2f}\n",
        (unsigned long)rt_tick_get(),
        gd_state_name(state),
        (double)feat->amp_dominant,
        (double)feat->amp_x_pp,
        (double)feat->amp_y_pp,
        (double)feat->amp_z_pp,
        (double)feat->displacement_est,
        (double)feat->dominant_freq,
        (double)feat->zero_cross_rate,
        (double)feat->rms_accel,
        (double)feat->vibr_energy,
        (double)feat->torsion_deg,
        (double)feat->confidence);

    if (len < 0 || len >= (int)sizeof(buf)) {
        rt_kprintf("[DTU] JSON overflow (galloping)\n");
        return -RT_ERROR;
    }

    return dtu_write(buf, len);
}

int dtu_send_env(float temperature, float humidity)
{
    if (g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"env\","
        "\"ts\":%lu,"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f}\n",
        (unsigned long)rt_tick_get(),
        (double)temperature,
        (double)humidity);

    if (len < 0 || len >= (int)sizeof(buf)) {
        return -RT_ERROR;
    }

    return dtu_write(buf, len);
}

int dtu_send_motor(const char *motor_state, int position)
{
    if (g_dtu_console == RT_NULL || motor_state == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"motor\","
        "\"ts\":%lu,"
        "\"motor_state\":\"%s\","
        "\"position\":%d}\n",
        (unsigned long)rt_tick_get(),
        motor_state,
        position);

    if (len < 0 || len >= (int)sizeof(buf)) {
        return -RT_ERROR;
    }

    return dtu_write(buf, len);
}

int dtu_send_heartbeat(void)
{
    if (g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"heartbeat\",\"ts\":%lu}\n",
        (unsigned long)rt_tick_get());

    if (len < 0 || len >= (int)sizeof(buf)) {
        return -RT_ERROR;
    }

    return dtu_write(buf, len);
}

int dtu_send_boot(void)
{
    if (g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"boot\","
        "\"ts\":%lu,"
        "\"msg\":\"RT_SPARK system started\","
        "\"version\":\"1.0\"}\n",
        (unsigned long)rt_tick_get());

    if (len < 0 || len >= (int)sizeof(buf)) {
        return -RT_ERROR;
    }

    return dtu_write(buf, len);
}
