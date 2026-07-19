/*
 * uart_recv.c
 * UART2 接收驱动（PA2/PA3），用于接收 Core-Y100M 4G 模块数据并打印到 RT-Thread 控制台
 *
 * 注意：本模块使用 UART2（PA2 TX / PA3 RX），与 GY-BN0055 使用的 UART1 分开，
 *       避免冲突。FinSH 控制台已迁移到 UART3（PB10/PB11）。
 */

#include <rtthread.h>
#include <rtdevice.h>

/* ==================== Configuration ==================== */
#define UART_NAME       "uart2"     /* UART2: PA2(TX)/PA3(RX) → Core-Y100M 4G */
#define UART_BAUDRATE   115200
#define RX_BUF_SIZE     256
#define DTU_AUTO_CONFIG_ON_BOOT 1

/* ==================== Global Variables ==================== */
static rt_device_t serial  = RT_NULL;
static rt_sem_t    rx_sem  = RT_NULL;
static char rx_buffer[RX_BUF_SIZE];
volatile int g4_config_busy = 0;

int uart_recv_init(void);

/* ==================== UART Receive Callback ==================== */
static rt_err_t uart_rx_callback(rt_device_t dev, rt_size_t size)
{
    if (rx_sem != RT_NULL) {
        rt_sem_release(rx_sem);
    }
    return RT_EOK;
}

/* ==================== Receive Thread ==================== */
static void serial_recv_thread_entry(void *parameter)
{
    char ch;
    int  index = 0;

    rt_kprintf("[4G] Wireless command receiver started\n");

    while (1)
    {
        rt_sem_take(rx_sem, RT_WAITING_FOREVER);

        while (rt_device_read(serial, 0, &ch, 1) == 1)
        {
            if (index < RX_BUF_SIZE - 1) {
                rx_buffer[index++] = ch;
            }

            if (ch == '\n' || ch == '\r')
            {
                if (index > 1) {  /* 忽略只有 \r 或 \n 的空行 */
                    rx_buffer[index] = '\0';
                    rt_kprintf("[4G] RX: %s\n", rx_buffer);

                    /* 解析并执行远程命令 */
                    if (rt_strcmp(rx_buffer, "forward") == 0) {
                        extern void start_motor_with_timeout(int dir);
                        start_motor_with_timeout(1);
                        rt_kprintf("[4G-CMD] -> FORWARD executed\n");
                    }
                    else if (rt_strcmp(rx_buffer, "backward") == 0) {
                        extern void start_motor_with_timeout(int dir);
                        start_motor_with_timeout(-1);
                        rt_kprintf("[4G-CMD] -> BACKWARD executed\n");
                    }
                    else if (rt_strcmp(rx_buffer, "stop") == 0) {
                        extern void stop(void);
                        stop();
                        rt_kprintf("[4G-CMD] -> STOP executed\n");
                    }
                    else if (rt_strcmp(rx_buffer, "heater_on") == 0) {
                        extern void heater_on(void);
                        heater_on();
                        rt_kprintf("[4G-CMD] -> HEATER ON executed\n");
                    }
                    else if (rt_strcmp(rx_buffer, "heater_off") == 0) {
                        extern void heater_off(void);
                        heater_off();
                        rt_kprintf("[4G-CMD] -> HEATER OFF executed\n");
                    }
                    else if (rt_strcmp(rx_buffer, "reset_detector") == 0) {
                        extern void gd_reset_detector(void);
                        gd_reset_detector();
                        rt_kprintf("[4G-CMD] -> DETECTOR RESET executed\n");
                    }
                    else {
                        rt_kprintf("[4G-CMD] Unknown: %s\n", rx_buffer);
                    }
                }
                index = 0;
                rt_memset(rx_buffer, 0, RX_BUF_SIZE);
            }
        }
    }
}

/* ==================== 4G Send Function ==================== */
/**
 * @brief 向 Core-Y100M 4G 模块发送文本数据
 *        模块配置好 TCP/MQTT 透传通道后，发送的数据会由模块转发至云端
 * @param text 要发送的文本内容（如 "T:25.3,H:70.1\r\n"）
 * @return RT_EOK 成功，-RT_ERROR 失败
 */
