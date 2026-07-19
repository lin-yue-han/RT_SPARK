/*
 * dtu_sender.c - 4G DTU 数据发送模块实现（无线透传模式）
 *
 * 修改说明：
 *   本模块通过 UART2 向 Air778E 4G 模块发送 JSON 数据，
 *   由 Air778E 通过 TCP 透传转发到 frp 服务器，最终到达网页端。
 *   若 UART2 不可用，则回退到控制台 UART1（仅用于调试）。
 *
 * 数据流（无线）：
 *   STM32 (BNO055/SHT3X) → 控制台 UART2 → Air778E → 4G → frp → bridge.js → 网页
 *
 * 设计要点：
 *   1. 优先使用 UART2（Air778E 4G 模块）输出 JSON
 *   2. 使用 rt_snprintf 构造 JSON，避免动态内存分配
 *   3. 每行一条 JSON，\n 结尾，方便接收端按行解析
 *   4. 发送失败时静默处理，不阻塞主线程
 */

#include "dtu_sender.h"
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>

/* 最大单条 JSON 长度 */
#define DTU_JSON_BUF_SIZE   512

/* 全局控制台设备句柄 */
static rt_device_t g_dtu_console = RT_NULL;
extern volatile int g4_config_busy;
static rt_mutex_t g_dtu_lock = RT_NULL;
static int g_dtu_at_mode = 0;
static int g_dtu_tcp_connected = 0;
static int g_dtu_rx_thread_started = 0;
static char g_dtu_downlink_buf[512];
static int g_dtu_downlink_len = 0;
static char g_dtu_pending_cmds[6][16];
static volatile int g_dtu_pending_head = 0;
static volatile int g_dtu_pending_tail = 0;
static volatile int g_dtu_pending_count = 0;

#define DTU_REMOTE_HOST "frp-oil.com"
#define DTU_REMOTE_PORT 32762

extern void start_motor_with_timeout(int dir);

static void dtu_exec_remote_cmd(const char *cmd)
{
    if (cmd == RT_NULL || cmd[0] == '\0') {
        return;
    }

    rt_kprintf("[DTU-CMD] RX remote command: %s\n", cmd);

    if (rt_strcmp(cmd, "forward") == 0) {
        start_motor_with_timeout(1);
        dtu_send_motor("forward", 0);
        rt_kprintf("[DTU-CMD] -> FORWARD executed (wireless)\n");
    } else if (rt_strcmp(cmd, "backward") == 0) {
        start_motor_with_timeout(-1);
        dtu_send_motor("backward", 0);
        rt_kprintf("[DTU-CMD] -> BACKWARD executed (wireless)\n");
    } else if (rt_strcmp(cmd, "stop") == 0) {
        start_motor_with_timeout(0);
        dtu_send_motor("stop", 0);
        rt_kprintf("[DTU-CMD] -> STOP executed (wireless)\n");
    } else {
        rt_kprintf("[DTU-CMD] Unknown remote command: %s\n", cmd);
    }
}

static void dtu_trim_cmd(char *cmd)
{
    int len;

    if (cmd == RT_NULL) {
        return;
    }

    len = rt_strlen(cmd);
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n' ||
                       cmd[len - 1] == ' '  || cmd[len - 1] == '\t')) {
        cmd[--len] = '\0';
    }
}

static int dtu_is_known_cmd(const char *cmd)
{
    return (cmd != RT_NULL) &&
           (rt_strcmp(cmd, "forward") == 0 ||
            rt_strcmp(cmd, "backward") == 0 ||
            rt_strcmp(cmd, "stop") == 0);
}

static void dtu_queue_remote_cmd(const char *cmd)
{
    if (!dtu_is_known_cmd(cmd)) {
        return;
    }

    if (g_dtu_pending_count >= 6) {
        rt_kprintf("[DTU-CMD] Queue full, drop command: %s\n", cmd);
        return;
    }

    rt_strncpy(g_dtu_pending_cmds[g_dtu_pending_tail], cmd, sizeof(g_dtu_pending_cmds[0]) - 1);
    g_dtu_pending_cmds[g_dtu_pending_tail][sizeof(g_dtu_pending_cmds[0]) - 1] = '\0';
    g_dtu_pending_tail = (g_dtu_pending_tail + 1) % 6;
    g_dtu_pending_count++;
    rt_kprintf("[DTU-CMD] Queued remote command: %s (count=%d)\n", cmd, g_dtu_pending_count);
}

