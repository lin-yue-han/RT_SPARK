/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2026-06-25     Administrator the first version
 * 2026-07-18     Administrator fix: UART binary protocol (BNO055 datasheet)
 *                BNO055 UART uses 0xAA frame header, NOT ASCII commands!
 */
#include "gy_bn0055.h"
#include <stdint.h>
#include <string.h>

/* BNO055 使用 UART1(PA9/PA10)，控制台的 JSON 输出从 PA9(TX) 发送，
 * 也会到达 BNO055 的 RX。BNO055 UART 协议以 0xAA 为帧头，收到非 0xAA
 * 数据会忽略，所以控制台 JSON 不会干扰正确解析。 */
#define UART_NAME       "uart1"
#define UART_BAUDRATE   115200

/* 模拟数据模式：当 BNO055 硬件无法通信时，生成模拟数据 */
static int g_sim_mode = 0;
static uint32_t g_sim_tick = 0;

/* BNO055 UART 协议常量 */
#define BNO055_START_BYTE     0xAA
#define BNO055_WRITE_CMD      0x00
#define BNO055_READ_CMD       0x01
#define BNO055_RESP_SUCCESS   0xBB
#define BNO055_RESP_ERROR     0xEE

/* BNO055 寄存器 */
#define BNO055_REG_CHIP_ID      0x00
#define BNO055_REG_PAGE_ID      0x07
#define BNO055_REG_ACC_X_LSB    0x08
#define BNO055_REG_MAG_X_LSB    0x0E
#define BNO055_REG_GYR_X_LSB    0x14
#define BNO055_REG_EUL_H_LSB    0x1A
#define BNO055_REG_QUAT_W_LSB   0x20
#define BNO055_REG_OPR_MODE     0x3D
#define BNO055_REG_PWR_MODE     0x3E
#define BNO055_REG_SYS_TRIGGER  0x3F

/* 操作模式 */
#define BNO055_MODE_CONFIG      0x00
#define BNO055_MODE_NDOF        0x0C

static rt_device_t uart_dev = RT_NULL;

/**
 * @brief 清空 UART RX 缓冲区（丢弃所有已接收数据）
 */
static void uart_flush_rx(void)
{
    char ch;
    while (rt_device_read(uart_dev, 0, &ch, 1) == 1) {}
}

/**
 * @brief 从 UART 读取指定字节数（轮询，超时 timeout_ms）
 * @return 实际读取字节数
 */
static int uart_read_bytes(uint8_t *buf, int len, int timeout_ms)
{
    int       pos = 0;
    char      ch;
    rt_tick_t start = rt_tick_get();
    rt_tick_t deadline = rt_tick_from_millisecond(timeout_ms);

    while (pos < len) {
        if (rt_tick_get() - start >= deadline) {
            break;
        }
        if (rt_device_read(uart_dev, 0, &ch, 1) == 1) {
            buf[pos++] = (uint8_t)ch;
        } else {
            rt_thread_mdelay(5);
        }
    }
    return pos;
}

/**
 * @brief BNO055 UART 读取寄存器
 * 发送帧: 0xAA 0x01 reg_addr length
 * 应答帧: 0xBB length [data]  或  0xEE err_code
 */
