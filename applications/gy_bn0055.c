/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2026-06-25     Administrator the first version
 * 2026-07-18     Administrator fix: use polling read instead of INT_RX
 */
#include "gy_bn0055.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* BNO055 使用 UART1(PA9/PA10)，RX 读取传感器数据，TX 留给控制台输出 JSON */
#define UART_NAME       "uart1"
#define UART_BAUDRATE   115200
/* BNO055 UART 帧最长约 120 字节，留足裕量 */
#define BUF_SIZE        256
#define LINE_BUF_SIZE   200

static rt_device_t uart_dev    = RT_NULL;

/**
 * @brief 从 UART 轮询读取一行（以 '\r' 或 '\n' 为结束符，超时 timeout_ms）
 * @return 读取到的有效字符数（不含终止符）；0 表示超时/无数据
 */
static int read_uart_line(char *line, int max_len, int timeout_ms)
{
    int       pos = 0;
    char      ch;
    rt_tick_t start    = rt_tick_get();
    rt_tick_t deadline = start + rt_tick_from_millisecond(timeout_ms);

    while (pos < max_len - 1) {
        if (rt_tick_get() >= deadline) {
            break;
        }

        /* 轮询读取一个字节 */
        if (rt_device_read(uart_dev, 0, &ch, 1) == 1) {
            if (ch == '\r' || ch == '\n') {
                if (pos > 0) {
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
        } else {
            /* 无数据，短暂等待 */
            rt_thread_mdelay(5);
        }
    }

    line[pos] = '\0';
    return pos;  /* 可能为 0（纯超时） */
}

/**
 * @brief 解析 BNO055 UART 数据帧
 * 期望格式：
 *   $BNO055,ACC=ax,ay,az,GYR=gx,gy,gz,MAG=mx,my,mz,EUL=h,r,p,QUAT=w,x,y,z*CS
 */
static int parse_bn0055_frame(const char *line, bn0055_data_t *data)
{
    char  buffer[LINE_BUF_SIZE];
    char *token;
    char *saveptr;

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
 *        先发送 READ 命令，然后轮询等待响应（最长 1 秒）
 */
int bn0055_read(bn0055_data_t *data)
{
    char line[LINE_BUF_SIZE];
    int  len;
    int  retry = 3;

    if (data == RT_NULL || uart_dev == RT_NULL) return -RT_ERROR;

    /* 发送读取命令，请求 BNO055 输出数据 */
    rt_device_write(uart_dev, 0, "READ\r\n", 6);
    rt_thread_mdelay(50);  /* 等待模块响应 */

    while (retry-- > 0) {
        len = read_uart_line(line, sizeof(line), 1000);
        if (len <= 0) {
            rt_kprintf("[BN0055] read_uart_line timeout/empty, retry left=%d\n", retry);
            continue;
        }

        rt_kprintf("[BN0055] Raw line: len=%d, content=%.60s\n", len, line);

        if (parse_bn0055_frame(line, data) == RT_EOK) {
            data->error = 0;
            return RT_EOK;
        }
        rt_kprintf("[BN0055] Parse fail: len=%d, raw=%.40s\n", len, line);
    }

    return -RT_ERROR;
}

/**
 * @brief 设置 BNO055 工作模式
 */
int bn0055_set_mode(uint8_t mode)
{
    char cmd[16];
    int  len;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    len = rt_snprintf(cmd, sizeof(cmd), "MODE=%d\r\n", mode);
    rt_device_write(uart_dev, 0, cmd, len);
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: set mode %d\n", mode);
    return RT_EOK;
}

/**
 * @brief 初始化 GY-BN0055（UART1，115200，PA9/PA10）
 *        使用轮询读取，不依赖 RX 中断，避免与控制台冲突
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

    /* 2. 打开 UART（轮询读写模式） */
    ret = rt_device_open(uart_dev, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: Failed to open UART (ret=%d)\n", ret);
        return -RT_ERROR;
    }

    /* 3. 配置波特率（必须在 open 之后） */
    config.baud_rate = UART_BAUDRATE;
    ret = rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: UART config failed (ret=%d)\n", ret);
    }

    /* 4. 发送初始化命令 */
    rt_device_write(uart_dev, 0, "INIT\r\n", 6);
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: Init OK (baudrate=%d, polling mode)\n", UART_BAUDRATE);
    return RT_EOK;
}