static void dtu_exec_pending_cmd(void)
{
    char cmd[16];

    while (g_dtu_pending_count > 0) {
        rt_strncpy(cmd, g_dtu_pending_cmds[g_dtu_pending_head], sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        g_dtu_pending_head = (g_dtu_pending_head + 1) % 6;
        g_dtu_pending_count--;
        dtu_exec_remote_cmd(cmd);
    }
}

static int dtu_parse_ipd_len(const char *p, int *header_len)
{
    int len = 0;
    int i = 0;

    if (p == RT_NULL || header_len == RT_NULL || rt_strncmp(p, "+IPD,", 5) != 0) {
        return -1;
    }

    i = 5;
    while (p[i] >= '0' && p[i] <= '9') {
        len = len * 10 + (p[i] - '0');
        i++;
    }

    if (p[i] != ':' || len <= 0) {
        return -1;
    }

    *header_len = i + 1;
    return len;
}

static void dtu_process_downlink_text(const char *text)
{
    const char *p;
    char cmd[64];
    int i;
    int cmd_len = 0;

    if (text == RT_NULL || text[0] == '\0') {
        return;
    }

    if (rt_strstr(text, "CLOSED") != RT_NULL || rt_strstr(text, "CLOSE OK") != RT_NULL) {
        g_dtu_tcp_connected = 0;
        rt_kprintf("[DTU-AT] TCP closed by module/server\n");
    }

    p = text;
    while ((p = rt_strstr(p, "+IPD,")) != RT_NULL) {
        int header_len = 0;
        int payload_len = dtu_parse_ipd_len(p, &header_len);
        if (payload_len > 0) {
            const char *payload = p + header_len;
            int copy_len = payload_len;
            if (copy_len >= (int)sizeof(cmd)) {
                copy_len = sizeof(cmd) - 1;
            }
            rt_memcpy(cmd, payload, copy_len);
            cmd[copy_len] = '\0';
            dtu_trim_cmd(cmd);
            if (dtu_is_known_cmd(cmd)) {
                dtu_queue_remote_cmd(cmd);
            } else if (cmd[0] != '\0') {
                rt_kprintf("[DTU-CMD] Ignored IPD payload: %s\n", cmd);
            }
            p = payload + payload_len;
        } else {
            p += 5;
        }
    }

    for (i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch == '\r' || ch == '\n' || ch == '>' || ch == ' ') {
            if (cmd_len > 0) {
                cmd[cmd_len] = '\0';
                dtu_trim_cmd(cmd);
                if (dtu_is_known_cmd(cmd)) {
                    dtu_queue_remote_cmd(cmd);
                }
                cmd_len = 0;
            }
        } else if (cmd_len < (int)sizeof(cmd) - 1) {
            cmd[cmd_len++] = ch;
        } else {
            cmd_len = 0;
        }
    }
    if (cmd_len > 0) {
        cmd[cmd_len] = '\0';
        dtu_trim_cmd(cmd);
        if (dtu_is_known_cmd(cmd)) {
            dtu_queue_remote_cmd(cmd);
        }
    }
}