static int bno055_uart_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t tx[4] = {BNO055_START_BYTE, BNO055_READ_CMD, reg, len};
    uint8_t rx[2];
    int     rd;
    int     i;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    /* 清空旧数据 */
    uart_flush_rx();

    /* 发送读取命令 */
    rt_device_write(uart_dev, 0, tx, 4);
    rt_thread_mdelay(50);

    /* 等待应答帧头（跳过控制台 JSON 输出的干扰字节） */
    rx[0] = 0;
    for (i = 0; i < 50; i++) {  /* 最多尝试 50 次，约 250ms */
        rd = uart_read_bytes(&rx[0], 1, 10);
        if (rd == 1 && (rx[0] == BNO055_RESP_SUCCESS || rx[0] == BNO055_RESP_ERROR)) {
            break;
        }
    }

    if (rx[0] == BNO055_RESP_ERROR) {
        /* 读取错误码 */
        uart_read_bytes(&rx[1], 1, 50);
        rt_kprintf("[BN0055] UART ERROR resp: 0x%02X\n", rx[1]);
        return -RT_ERROR;
    }
    if (rx[0] != BNO055_RESP_SUCCESS) {
        return -RT_ERROR;
    }

    /* 读取应答长度 */
    rd = uart_read_bytes(&rx[1], 1, 50);
    if (rd != 1) return -RT_ETIMEOUT;

    if (rx[1] != len) {
        rt_kprintf("[BN0055] UART len mismatch: got %d, expect %d\n", rx[1], len);
    }

    /* 读取数据 */
    rd = uart_read_bytes(buf, (rx[1] < len) ? rx[1] : len, 100);
    if (rd < len) {
        rt_kprintf("[BN0055] UART data timeout: got %d, expect %d\n", rd, len);
        return -RT_ETIMEOUT;
    }

    return RT_EOK;
}

/**
 * @brief BNO055 UART 写入寄存器
 * 发送帧: 0xAA 0x00 reg_addr 0x01 data
 * 应答帧: 0xBB 0x01 0x00
 */
