/*
 * main.c - DTU 纯硬件透传桥接固件 v4
 *
 * 功能：STM32 UART1 ↔ UART2 纯字节转发，不做任何解析。
 *        电脑 COM9 (UART1) 的数据直接发到 DTU (UART2)，
 *        DTU (UART2) 的回复直接回电脑 COM9 (UART1)。
 *
 * 注意：此固件不启动 FinSH、不启动任何传感器/电机/加热片。
 *       上电后立即开始双向转发。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define BUF_SIZE 256

static rt_device_t g_uart1 = RT_NULL;
static rt_device_t g_uart2 = RT_NULL;

/* ================================================================
 * UART1 → UART2 转发线程
 * ================================================================ */

static void uart1_to_uart2_entry(void *parameter)
{
    unsigned char buf[BUF_SIZE];
    rt_kprintf("[Bridge] UART1→UART2 forward started\n");

    while (1) {
        int len = rt_device_read(g_uart1, 0, buf, sizeof(buf));
        if (len > 0) {
            rt_device_write(g_uart2, 0, buf, len);
        }
        rt_thread_mdelay(5);
    }
}

/* ================================================================
 * UART2 → UART1 转发线程
 * ================================================================ */

static void uart2_to_uart1_entry(void *parameter)
{
    unsigned char buf[BUF_SIZE];
    rt_kprintf("[Bridge] UART2→UART1 forward started\n");

    while (1) {
        int len = rt_device_read(g_uart2, 0, buf, sizeof(buf));
        if (len > 0) {
            rt_device_write(g_uart1, 0, buf, len);
        }
        rt_thread_mdelay(5);
    }
}

/* ================================================================
 * 初始化
 * ================================================================ */

static int bridge_init(void)
{
    rt_thread_t t1, t2;
    struct serial_configure cfg;

    /* 打开 UART1 (COM9) - 读写 */
    g_uart1 = rt_device_find("uart1");
    if (g_uart1 == RT_NULL) {
        rt_kprintf("[Bridge] ERROR: uart1 not found!\n");
        return -RT_ERROR;
    }
    if (rt_device_open(g_uart1, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[Bridge] ERROR: failed to open uart1!\n");
        return -RT_ERROR;
    }

    /* 打开 UART2 (DTU) - 读写 */
    g_uart2 = rt_device_find("uart2");
    if (g_uart2 == RT_NULL) {
        rt_kprintf("[Bridge] ERROR: uart2 not found!\n");
        return -RT_ERROR;
    }
    if (rt_device_open(g_uart2, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[Bridge] ERROR: failed to open uart2!\n");
        return -RT_ERROR;
    }

    /* 配置 UART1: 115200 8N1 */
    cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = 115200;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_uart1, RT_DEVICE_CTRL_CONFIG, &cfg);

    /* 配置 UART2: 115200 8N1 (可改) */
    cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = 115200;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_uart2, RT_DEVICE_CTRL_CONFIG, &cfg);

    rt_kprintf("[Bridge] UART1 @ 115200, UART2 @ 115200\n");

    /* 启动转发线程 */
    t1 = rt_thread_create("u1_u2", uart1_to_uart2_entry, RT_NULL, 1024, 12, 10);
    t2 = rt_thread_create("u2_u1", uart2_to_uart1_entry, RT_NULL, 1024, 12, 10);
    if (t1) rt_thread_startup(t1);
    if (t2) rt_thread_startup(t2);

    return RT_EOK;
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("================================================\n");
    rt_kprintf("  RT_SPARK - Hardware Bridge v4\n");
    rt_kprintf("  UART1(COM9) <-> UART2(DTU)\n");
    rt_kprintf("  Pure byte forwarding, no parsing\n");
    rt_kprintf("================================================\n\n");

    bridge_init();

    /* 主线程空转，保持系统运行 */
    while (1) {
        rt_thread_mdelay(1000);
    }

    return 0;
}
