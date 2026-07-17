/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2026-06-25     Administrator the first version
 */
#include "gy_bn0055.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define UART_NAME       "uart1"
#define UART_BAUDRATE   115200
/* BNO055 UART 帧最长约 120 字节，留足裕量 */
#define BUF_SIZE        256
#define LINE_BUF_SIZE   200

static rt_device_t uart_dev    = RT_NULL;
static rt_sem_t    uart_rx_sem = RT_NULL;

/**
 * @brief UART 接收中断回调——释放信号量通知读取线程
 */
static rt_err_t uart_rx_indicate(rt_device_t dev, rt_size_t size)
{
    if (uart_rx_sem != RT_NULL) {
        rt_sem_release(uart_rx_sem);
    }
    return RT_EOK;
}

/**
 * @brief 从 UART 读取一行（以 '\r' 或 '\n' 为结束符，超时 timeout_ms ms）
 * @return 读取到的有效字符数（不含终止符）；0 或负值表示超时/错误
 */
static int read_uart_line(char *line, int max_len, int timeout_ms)
{
    int      pos        = 0;
    char     ch;
    rt_tick_t start    = rt_tick_get();
    rt_tick_t deadline = start + rt_tick_from_millisecond(timeout_ms);

    while (pos < max_len - 1) {
        /* 剩余超时时间（tick） */
        rt_tick_t now = rt_tick_get();
        if (now >= deadline) {
            break;
        }
        rt_tick_t wait_ticks = deadline - now;
        if (wait_ticks > rt_tick_from_millisecond(50)) {
            wait_ticks = rt_tick_from_millisecond(50);
        }

        /* 等待数据就绪信号 */
        if (rt_sem_take(uart_rx_sem, wait_ticks) != RT_EOK) {
            continue;  /* 超时片段，重新判断总超时 */
        }

        /* 读取所有当前可用字节 */
        while (rt_device_read(uart_dev, 0, &ch, 1) == 1) {
            if (ch == '\r' || ch == '\n') {
                if (pos > 0) {
                    /* 收到行结束符且已有内容，结束本行 */
                    line[pos] = '\0';
                    return pos;
                }
                /* 前导 \r\n，跳过 */
                continue;
            }
            line[pos++] = ch;
            if (pos >= max_len - 1) {
                line[pos] = '\0';
                return pos;
            }
        }
    }

    line[pos] = '\0';
    return pos;  /* 可能为 0（纯超时） */
}

/**
 * @brief 解析 BNO055 UART 数据帧
 * 期望格式：
 *   $BNO055,ACC=ax,ay,az,GYR=gx,gy,gz,MAG=mx,my,mz,EUL=h,r,p,QUAT=w,x,y,z*CS
 *
 * 注意：外层 strtok_r 以 ',' 切分后，各段可能是：
 *   "ACC=ax"  "ay"  "az"  "GYR=gx" ...
 * 因此对带 '=' 的段取 '=' 后的值，后续裸数字段顺序延续到当前字段组。
 */