static void dtu_downlink_append_and_parse(const char *data, int len)
{
    int copy_len;

    if (data == RT_NULL || len <= 0) {
        return;
    }

    if (len >= (int)sizeof(g_dtu_downlink_buf)) {
        data += len - ((int)sizeof(g_dtu_downlink_buf) - 1);
        len = sizeof(g_dtu_downlink_buf) - 1;
        g_dtu_downlink_len = 0;
    }

    if (g_dtu_downlink_len + len >= (int)sizeof(g_dtu_downlink_buf)) {
        int keep = sizeof(g_dtu_downlink_buf) / 2;
        rt_memmove(g_dtu_downlink_buf, g_dtu_downlink_buf + g_dtu_downlink_len - keep, keep);
        g_dtu_downlink_len = keep;
    }

    copy_len = len;
    rt_memcpy(g_dtu_downlink_buf + g_dtu_downlink_len, data, copy_len);
    g_dtu_downlink_len += copy_len;
    g_dtu_downlink_buf[g_dtu_downlink_len] = '\0';

    dtu_process_downlink_text(g_dtu_downlink_buf);

    if (rt_strstr(g_dtu_downlink_buf, "+IPD,") != RT_NULL &&
        rt_strstr(g_dtu_downlink_buf, ":") != RT_NULL) {
        g_dtu_downlink_len = 0;
        g_dtu_downlink_buf[0] = '\0';
        return;
    }

    if (g_dtu_downlink_len > 300) {
        int keep = 160;
        rt_memmove(g_dtu_downlink_buf, g_dtu_downlink_buf + g_dtu_downlink_len - keep, keep);
        g_dtu_downlink_len = keep;
        g_dtu_downlink_buf[g_dtu_downlink_len] = '\0';
    }
}

static void dtu_poll_downlink_once(void)
{
    char rx[128];
    int n;

    if (!g_dtu_at_mode || g_dtu_console == RT_NULL || g_dtu_lock == RT_NULL) {
        return;
    }

    if (rt_mutex_take(g_dtu_lock, 0) != RT_EOK) {
        return;
    }

    n = rt_device_read(g_dtu_console, 0, rx, sizeof(rx) - 1);
    if (n > 0) {
        rx[n] = '\0';
    }

    rt_mutex_release(g_dtu_lock);

    if (n > 0) {
        rt_kprintf("[DTU-AT] DOWNLINK RX: %s\n", rx);
        dtu_downlink_append_and_parse(rx, n);
        dtu_exec_pending_cmd();
    }
}

static void dtu_downlink_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_kprintf("[DTU-CMD] Downlink parser started (+IPD -> motor command)\n");

    while (1) {
        dtu_poll_downlink_once();
        rt_thread_mdelay(100);
    }
}

static void dtu_start_downlink_thread(void)
{
    rt_thread_t tid;

    if (g_dtu_rx_thread_started) {
        return;
    }

    tid = rt_thread_create("dtu_rx",
                           dtu_downlink_thread_entry,
                           RT_NULL,
                           2048,
                           16,
                           10);
    if (tid != RT_NULL) {
        g_dtu_rx_thread_started = 1;
        rt_thread_startup(tid);
    } else {
        rt_kprintf("[DTU-CMD] ERROR: create downlink parser thread failed\n");
    }
}

static int dtu_read_for(char *buf, int max_len, int timeout_ms)
{
    rt_tick_t end_tick;
    int total = 0;

    if (buf == RT_NULL || max_len <= 1 || g_dtu_console == RT_NULL) {
        return 0;
    }

    end_tick = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    buf[0] = '\0';

    while ((rt_int32_t)(end_tick - rt_tick_get()) > 0 && total < max_len - 1) {
        char ch;
        int n = rt_device_read(g_dtu_console, 0, &ch, 1);
        if (n == 1) {
            buf[total++] = ch;
            buf[total] = '\0';
        } else {
            rt_thread_mdelay(20);
        }
    }

    return total;
}

static void dtu_drain_rx(void)
{
    char tmp[32];
    rt_tick_t end_tick = rt_tick_get() + rt_tick_from_millisecond(200);

    while ((rt_int32_t)(end_tick - rt_tick_get()) > 0) {
        if (rt_device_read(g_dtu_console, 0, tmp, sizeof(tmp)) <= 0) {
            rt_thread_mdelay(20);
        }
    }
}