rt_err_t g4_send_text(const char *text)
{
    if (serial == RT_NULL || text == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_size_t length = rt_strlen(text);
    rt_size_t written = rt_device_write(serial, 0, text, length);

    return written == length ? RT_EOK : -RT_ERROR;
}

static rt_err_t g4_send_line(const char *text)
{
    if (g4_send_text(text) != RT_EOK) {
        return -RT_ERROR;
    }

    return g4_send_text("\r\n");
}

static rt_err_t ensure_uart2_ready(void)
{
    if (serial != RT_NULL) {
        return RT_EOK;
    }

    return uart_recv_init();
}

void dtu_cmd(int argc, char **argv)
{
    if (argc < 2) {
        rt_kprintf("Usage: dtu_cmd <text-without-spaces>\n");
        rt_kprintf("Example: dtu_cmd config,set,save\n");
        return;
    }

    if (ensure_uart2_ready() != RT_EOK) {
        rt_kprintf("[4G-CONFIG] UART2 not ready\n");
        return;
    }

    rt_kprintf("[4G-CONFIG] TX: %s\n", argv[1]);
    if (g4_send_line(argv[1]) != RT_EOK) {
        rt_kprintf("[4G-CONFIG] send failed\n");
    }
}

void dtu_config(void)
{
    static const char *cmds[] = {
        "config,set,tcp,1,ttluart,0,0,,0,frp-oil.com,32762,0,,0,,0,0,0,0,0",
        "config,set,save",
        "config,set,reboot",
    };
    int i;

    if (ensure_uart2_ready() != RT_EOK) {
        rt_kprintf("[4G-CONFIG] UART2 not ready\n");
        return;
    }

    g4_config_busy = 1;
    rt_thread_mdelay(300);

    rt_kprintf("[4G-CONFIG] Core-Y100P TCP client -> frp-oil.com:32762\n");
    for (i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        rt_kprintf("[4G-CONFIG] TX: %s\n", cmds[i]);
        if (g4_send_line(cmds[i]) != RT_EOK) {
            rt_kprintf("[4G-CONFIG] send failed at step %d\n", i + 1);
            g4_config_busy = 0;
            return;
        }
        rt_thread_mdelay(2500);
    }
    rt_thread_mdelay(15000);
    g4_config_busy = 0;
    rt_kprintf("[4G-CONFIG] done, wait 30-60s for module reboot/register/connect\n");
}

#if DTU_AUTO_CONFIG_ON_BOOT
static void dtu_auto_config_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_kprintf("[4G-CONFIG] auto config scheduled, waiting 45s for shared power DTU boot...\n");
    rt_thread_mdelay(45000);
    dtu_config();

    rt_kprintf("[4G-CONFIG] backup auto config scheduled, waiting another 75s...\n");
    rt_thread_mdelay(75000);
    dtu_config();
}
#endif

/* ==================== Initialization Function ==================== */
/* 导出为全局函数，供 main.c 调用 */
int uart_recv_init(void)
{
    rt_err_t ret;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* 1. 查找 UART 设备 */
    serial = rt_device_find(UART_NAME);
    if (serial == RT_NULL) {
        rt_kprintf("[4G] Error: '%s' not found!\n", UART_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("[4G] Device '%s' found\n", UART_NAME);

    /* 2. 创建信号量 */
    rx_sem = rt_sem_create("uart2_sem", 0, RT_IPC_FLAG_FIFO);
    if (rx_sem == RT_NULL) {
        rt_kprintf("[4G] Error: Failed to create semaphore!\n");
        return -RT_ERROR;
    }

    /* 3. 打开设备（中断接收模式） */
    ret = rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("[4G] Error: Failed to open '%s' (ret=%d)!\n", UART_NAME, ret);
        rt_sem_delete(rx_sem);
        rx_sem = RT_NULL;
        return -RT_ERROR;
    }

    /* 4. 配置波特率 */
    config.baud_rate = UART_BAUDRATE;
    rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config);
    rt_kprintf("[4G] '%s' opened, baudrate=%d\n", UART_NAME, UART_BAUDRATE);

    /* 5. 注册接收回调 */
    rt_device_set_rx_indicate(serial, uart_rx_callback);

    /* 6. 创建并启动接收线程 */
    rt_thread_t thread = rt_thread_create(
        "uart2_rx",
        serial_recv_thread_entry,
        RT_NULL,
        1024,   /* 栈大小：接收处理简单，512 字节足够，给 1024 留余量 */
        16,     /* 优先级略低于传感器线程 */
        10
    );

    if (thread == RT_NULL) {
        rt_kprintf("[4G] Error: Failed to create receive thread!\n");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    rt_kprintf("[4G] Ready, waiting for Core-Y100M data on %s\n", UART_NAME);

#if DTU_AUTO_CONFIG_ON_BOOT
    rt_thread_t cfg_thread = rt_thread_create(
        "dtu_cfg",
        dtu_auto_config_thread_entry,
        RT_NULL,
        1024,
        18,
        10
    );
    if (cfg_thread != RT_NULL) {
        rt_thread_startup(cfg_thread);
    } else {
        rt_kprintf("[4G-CONFIG] auto config thread create failed\n");
    }
#endif

    return RT_EOK;
}

/* MSH 命令导出（可选，用于手动调试） */
MSH_CMD_EXPORT(uart_recv_init, start UART2 wireless command receiver);
MSH_CMD_EXPORT(dtu_cmd, send one raw line to Core-Y100P over UART2);
MSH_CMD_EXPORT(dtu_config, configure Core-Y100P TCP client for RT_SPARK);