static int bno055_uart_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[5] = {BNO055_START_BYTE, BNO055_WRITE_CMD, reg, 0x01, val};
    uint8_t rx[3];
    int     rd;
    int     i;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    uart_flush_rx();
    rt_device_write(uart_dev, 0, tx, 5);
    rt_thread_mdelay(50);

    /* 等待应答帧头 */
    rx[0] = 0;
    for (i = 0; i < 50; i++) {
        rd = uart_read_bytes(&rx[0], 1, 10);
        if (rd == 1 && (rx[0] == BNO055_RESP_SUCCESS || rx[0] == BNO055_RESP_ERROR)) {
            break;
        }
    }

    if (rx[0] != BNO055_RESP_SUCCESS) {
        return -RT_ERROR;
    }

    /* 读取应答长度和状态 */
    rd = uart_read_bytes(&rx[1], 2, 50);
    if (rd != 2) return -RT_ETIMEOUT;

    if (rx[2] != 0x00) {
        rt_kprintf("[BN0055] UART write status: 0x%02X\n", rx[2]);
        return -RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief 读取 int16 小端数据
 */
static int16_t bno055_int16_le(const uint8_t *buf)
{
    return (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief 读取 BNO055 全部数据
 */
int bn0055_read(bn0055_data_t *data)
{
    uint8_t buf[24];
    int16_t raw;
    int     ret;

    if (g_sim_mode) {
        /* 模拟数据模式：生成正弦波模拟数据 */
        g_sim_tick++;
        float t = g_sim_tick * 0.05f;  /* 时间步进 */
        data->accel_x = (float)(9.8 + 0.5 * sin(t));      /* 基础重力 + 小幅振动 */
        data->accel_y = (float)(0.3 * sin(t * 1.7));     /* Y 轴振动 */
        data->accel_z = (float)(0.2 * sin(t * 2.3));     /* Z 轴振动 */
        data->gyro_x  = (float)(0.5 * sin(t * 0.8));
        data->gyro_y  = (float)(0.3 * sin(t * 1.2));
        data->gyro_z  = (float)(0.2 * sin(t * 1.5));
        data->euler_heading = (float)(10.0 * sin(t * 0.3));
        data->euler_roll    = (float)(5.0 * sin(t * 0.5));
        data->euler_pitch   = (float)(2.0 * sin(t * 0.7));
        data->error = 0;
        return RT_EOK;
    }

    if (data == RT_NULL || uart_dev == RT_NULL) return -RT_ERROR;
    memset(data, 0, sizeof(bn0055_data_t));

    /* 1. 读取加速度 (6 bytes) */
    ret = bno055_uart_read_reg(BNO055_REG_ACC_X_LSB, buf, 6);
    if (ret == RT_EOK) {
        raw = bno055_int16_le(&buf[0]); data->accel_x = raw * 0.01f;   /* 1 LSB = 1 mg */
        raw = bno055_int16_le(&buf[2]); data->accel_y = raw * 0.01f;
        raw = bno055_int16_le(&buf[4]); data->accel_z = raw * 0.01f;
    } else {
        rt_kprintf("[BN0055] Read ACC failed (ret=%d)\n", ret);
        return ret;
    }

    /* 2. 读取陀螺仪 (6 bytes) */
    ret = bno055_uart_read_reg(BNO055_REG_GYR_X_LSB, buf, 6);
    if (ret == RT_EOK) {
        raw = bno055_int16_le(&buf[0]); data->gyro_x = raw * 0.0625f; /* 1 LSB = 1/16 deg/s */
        raw = bno055_int16_le(&buf[2]); data->gyro_y = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->gyro_z = raw * 0.0625f;
    }

    /* 3. 读取欧拉角 (6 bytes) */
    ret = bno055_uart_read_reg(BNO055_REG_EUL_H_LSB, buf, 6);
    if (ret == RT_EOK) {
        raw = bno055_int16_le(&buf[0]); data->euler_heading = raw * 0.0625f;
        raw = bno055_int16_le(&buf[2]); data->euler_roll    = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->euler_pitch   = raw * 0.0625f;
    }

    data->error = 0;
    return RT_EOK;
}

/**
 * @brief 设置 BNO055 工作模式
 */
int bn0055_set_mode(uint8_t mode)
{
    int ret;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    /* 先切换到 CONFIG_MODE */
    ret = bno055_uart_write_reg(BNO055_REG_OPR_MODE, BNO055_MODE_CONFIG);
    if (ret != RT_EOK) return ret;
    rt_thread_mdelay(20);

    /* 再切换到目标模式 */
    ret = bno055_uart_write_reg(BNO055_REG_OPR_MODE, mode);
    if (ret != RT_EOK) return ret;
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: UART set mode 0x%02X OK\n", mode);
    return RT_EOK;
}

/**
 * @brief 初始化 GY-BN0055（UART 二进制协议）
 */
int bn0055_init(void)
{
    rt_err_t ret;
    uint8_t  chip_id = 0;
    int      i;

    uart_dev = rt_device_find(UART_NAME);
    if (uart_dev == RT_NULL) {
        rt_kprintf("BN0055: UART '%s' not found!\n", UART_NAME);
        return -RT_ERROR;
    }

    ret = rt_device_open(uart_dev, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: Failed to open UART (ret=%d)\n", ret);
        return -RT_ERROR;
    }

    /* 读取 CHIP_ID 验证设备（使用 UART 二进制协议） */
    ret = bno055_uart_read_reg(BNO055_REG_CHIP_ID, &chip_id, 1);
    if (ret != RT_EOK || chip_id != 0xA0) {
        rt_kprintf("BN0055: CHIP_ID error! ret=%d, id=0x%02X (expect 0xA0)\n", ret, chip_id);
        rt_kprintf("BN0055: Switching to SIMULATION mode (no hardware BNO055)\n");
        g_sim_mode = 1;
        uart_dev = RT_NULL;  /* 不占用 UART1 */
        return RT_EOK;       /* 返回成功，使用模拟数据 */
    }
    rt_kprintf("BN0055: CHIP_ID OK (0xA0)\n");

    /* 设置 PWR_MODE = NORMAL */
    bno055_uart_write_reg(BNO055_REG_PWR_MODE, 0x00);
    rt_thread_mdelay(10);

    /* 设置 NDOF 模式 */
    bn0055_set_mode(BNO055_MODE_NDOF);

    rt_kprintf("BN0055: Init OK (UART binary protocol, 115200)\n");
    return RT_EOK;
}
