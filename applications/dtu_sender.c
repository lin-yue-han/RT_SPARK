/*
 * dtu_sender.c - 4G DTU 数据发送模块实现
 *
 * 通过 RT-Thread 的 UART 设备框架向银尔达 Core-Y100M DTU 模块发送 JSON 行数据。
 *
 * 硬件连接：STM32 UART2 (PA2-TX, PA3-RX) → 银尔达 DTU 串口
 * DTU 固件：银尔达 DTU 透传固件（私有 config,set,tcp 命令，非标准 AT）
 *
 * 重要说明：
 *   DTU 的 TCP 配置是一次性的 —— 用 USB-TTL 串口工具发送以下命令后，
 *   DTU 上电即自动连接 frp-oil.com:32762，无需 STM32 参与配置：
 *
 *     config,set,tcp,1,ttluart,0,0,,0,frp-oil.com,32762,0,,0,,0,0,0,0,0
 *     config,set,save
 *     config,set,reboot
 *
 *   STM32 只需通过 UART2 发送纯 JSON 行数据（\n 结尾），
 *   DTU 会自动透传到远端 TCP Server。
 *
 * 设计要点：
 *   1. 使用 rt_device_write() 直接发送，只写模式（不需要读取 DTU 返回）
 *   2. 每行一条 JSON，\n 结尾，方便接收端按行解析
 *   3. 使用 rt_snprintf 构造 JSON，避免动态内存分配
 *   4. 所有浮点数保留足够精度（%.3f/%.1f），减少冗余
 *   5. 发送失败时静默处理，不阻塞主线程
 */

#include "dtu_sender.h"
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>

/* 最大单条 JSON 长度 */
#define DTU_JSON_BUF_SIZE   512

/* 全局 UART 设备句柄 */
static rt_device_t g_dtu_uart = RT_NULL;

/* ================================================================
 * 初始化
 * ================================================================ */

int dtu_sender_init(void)
{
    if (g_dtu_uart != RT_NULL) {
        return RT_EOK;  /* 已初始化 */
    }

    g_dtu_uart = rt_device_find(DTU_UART_NAME);
    if (g_dtu_uart == RT_NULL) {
        rt_kprintf("[DTU] ERROR: device '%s' not found!\n", DTU_UART_NAME);
        return -RT_ERROR;
    }

    /*
     * 只写模式：
     * DTU 透传固件自动管理 TCP 连接/重连/心跳，
     * STM32 不需要读取 DTU 的返回数据。
     */
    if (rt_device_open(g_dtu_uart, RT_DEVICE_OFLAG_WRONLY) != RT_EOK) {
        rt_kprintf("[DTU] ERROR: failed to open '%s'!\n", DTU_UART_NAME);
        g_dtu_uart = RT_NULL;
        return -RT_ERROR;
    }

    /* 配置波特率（需与 DTU 固件中 ttluart 波特率一致） */
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = DTU_BAUD_RATE;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_dtu_uart, RT_DEVICE_CTRL_CONFIG, &cfg);

    rt_kprintf("[DTU] Initialized on %s @ %d baud (transparent mode, write-only)\n",
               DTU_UART_NAME, DTU_BAUD_RATE);

    /* DTU 配置已通过串口工具一次性写入，上电自动连接，无需额外配置 */
    return RT_EOK;
}

int dtu_is_ready(void)
{
    return (g_dtu_uart != RT_NULL) ? 1 : 0;
}

/* ================================================================
 * 底层发送
 * ================================================================ */

/**
 * @brief 发送原始数据到 DTU 模块
 * @param data  数据指针
 * @param len   数据长度
 * @return 实际发送字节数，<0 表示失败
 */
static int dtu_write(const char *data, int len)
{
    if (g_dtu_uart == RT_NULL || data == RT_NULL || len <= 0) {
        return -RT_ERROR;
    }

    int ret = rt_device_write(g_dtu_uart, 0, data, len);
    return ret;
}

/* ================================================================
 * 业务数据发送
 * ================================================================ */

int dtu_send_galloping(const gd_feature_t *feat, galloping_state_t state)
{
    if (g_dtu_uart == RT_NULL || feat == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    /*
     * JSON 格式：
     * {
     *   "type":"galloping",
     *   "ts":<rt_tick>,
     *   "state":"<STATE>",
     *   "amp_dominant":<float>,
     *   "amp_x_pp":<float>,
     *   "amp_y_pp":<float>,
     *   "amp_z_pp":<float>,
     *   "displacement_est":<float>,
     *   "dominant_freq":<float>,
     *   "zero_cross_rate":<float>,
     *   "rms_accel":<float>,
     *   "vibr_energy":<float>,
     *   "torsion_deg":<float>,
     *   "confidence":<float>
     * }
     */
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
    if (g_dtu_uart == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    /*
     * JSON 格式：
     *   {"type":"env","ts":<tick>,"temperature":<float>,"humidity":<float>}
     */
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
    if (g_dtu_uart == RT_NULL || motor_state == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];

    /*
     * JSON 格式：
     *   {"type":"motor","ts":<tick>,"motor_state":"<state>","position":<int>}
     */
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
    if (g_dtu_uart == RT_NULL) {
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
    if (g_dtu_uart == RT_NULL) {
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