static int dtu_at_cmd(const char *cmd, int timeout_ms, const char *expect1, const char *expect2)
{
    char rx[256];
    int n;

    if (cmd == RT_NULL || g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    dtu_drain_rx();
    rt_kprintf("[DTU-AT] TX: %s\n", cmd);
    rt_device_write(g_dtu_console, 0, cmd, rt_strlen(cmd));
    rt_device_write(g_dtu_console, 0, "\r\n", 2);

    n = dtu_read_for(rx, sizeof(rx), timeout_ms);
    if (n > 0) {
        rt_kprintf("[DTU-AT] RX: %s\n", rx);
        dtu_process_downlink_text(rx);
    } else {
        rt_kprintf("[DTU-AT] RX: <none>\n");
    }

    if (expect1 != RT_NULL && rt_strstr(rx, expect1) != RT_NULL) {
        return RT_EOK;
    }
    if (expect2 != RT_NULL && rt_strstr(rx, expect2) != RT_NULL) {
        return RT_EOK;
    }

    return -RT_ERROR;
}

static int dtu_at_connect(void)
{
    char cmd[96];
    int i;

    if (g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    for (i = 0; i < 3; i++) {
        if (dtu_at_cmd("AT", 1200, "OK", RT_NULL) == RT_EOK) {
            break;
        }
        rt_thread_mdelay(1000);
    }
    if (i >= 3) {
        rt_kprintf("[DTU-AT] ERROR: module no AT response\n");
        return -RT_ERROR;
    }

    dtu_at_cmd("ATE0", 1200, "OK", RT_NULL);
    dtu_at_cmd("AT+CPIN?", 1500, "READY", "OK");
    dtu_at_cmd("AT+CSQ", 1500, "OK", RT_NULL);
    dtu_at_cmd("AT+CGATT?", 1500, "OK", RT_NULL);

    dtu_at_cmd("AT+CIPSHUT", 6000, "SHUT OK", "OK");
    dtu_at_cmd("AT+CIPMUX=0", 2000, "OK", RT_NULL);
    dtu_at_cmd("AT+CIPMODE=0", 2000, "OK", RT_NULL);
    dtu_at_cmd("AT+CSTT", 4000, "OK", RT_NULL);
    dtu_at_cmd("AT+CIICR", 10000, "OK", RT_NULL);
    dtu_at_cmd("AT+CIFSR", 4000, ".", RT_NULL);

    rt_snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d", DTU_REMOTE_HOST, DTU_REMOTE_PORT);
    if (dtu_at_cmd(cmd, 15000, "CONNECT OK", "ALREADY CONNECT") == RT_EOK) {
        g_dtu_tcp_connected = 1;
        rt_kprintf("[DTU-AT] TCP connected to %s:%d\n", DTU_REMOTE_HOST, DTU_REMOTE_PORT);
        return RT_EOK;
    }

    g_dtu_tcp_connected = 0;
    rt_kprintf("[DTU-AT] ERROR: TCP connect failed\n");
    return -RT_ERROR;
}

static int dtu_at_send_payload(const char *data, int len)
{
    char cmd[32];
    char rx[256];
    int n;

    if (data == RT_NULL || len <= 0 || g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    if (!g_dtu_tcp_connected) {
        if (dtu_at_connect() != RT_EOK) {
            return -RT_ERROR;
        }
    }

    dtu_drain_rx();
    rt_snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    rt_kprintf("[DTU-AT] TX: %s\n", cmd);
    rt_device_write(g_dtu_console, 0, cmd, rt_strlen(cmd));
    rt_device_write(g_dtu_console, 0, "\r\n", 2);

    n = dtu_read_for(rx, sizeof(rx), 5000);
    if (n > 0) {
        rt_kprintf("[DTU-AT] RX: %s\n", rx);
        dtu_process_downlink_text(rx);
    } else {
        rt_kprintf("[DTU-AT] RX: <no prompt>\n");
    }

    if (rt_strstr(rx, ">") == RT_NULL) {
        rt_kprintf("[DTU-AT] ERROR: CIPSEND prompt missing\n");
        g_dtu_tcp_connected = 0;
        return -RT_ERROR;
    }

    rt_kprintf("[DTU-AT] SEND JSON len=%d\n", len);
    rt_device_write(g_dtu_console, 0, data, len);

    n = dtu_read_for(rx, sizeof(rx), 8000);
    if (n > 0) {
        rt_kprintf("[DTU-AT] RX: %s\n", rx);
        dtu_process_downlink_text(rx);
    } else {
        rt_kprintf("[DTU-AT] RX: <no send result>\n");
    }

    if (rt_strstr(rx, "SEND OK") != RT_NULL) {
        return len;
    }

    g_dtu_tcp_connected = 0;
    rt_kprintf("[DTU-AT] ERROR: payload send failed\n");
    return -RT_ERROR;
}

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
    if (g_dtu_lock == RT_NULL) {
        g_dtu_lock = rt_mutex_create("dtu_at", RT_IPC_FLAG_PRIO);
        if (g_dtu_lock == RT_NULL) {
            rt_kprintf("[DTU] ERROR: mutex create failed\n");
            return -RT_ERROR;
        }
    }

    rt_mutex_take(g_dtu_lock, RT_WAITING_FOREVER);

    if (g_dtu_console != RT_NULL) {
        rt_mutex_release(g_dtu_lock);
        return RT_EOK;  /* 已初始化 */
    }

    /* 优先尝试 UART2（Air778E 4G 模块），无线模式 */
    g_dtu_console = rt_device_find("uart2");
    if (g_dtu_console != RT_NULL) {
        struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
        cfg.baud_rate = BAUD_RATE_115200;
        cfg.data_bits = DATA_BITS_8;
        cfg.stop_bits = STOP_BITS_1;
        cfg.parity = PARITY_NONE;
        rt_device_control(g_dtu_console, RT_DEVICE_CTRL_CONFIG, &cfg);
        if (rt_device_open(g_dtu_console, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
            rt_kprintf("[DTU] ERROR: open UART2 failed\n");
            g_dtu_console = RT_NULL;
            rt_mutex_release(g_dtu_lock);
            return -RT_ERROR;
        }
        g_dtu_at_mode = 1;
        rt_kprintf("[DTU] Initialized on UART2 AT TCP mode (%s:%d)\n", DTU_REMOTE_HOST, DTU_REMOTE_PORT);
        if (dtu_at_connect() != RT_EOK) {
            rt_kprintf("[DTU] WARNING: initial AT TCP connect failed, will retry on send\n");
        }
        rt_mutex_release(g_dtu_lock);
        dtu_start_downlink_thread();
        return RT_EOK;
    }

    /* UART2 不可用，回退到控制台 UART1（ST-Link VCP，有线模式） */
    g_dtu_console = rt_console_get_device();
    if (g_dtu_console == RT_NULL) {
        rt_kprintf("[DTU] ERROR: neither uart2 nor console device found!\n");
        rt_mutex_release(g_dtu_lock);
        return -RT_ERROR;
    }

    rt_kprintf("[DTU] Initialized on console (ST-Link VCP) - wired JSON mode\n");

    rt_mutex_release(g_dtu_lock);
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
    int ret;

    if (g_dtu_console == RT_NULL || data == RT_NULL || len <= 0) {
        return -RT_ERROR;
    }

    if (g4_config_busy) {
        return len;
    }

    if (g_dtu_at_mode) {
        if (g_dtu_lock != RT_NULL) {
            rt_mutex_take(g_dtu_lock, RT_WAITING_FOREVER);
        }
        ret = dtu_at_send_payload(data, len);
        if (g_dtu_lock != RT_NULL) {
            rt_mutex_release(g_dtu_lock);
        }
        dtu_exec_pending_cmd();
        return ret;
    }

    ret = rt_device_write(g_dtu_console, 0, data, len);
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

int dtu_send_raw_accel(float ax, float ay, float az)
{
    if (g_dtu_console == RT_NULL) {
        return -RT_ERROR;
    }

    char buf[DTU_JSON_BUF_SIZE];
    char s_ax[16], s_ay[16], s_az[16];

    f2s_3(s_ax, ax);
    f2s_3(s_ay, ay);
    f2s_3(s_az, az);

    int len = rt_snprintf(buf, sizeof(buf),
        "{\"type\":\"raw_accel\","
        "\"ts\":%lu,"
        "\"ax\":%s,"
        "\"ay\":%s,"
        "\"az\":%s}\n",
        (unsigned long)rt_tick_get(),
        s_ax, s_ay, s_az);

    if (len < 0 || len >= (int)sizeof(buf)) {
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
