/*
 * uart_recv.c
 * UART4 接收驱动（PA0/PA1），用于接收 ESP32-C3 数据并打印到 RT-Thread 控制台
 *
 * 注意：本模块使用 UART4（PA0 TX / PA1 RX），与 GY-BN0055 使用的 UART1 分开，
 *       避免冲突。
 */

#include <rtthread.h>
#include <rtdevice.h>

/* ==================== Configuration ==================== */
#define UART_NAME       "uart4"     /* UART4: PA0(TX)/PA1(RX) */
#define UART_BAUDRATE   115200
#define RX_BUF_SIZE     256

/* ==================== Global Variables ==================== */
static rt_device_t serial  = RT_NULL;
static rt_sem_t    rx_sem  = RT_NULL;
static char rx_buffer[RX_BUF_SIZE];

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

    rt_kprintf("[uart_recv] Thread started, waiting for ESP32-C3 data...\n");

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
                    rt_kprintf("[ESP32] %s", rx_buffer);
                }
                index = 0;
                rt_memset(rx_buffer, 0, RX_BUF_SIZE);
            }
        }
    }
}

/* ==================== Initialization Function ==================== */
static int uart_recv_init(void)
{
    rt_err_t ret;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* 1. 查找 UART 设备 */
    serial = rt_device_find(UART_NAME);
    if (serial == RT_NULL) {
        rt_kprintf("[uart_recv] Error: '%s' not found!\n", UART_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("[uart_recv] Device '%s' found\n", UART_NAME);

    /* 2. 创建信号量 */
    rx_sem = rt_sem_create("uart4_sem", 0, RT_IPC_FLAG_FIFO);
    if (rx_sem == RT_NULL) {
        rt_kprintf("[uart_recv] Error: Failed to create semaphore!\n");
        return -RT_ERROR;
    }

    /* 3. 打开设备（中断接收模式） */
    ret = rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("[uart_recv] Error: Failed to open '%s' (ret=%d)!\n", UART_NAME, ret);
        rt_sem_delete(rx_sem);
        rx_sem = RT_NULL;
        return -RT_ERROR;
    }

    /* 4. 配置波特率 */
    config.baud_rate = UART_BAUDRATE;
    rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config);
    rt_kprintf("[uart_recv] '%s' opened, baudrate=%d\n", UART_NAME, UART_BAUDRATE);

    /* 5. 注册接收回调 */
    rt_device_set_rx_indicate(serial, uart_rx_callback);

    /* 6. 创建并启动接收线程 */
    rt_thread_t thread = rt_thread_create(
        "uart4_rx",
        serial_recv_thread_entry,
        RT_NULL,
        1024,   /* 栈大小：接收处理简单，512 字节足够，给 1024 留余量 */
        16,     /* 优先级略低于传感器线程 */
        10
    );

    if (thread == RT_NULL) {
        rt_kprintf("[uart_recv] Error: Failed to create receive thread!\n");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    rt_kprintf("[uart_recv] Ready, waiting for ESP32-C3 data on %s\n", UART_NAME);

    return RT_EOK;
}

/* 手动初始化命令，系统启动后在 finsh 输入 uart_recv_start */
MSH_CMD_EXPORT(uart_recv_init, start UART4 receive for ESP32-C3);
