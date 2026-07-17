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
 * 辅助函数：将浮点数转为字符串（避免 rt_snprintf 不支持 %f）
 * ================================================================ */
static int f2s_1(char *buf, float v)   /* 1位小数 */
{
    int sign = (v < 0) ? -1 : 1;
    int int_part = (int)(v * sign);
    int frac = (int)((v * sign - int_part) * 10 + 0.5f);
    if (sign < 0)
        return rt_snprintf(buf, 16, "-%d.%d", int_part, frac);
    else
        return rt_snprintf(buf, 16, "%d.%d", int_part, frac);
}

static int f2s_2(char *buf, float v)   /* 2位小数 */
{
    int sign = (v < 0) ? -1 : 1;
    int int_part = (int)(v * sign);
    int frac = (int)((v * sign - int_part) * 100 + 0.5f);
    if (sign < 0)
        return rt_snprintf(buf, 16, "-%d.%02d", int_part, frac);
    else
        return rt_snprintf(buf, 16, "%d.%02d", int_part, frac);
}

static int f2s_3(char *buf, float v)   /* 3位小数 */
{
    int sign = (v < 0) ? -1 : 1;
    int int_part = (int)(v * sign);
    int frac = (int)((v * sign - int_part) * 1000 + 0.5f);
    if (sign < 0)
        return rt_snprintf(buf, 16, "-%d.%03d", int_part, frac);
    else
        return rt_snprintf(buf, 16, "%d.%03d", int_part, frac);
}

static int f2s_4(char *buf, float v)   /* 4位小数 */
{
    int sign = (v < 0) ? -1 : 1;
    int int_part = (int)(v * sign);
    int frac = (int)((v * sign - int_part) * 10000 + 0.5f);
    if (sign < 0)
        return rt_snprintf(buf, 20, "-%d.%04d", int_part, frac);
    else
        return rt_snprintf(buf, 20, "%d.%04d", int_part, frac);
}

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
    char s_amp[16], s_amp_x[16], s_amp_y[16], s_amp_z[16];
    char s_disp[20], s_freq[16], s_zcr[16], s_rms[16], s_vibr[16], s_torsion[16], s_conf[16];

    f2s_3(s_amp,   feat->amp_dominant);
    f2s_3(s_amp_x, feat->amp_x_pp);
    f2s_3(s_amp_y, feat->amp_y_pp);
    f2s_3(s_amp_z, feat->amp_z_pp);
    f2s_4(s_disp,  feat->displacement_est);
    f2s_3(s_freq,  feat->dominant_freq);
    f2s_1(s_zcr,   feat->zero_cross_rate);
    f2s_3(s_rms,   feat->rms_accel);
    f2s_3(s_vibr,  feat->vibr_energy);
    f2s_2(s_torsion, feat->torsion_deg);
    f2s_2(s_conf,  feat->confidence);

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"galloping\","
        "\"ts\":%lu,"
        "\"state\":\"%s\","
        "\"amp_dominant\":%s,"
        "\"amp_x_pp\":%s,"
        "\"amp_y_pp\":%s,"
        "\"amp_z_pp\":%s,"
        "\"displacement_est\":%s,"
        "\"dominant_freq\":%s,"
        "\"zero_cross_rate\":%s,"
        "\"rms_accel\":%s,"
        "\"vibr_energy\":%s,"
        "\"torsion_deg\":%s,"
        "\"confidence\":%s}\n",
        (unsigned long)rt_tick_get(),
        gd_state_name(state),
        s_amp, s_amp_x, s_amp_y, s_amp_z,
        s_disp, s_freq, s_zcr, s_rms, s_vibr, s_torsion, s_conf);

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
    char s_temp[16], s_humi[16];

    f2s_1(s_temp, temperature);
    f2s_1(s_humi, humidity);

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"env\","
        "\"ts\":%lu,"
        "\"temperature\":%s,"
        "\"humidity\":%s}\n",
        (unsigned long)rt_tick_get(),
        s_temp, s_humi);

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
        motor_state, position);

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
