/*
 * main.c - DTU 桥接固件 v3（简化版：固定 115200，打印原始 hex）
 *
 * 硬件：
 *   UART1 (PA9/PA10) → ST-Link COM9（控制台）
 *   UART2 (PA2/PA3)   → DTU 模块
 *
 * 功能：
 *   - 上电后 DTU UART2 固定 115200
 *   - DTU 收到的一切数据以 hex 形式打印（可看到乱码的真实字节）
 *   - dtu_send 命令发数据
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#define DTU_UART_NAME   "uart2"
#define BUF_SIZE        256

static rt_device_t g_dtu_uart = RT_NULL;

/* ================================================================
 * DTU 接收线程 —— 十六进制打印，不丢失任何信息
 * ================================================================ */

static void dtu_recv_thread_entry(void *parameter)
{
    unsigned char buf[BUF_SIZE];

    rt_kprintf("[Bridge] DTU recv started @ 115200 (hex dump mode)\n");

    while (1)
    {
        int len = rt_device_read(g_dtu_uart, 0, buf, sizeof(buf));
        if (len > 0) {
            rt_kprintf("[DTU-HEX] len=%d: ", len);
            for (int i = 0; i < len; i++) {
                rt_kprintf("%02X ", buf[i]);
            }
            rt_kprintf("| ");
            for (int i = 0; i < len; i++) {
                unsigned char c = buf[i];
                if (c >= 32 && c < 127) rt_kprintf("%c", c);
                else rt_kprintf(".");
            }
            rt_kprintf("\n");
        }
        rt_thread_mdelay(50);
    }
}

/* ================================================================
 * MSH: dtu_send
 * ================================================================ */

static void dtu_send(int argc, char **argv)
{
    if (g_dtu_uart == RT_NULL) {
        rt_kprintf("[DTU] ERROR: UART2 not ready!\n");
        return;
    }
    if (argc < 2) {
        rt_kprintf("Usage: dtu_send <data>\n");
        return;
    }

    char buf[BUF_SIZE];
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        int alen = rt_strlen(argv[i]);
        if (pos + alen + 2 > (int)sizeof(buf)) break;
        if (i > 1) buf[pos++] = ' ';
        rt_memcpy(buf + pos, argv[i], alen);
        pos += alen;
    }
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos] = '\0';

    rt_kprintf("[SEND-HEX] ");
    for (int i = 0; i < pos; i++) rt_kprintf("%02X ", (unsigned char)buf[i]);
    rt_kprintf("| %s", buf);

    int ret = rt_device_write(g_dtu_uart, 0, buf, pos);
    if (ret < 0) rt_kprintf("[DTU] ERROR: send failed\n");
}
MSH_CMD_EXPORT(dtu_send, send data to DTU via UART2);

/* ================================================================
 * MSH: dtu_raw —— 发送原始十六进制字节
 * ================================================================ */

static void dtu_raw(int argc, char **argv)
{
    if (g_dtu_uart == RT_NULL) {
        rt_kprintf("[DTU] ERROR: UART2 not ready!\n");
        return;
    }
    if (argc < 2) {
        rt_kprintf("Usage: dtu_raw <hex bytes>\n");
        rt_kprintf("Example: dtu_raw 41 54 0D 0A   (sends AT\\r\\n)\n");
        return;
    }

    unsigned char buf[BUF_SIZE];
    int len = 0;
    for (int i = 1; i < argc && len < (int)sizeof(buf); i++) {
        unsigned int byte;
        if (sscanf(argv[i], "%x", &byte) == 1) {
            buf[len++] = (unsigned char)byte;
        }
    }

    rt_kprintf("[SEND-RAW] %d bytes: ", len);
    for (int i = 0; i < len; i++) rt_kprintf("%02X ", buf[i]);
    rt_kprintf("\n");

    rt_device_write(g_dtu_uart, 0, buf, len);
}
MSH_CMD_EXPORT(dtu_raw, send raw hex bytes to DTU);

/* ================================================================
 * 初始化
 * ================================================================ */

static int bridge_init(void)
{
    rt_thread_t thread;

    g_dtu_uart = rt_device_find(DTU_UART_NAME);
    if (g_dtu_uart == RT_NULL) {
        rt_kprintf("[Bridge] ERROR: '%s' not found!\n", DTU_UART_NAME);
        return -RT_ERROR;
    }

    if (rt_device_open(g_dtu_uart, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[Bridge] ERROR: failed to open '%s'!\n", DTU_UART_NAME);
        g_dtu_uart = RT_NULL;
        return -RT_ERROR;
    }

    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = 115200;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    rt_device_control(g_dtu_uart, RT_DEVICE_CTRL_CONFIG, &cfg);

    rt_kprintf("[Bridge] UART2 opened @ 115200\n");

    thread = rt_thread_create("dtu_recv", dtu_recv_thread_entry, RT_NULL, 2048, 12, 10);
    if (thread) rt_thread_startup(thread);

    return RT_EOK;
}

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("================================================\n");
    rt_kprintf("  RT_SPARK - DTU Bridge v3 (Hex Dump)\n");
    rt_kprintf("================================================\n\n");

    bridge_init();
    return 0;
}
