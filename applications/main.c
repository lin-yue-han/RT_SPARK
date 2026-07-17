/*
 * main.c - RT_SPARK 全量自动探测固件 v5
 *
 * 上电后自动执行多种探测序列，探测 DTU 模块实际固件类型和协议。
 * 所有结果通过 UART1 输出到 COM9，SSCOM 查看。
 *
 * 探测项目：
 *   1. 多种波特率（4800/9600/19200/38400/57600/115200）
 *   2. 多种命令格式（大小写/有无CRLF/有无延迟）
 *   3. 标准 AT 固件探测
 *   4. LuatOS 交互探测
 *   5. 银尔达 DTU 探测
 *   6. 十六进制唤醒序列探测
 *   7. 流控引脚状态测试
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdio.h>

#define UART1_NAME      "uart1"
#define UART2_NAME      "uart2"
#define BUF_SIZE        256
#define RESP_TIMEOUT_MS 2000

static rt_device_t g_uart2 = RT_NULL;

/* 探测结果缓冲区 */
static char g_rx_buf[BUF_SIZE];

/* ================================================================
 * 基础工具函数
 * ================================================================ */

static void bridge_print(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rt_device_t console = rt_console_get_device();
    if (console) {
        rt_device_write(console, 0, buf, rt_strlen(buf));
    }
}

static void hex_dump(const char *label, const rt_uint8_t *data, int len)
{
    bridge_print("[RX] %s len=%d: ", label, len);
    for (int i = 0; i < len; i++) {
        bridge_print("%02X ", data[i]);
    }
    bridge_print("| ");
    for (int i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] < 127) {
            bridge_print("%c", data[i]);
        } else {
            bridge_print(".");
        }
    }
    bridge_print("\n");
}

static void uart2_set_baud(int baud)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = baud;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_uart2, RT_DEVICE_CTRL_CONFIG, &cfg);
    bridge_print("[CFG] UART2 baud=%d\n", baud);
}

static void uart2_send_raw(const rt_uint8_t *data, int len)
{
    if (g_uart2) rt_device_write(g_uart2, 0, data, len);
}

static void uart2_send_str(const char *str)
{
    if (g_uart2) rt_device_write(g_uart2, 0, str, rt_strlen(str));
}

static int uart2_recv_wait(rt_uint8_t *buf, int max_len, int timeout_ms)
{
    if (!g_uart2) return 0;
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout_ticks = rt_tick_from_millisecond(timeout_ms);
    int idx = 0;

    while ((rt_tick_get() - start) < timeout_ticks) {
        rt_uint8_t ch;
        while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {
            if (idx < max_len) buf[idx++] = ch;
        }
        if (idx > 0) {
            /* 再等一小段时间看看有没有更多数据 */
            rt_thread_mdelay(100);
            while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {
                if (idx < max_len) buf[idx++] = ch;
            }
            return idx;
        }
        rt_thread_mdelay(50);
    }
    return 0;
}

static void flush_uart2(void)
{
    rt_uint8_t ch;
    while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {}
}

/* ================================================================
 * 探测序列
 * ================================================================ */

static void probe_at_variants(int baud)
{
    uart2_set_baud(baud);
    rt_thread_mdelay(500);
    flush_uart2();

    bridge_print("\n===== AT 变体探测 @ %d =====\n", baud);

    const char *variants[] = {
        "AT\r\n",           /* 标准 */
        "AT\r",             /* 只有 CR */
        "AT\n",             /* 只有 LF */
        "AT",               /* 无换行 */
        "at\r\n",           /* 小写 */
        "At\r\n",           /* 首字母大写 */
        "aT\r\n",           /* 只有 T 大写 */
        "AT\r\nAT\r\n",     /* 连续两个 */
        "AT\r\nAT\r\nAT\r\nAT\r\nAT\r\nAT\r\nAT\r\nAT\r\n", /* 8 个 */
        "AT+CGMR\r\n",      /* 查询版本 */
        "AT+CFUN=1\r\n",    /* 开启射频 */
        "AT+CPIN?\r\n",     /* SIM 卡 */
        "AT+CSQ\r\n",       /* 信号 */
        "AT+CREG?\r\n",     /* 注册 */
        "AT+IPR=115200\r\n",/* 设波特率 */
    };

    for (int i = 0; i < sizeof(variants)/sizeof(variants[0]); i++) {
        flush_uart2();
        rt_thread_mdelay(100);
        uart2_send_str(variants[i]);
        bridge_print("[TX%2d] ", i);
        hex_dump("", (rt_uint8_t*)variants[i], rt_strlen(variants[i]));

        rt_uint8_t rx[64];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 1500);
        if (rx_len > 0) {
            hex_dump("REPLY", rx, rx_len);
        } else {
            bridge_print("[RX%2d] NO RESPONSE\n", i);
        }
    }
}

static void probe_luatos(int baud)
{
    uart2_set_baud(baud);
    rt_thread_mdelay(500);
    flush_uart2();

    bridge_print("\n===== LuatOS 探测 @ %d =====\n", baud);

    const char *cmds[] = {
        "print(\"hello\")\r\n",
        "=node.info()\r\n",
        "=rtos.version()\r\n",
        "help\r\n",
        "\r\n",              /* 空命令 */
    };

    for (int i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        flush_uart2();
        rt_thread_mdelay(100);
        uart2_send_str(cmds[i]);
        bridge_print("[TX-L%d] ", i);
        hex_dump("", (rt_uint8_t*)cmds[i], rt_strlen(cmds[i]));

        rt_uint8_t rx[128];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 2000);
        if (rx_len > 0) {
            hex_dump("REPLY", rx, rx_len);
        } else {
            bridge_print("[RX-L%d] NO RESPONSE\n", i);
        }
    }
}