static int parse_bn0055_frame(const char *line, bn0055_data_t *data)
{
    char  buffer[LINE_BUF_SIZE];
    char *token;
    char *saveptr;

    /* 各分量计数器，用于按顺序填入 */
    int  parsed_count = 0;
    int  field        = -1;  /* 0=ACC 1=GYR 2=MAG 3=EUL 4=QUAT */
    int  sub_idx      = 0;

    if (line == NULL || data == NULL) return -1;
    if (strncmp(line, "$BNO055", 7) != 0) return -1;

    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    /* 去掉校验和 "*XX" */
    char *star = strrchr(buffer, '*');
    if (star) *star = '\0';

    token = strtok_r(buffer, ",", &saveptr);
    while (token != NULL) {
        const char *val_str = NULL;

        /* 检测字段头并切换 field 状态机 */
        if (strncmp(token, "ACC=", 4) == 0) {
            field = 0; sub_idx = 0; val_str = token + 4; parsed_count++;
        } else if (strncmp(token, "GYR=", 4) == 0) {
            field = 1; sub_idx = 0; val_str = token + 4; parsed_count++;
        } else if (strncmp(token, "MAG=", 4) == 0) {
            field = 2; sub_idx = 0; val_str = token + 4; parsed_count++;
        } else if (strncmp(token, "EUL=", 4) == 0) {
            field = 3; sub_idx = 0; val_str = token + 4; parsed_count++;
        } else if (strncmp(token, "QUAT=", 5) == 0) {
            field = 4; sub_idx = 0; val_str = token + 5; parsed_count++;
        } else if (strncmp(token, "$BNO055", 7) == 0) {
            /* 跳过帧头 */
        } else {
            /* 延续上一个字段的后续分量 */
            val_str = token;
        }

        if (val_str != NULL && field >= 0) {
            float val = (float)atof(val_str);
            switch (field) {
                case 0:
                    if (sub_idx == 0) data->accel_x = val;
                    else if (sub_idx == 1) data->accel_y = val;
                    else if (sub_idx == 2) data->accel_z = val;
                    break;
                case 1:
                    if (sub_idx == 0) data->gyro_x = val;
                    else if (sub_idx == 1) data->gyro_y = val;
                    else if (sub_idx == 2) data->gyro_z = val;
                    break;
                case 2:
                    if (sub_idx == 0) data->mag_x = val;
                    else if (sub_idx == 1) data->mag_y = val;
                    else if (sub_idx == 2) data->mag_z = val;
                    break;
                case 3:
                    if (sub_idx == 0) data->euler_heading = val;
                    else if (sub_idx == 1) data->euler_roll  = val;
                    else if (sub_idx == 2) data->euler_pitch = val;
                    break;
                case 4:
                    if (sub_idx == 0) data->quaternion_w = val;
                    else if (sub_idx == 1) data->quaternion_x = val;
                    else if (sub_idx == 2) data->quaternion_y = val;
                    else if (sub_idx == 3) data->quaternion_z = val;
                    break;
                default: break;
            }
            sub_idx++;
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    return (parsed_count >= 3) ? RT_EOK : -RT_ERROR;
}

/**
 * @brief 读取一帧 BNO055 数据
 */
int bn0055_read(bn0055_data_t *data)
{
    char line[LINE_BUF_SIZE];
    int  len;
    int  retry = 3;

    if (data == RT_NULL || uart_dev == RT_NULL) return -RT_ERROR;

    /* 最多重试 3 次，跳过噪声帧 */
    while (retry-- > 0) {
        len = read_uart_line(line, sizeof(line), 300);
        if (len <= 0) continue;

        if (parse_bn0055_frame(line, data) == RT_EOK) {
            data->error = 0;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

/**
 * @brief 设置 BNO055 工作模式（通过 UART 发送配置命令）
 *        GY-BN0055 通常在固件中固定输出模式，此接口预留兼容性
 */
int bn0055_set_mode(uint8_t mode)
{
    char cmd[16];
    int  len;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    len = rt_snprintf(cmd, sizeof(cmd), "MODE=%d\r\n", mode);
    rt_device_write(uart_dev, 0, cmd, len);

    /* 等待模块切换（100 ms） */
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: set mode %d\n", mode);
    return RT_EOK;
}

/**
 * @brief 初始化 GY-BN0055（UART1，115200，PA9/PA10）
 *        BNO055 独占 UART1(PA9/PA10)，不使用 FinSH 控制台
 *        数据通过 4G 模块(UART2) 发送到网页展示
 */
int bn0055_init(void)
{
    rt_err_t ret;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* 1. 查找 UART 设备 */
    uart_dev = rt_device_find(UART_NAME);
    if (uart_dev == RT_NULL) {
        rt_kprintf("BN0055: UART '%s' not found!\n", UART_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("BN0055: UART '%s' found\n", UART_NAME);

    /* 2. 创建接收信号量 */
    uart_rx_sem = rt_sem_create("bn0055_sem", 0, RT_IPC_FLAG_FIFO);
    if (uart_rx_sem == RT_NULL) {
        rt_kprintf("BN0055: Failed to create semaphore\n");
        return -RT_ERROR;
    }

    /* 3. 打开 UART（中断接收模式） */
    ret = rt_device_open(uart_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: Failed to open UART (ret=%d)\n", ret);
        rt_sem_delete(uart_rx_sem);
        uart_rx_sem = RT_NULL;
        return -RT_ERROR;
    }

    /* 4. 配置波特率（必须在 open 之后） */
    config.baud_rate = UART_BAUDRATE;
    ret = rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: UART config failed (ret=%d)\n", ret);
        /* 非致命，继续运行 */
    }

    /* 5. 注册接收回调 */
    rt_device_set_rx_indicate(uart_dev, uart_rx_indicate);

    rt_kprintf("BN0055: Init OK (baudrate=%d)\n", UART_BAUDRATE);
    return RT_EOK;
}
