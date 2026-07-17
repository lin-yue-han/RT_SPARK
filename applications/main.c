/*
 * main.c - RT_SPARK 精确波特率扫描固件 v6
 *
 * 模块在 115200 下回声第一个字符，说明波特率接近但不精确。
 * 此固件扫描 115200 附近的多个波特率，找到能收到完整 "OK" 的正确值。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdio.h>

#define UART2_NAME      "uart2"
#define BUF_SIZE        64

static rt_device_t g_uart2 = RT_NULL;

static void bridge_print(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rt_device_t console = rt_console_get_device();
    if (console) rt_device_write(console, 0, buf, rt_strlen(buf));
}

static void uart2_set_baud(int baud)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = baud;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_uart2, RT_DEVICE_CTRL_CONFIG, &cfg);
}

static void uart2_send_str(const char *str)
{
    if (g_uart2) rt_device_write(g_uart2, 0, str, rt_strlen(str));
}

static void flush_uart2(void)
{
    rt_uint8_t ch;
    while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {}
}

static int uart2_recv_wait(rt_uint8_t *buf, int max_len, int timeout_ms)
{
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout_ticks = rt_tick_from_millisecond(timeout_ms);
    int idx = 0;

    while ((rt_tick_get() - start) < timeout_ticks) {
        rt_uint8_t ch;
        while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {
            if (idx < max_len) buf[idx++] = ch;
        }
        if (idx > 0) {
            rt_thread_mdelay(100);  /* 等更多数据 */
            while (rt_device_read(g_uart2, 0, &ch, 1) == 1) {
                if (idx < max_len) buf[idx++] = ch;
            }
            return idx;
        }
        rt_thread_mdelay(50);
    }
    return 0;
}

/* ================================================================
 * 精确波特率扫描
 * ================================================================ */

static void scan_baud(void)
{
    /* 扫描 115200 附近的多个波特率 */
    int bauds[] = {
        111000, 112000, 113000, 114000,
        115000, 115200, 115384, 115789, 116000,
        117000, 117647, 118000, 118919, 120000,
        122000, 125000, 128000, 130000,
        230400, 460800, 921600
    };
    int n = sizeof(bauds) / sizeof(bauds[0]);

    bridge_print("\n===== 精确波特率扫描 =====\n");
    bridge_print("扫描 %d 个波特率，找能收到完整 OK 的值...\n\n", n);

    for (int i = 0; i < n; i++) {
        uart2_set_baud(bauds[i]);
        rt_thread_mdelay(200);
        flush_uart2();

        /* 发 5 次 AT\r\n */
        for (int j = 0; j < 5; j++) {
            uart2_send_str("AT\r\n");
            rt_thread_mdelay(100);
        }

        rt_uint8_t rx[64];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 1500);

        if (rx_len > 0) {
            bridge_print("baud=%d: rx_len=%d, ", bauds[i], rx_len);
            for (int k = 0; k < rx_len; k++) {
                bridge_print("%02X ", rx[k]);
            }
            bridge_print("| ");
            for (int k = 0; k < rx_len; k++) {
                if (rx[k] >= 32 && rx[k] < 127) {
                    bridge_print("%c", rx[k]);
                } else if (rx[k] == 0x0D) {
                    bridge_print("\\r");
                } else if (rx[k] == 0x0A) {
                    bridge_print("\\n");
                } else {
                    bridge_print(".");
                }
            }
            bridge_print("\n");

            /* 检查是否包含 OK */
            if (rx_len >= 2 && rx[0] == 'O' && rx[1] == 'K') {
                bridge_print("*** FOUND! baud=%d 回复 OK ***\n", bauds[i]);
            }
        } else {
            bridge_print("baud=%d: NO RESPONSE\n", bauds[i]);
        }
    }
}

/* ================================================================
 * 发送时序测试
 * ================================================================ */

static void timing_test(int baud)
{
    uart2_set_baud(baud);
    rt_thread_mdelay(300);
    flush_uart2();

    bridge_print("\n===== 发送时序测试 @ %d =====\n", baud);

    /* 测试不同发送间隔 */
    int delays[] = {0, 10, 50, 100, 200, 500};
    for (int i = 0; i < sizeof(delays)/sizeof(delays[0]); i++) {
        flush_uart2();

        uart2_send_str("AT");
        rt_thread_mdelay(delays[i]);
        uart2_send_str("\r\n");

        rt_uint8_t rx[64];
        int rx_len = uart2_recv_wait(rx, sizeof(rx), 1000);

        if (rx_len > 0) {
            bridge_print("delay=%dms: ", delays[i]);
            for (int k = 0; k < rx_len; k++) {
                bridge_print("%c", (rx[k] >= 32 && rx[k] < 127) ? rx[k] : '.');
            }
            bridge_print("\n");
        } else {
            bridge_print("delay=%dms: NO RESPONSE\n", delays[i]);
        }
    }
}

/* ================================================================
 * 入口
 * ================================================================ */

static void probe_thread(void *parameter)
{
    (void)parameter;

    rt_thread_mdelay(2000);
    bridge_print("\n================================================\n");
    bridge_print("  RT_SPARK - Baud Rate Scanner v6\n");
    bridge_print("  精确波特率扫描\n");
    bridge_print("================================================\n");

    scan_baud();
    timing_test(115200);
    timing_test(117647);  /* 26MHz 晶振常见偏差值 */

    bridge_print("\n================================================\n");
    bridge_print("  扫描完成\n");
    bridge_print("  如果找到 OK 回复，记下对应波特率\n");
    bridge_print("================================================\n");
}

int main(void)
{
    g_uart2 = rt_device_find(UART2_NAME);
    if (g_uart2 == RT_NULL) {
        bridge_print("[ERR] UART2 not found!\n");
        return -1;
    }
    if (rt_device_open(g_uart2, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        bridge_print("[ERR] UART2 open failed!\n");
        return -1;
    }

    rt_thread_t t = rt_thread_create("probe", probe_thread, RT_NULL, 1024, 10, 10);
    if (t) rt_thread_startup(t);

    while (1) rt_thread_mdelay(5000);
    return 0;
}