static void probe_yinerda_dtu(int baud)
{
    uart2_set_baud(baud);
    rt_thread_mdelay(500);
    flush_uart2();

    bridge_print("\n===== 银尔达 DTU 探测 @ %d =====\n", baud);

    const char *cmds[] = {
        "config,get,ver\r\n",
        "config,get,devinfo\r\n",
        "config,get,state\r\n",
        "config,set,tcp,1,ttluart,0,0,,0,frp-oil.com,32762,0,,0,,0,0,0,0,0\r\n",
        "config,set,save\r\n",
        "config,set,reboot\r\n",
        "+++",              /* 退出透传 */
    };

    for (int i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        flush_uart2();
        rt_thread_mdelay(100);
        uart2_send_str(cmds[i]);
        bridge_print("[TX-D%d] ", i);
        hex_dump("", (rt_uint8_t*)cmds[i], rt_strlen(cmds[i]));

        rt_uint8_t rx[128];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 2000);
        if (rx_len > 0) {
            hex_dump("REPLY", rx, rx_len);
        } else {
            bridge_print("[RX-D%d] NO RESPONSE\n", i);
        }
    }
}

static void probe_hex_wakeups(int baud)
{
    uart2_set_baud(baud);
    rt_thread_mdelay(500);
    flush_uart2();

    bridge_print("\n===== 十六进制唤醒序列 @ %d =====\n", baud);

    /* 常见唤醒序列 */
    rt_uint8_t wakeups[][8] = {
        {0xFF, 0xFF, 0xFF, 0xFF},                          /* 填充字节 */
        {0x00, 0x00, 0x00, 0x00},                          /* 空字节 */
        {0x41, 0x54, 0x0D, 0x0A},                          /* AT\r\n */
        {0x41, 0x54, 0x0D},                                  /* AT\r */
        {0x61, 0x74, 0x0D, 0x0A},                          /* at\r\n */
        {0x7E, 0x7E},                                        /* 某些模块的帧头 */
        {0x55, 0x55, 0x55, 0x55},                          /* 同步序列 */
    };
    int wake_len[] = {4, 4, 4, 3, 4, 2, 4};

    for (int i = 0; i < sizeof(wake_len)/sizeof(wake_len[0]); i++) {
        flush_uart2();
        rt_thread_mdelay(200);
        uart2_send_raw(wakeups[i], wake_len[i]);
        bridge_print("[TX-H%d] ", i);
        hex_dump("", wakeups[i], wake_len[i]);

        rt_uint8_t rx[64];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 1500);
        if (rx_len > 0) {
            hex_dump("REPLY", rx, rx_len);
        } else {
            bridge_print("[RX-H%d] NO RESPONSE\n", i);
        }
    }
}

static void probe_dtr_rts(void)
{
    bridge_print("\n===== DTR/RTS 引脚测试 =====\n");
    bridge_print("注意: 确保模块 RTS/CTS 引脚已正确连接或悬空\n");
    bridge_print("如果模块流控未就绪，可能拒绝发送数据\n");
    bridge_print("建议: 将模块 RTS 和 CTS 短接或都接 GND\n\n");
}

/* ================================================================
 * 初始化
 * ================================================================ */

static int probe_init(void)
{
    g_uart2 = rt_device_find(UART2_NAME);

    if (rt_console_get_device() == RT_NULL) {
        bridge_print("[ERR] Console device not available!\n");
        return -1;
    }

    if (g_uart2 == RT_NULL) {
        bridge_print("[ERR] UART2 not found!\n");
        return -1;
    }

    /* 读写模式打开 UART2 */
    if (rt_device_open(g_uart2, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        bridge_print("[ERR] UART2 open failed!\n");
        return -1;
    }

    bridge_print("\n================================================\n");
    bridge_print("  RT_SPARK - Auto Probe v5\n");
    bridge_print("  自动探测 DTU 模块固件类型\n");
    bridge_print("================================================\n\n");

    return 0;
}

/* ================================================================
 * 主探测线程
 * ================================================================ */

static void probe_thread(void *parameter)
{
    (void)parameter;

    rt_thread_mdelay(3000);  /* 等模块启动 */
    bridge_print("[INFO] 等待模块启动 3s，开始探测...\n\n");

    /* 探测 1: 多种波特率 AT 变体 */
    int bauds[] = {115200, 9600, 19200, 38400, 57600, 4800};
    for (int i = 0; i < sizeof(bauds)/sizeof(bauds[0]); i++) {
        probe_at_variants(bauds[i]);
    }

    /* 探测 2: LuatOS 模式 */
    probe_luatos(115200);
    probe_luatos(9600);

    /* 探测 3: 银尔达 DTU 模式 */
    probe_yinerda_dtu(115200);
    probe_yinerda_dtu(9600);

    /* 探测 4: 十六进制唤醒序列 */
    probe_hex_wakeups(115200);
    probe_hex_wakeups(9600);

    /* 探测 5: DTR/RTS 提示 */
    probe_dtr_rts();

    bridge_print("\n================================================\n");
    bridge_print("  探测完成！\n");
    bridge_print("  请查看上方结果，找有 [RX] REPLY 的条目\n");
    bridge_print("  如果所有测试都无响应，请检查硬件连接\n");
    bridge_print("================================================\n");
}

/* ================================================================
 * 入口
 * ================================================================ */

int main(void)
{
    if (probe_init() != 0) {
        return -1;
    }

    rt_thread_t t = rt_thread_create("probe",
                                     probe_thread, RT_NULL,
                                     1024, 10, 10);
    if (t != RT_NULL) {
        rt_thread_startup(t);
    }

    while (1) {
        rt_thread_mdelay(5000);
    }

    return 0;
}
