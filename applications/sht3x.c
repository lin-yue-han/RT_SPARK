/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2026-06-25     Administrator the first version
 */
#include "sht3x.h"
#include <stdint.h>
#include <string.h>

/* RT-Thread 软件 I2C 框架注册名为 "i2c1"（与 BSP_USING_I2C1 对应） */
#define SHT3X_I2C_BUS_NAME  "i2c1"

static struct rt_i2c_bus_device *sht3x_i2c_bus = RT_NULL;

/* ------------------------------------------------------------------ */
/* CRC-8 校验（多项式 0x31，初值 0xFF，SHT3x 规格书 4.12 节）          */
/* ------------------------------------------------------------------ */
static uint8_t sht3x_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    uint8_t i, b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief 向 SHT3x 发送 2 字节命令
 */
static int sht3x_send_cmd(uint16_t cmd)
{
    uint8_t cmd_buf[2];
    cmd_buf[0] = (cmd >> 8) & 0xFF;
    cmd_buf[1] = cmd & 0xFF;

    struct rt_i2c_msg msgs = {
        .addr  = SHT3X_I2C_ADDR,
        .flags = RT_I2C_WR,
        .len   = 2,
        .buf   = cmd_buf,
    };

    return (rt_i2c_transfer(sht3x_i2c_bus, &msgs, 1) == 1) ? RT_EOK : -RT_ERROR;
}

/**
 * @brief 读取 SHT3x 测量数据（温度 + 湿度）
 */
int sht3x_read(sht3x_data_t *data)
{
    uint8_t  raw[6];
    uint16_t temp_raw, humi_raw;
    int      ret;

    if (data == RT_NULL || sht3x_i2c_bus == RT_NULL) {
        return -RT_ERROR;
    }

    /* 发送单次测量命令（高重复性，Clock Stretching Disabled） */
    ret = sht3x_send_cmd(SHT3X_CMD_MEAS_HIGH);
    if (ret != RT_EOK) {
        data->error = 1;
        return ret;
    }

    /* 高重复性测量最长需要 15 ms，等待 20 ms 留余量 */
    rt_thread_mdelay(20);

    /* 读取 6 字节：T_MSB T_LSB T_CRC RH_MSB RH_LSB RH_CRC */
    struct rt_i2c_msg msgs = {
        .addr  = SHT3X_I2C_ADDR,
        .flags = RT_I2C_RD,
        .len   = 6,
        .buf   = raw,
    };

    ret = rt_i2c_transfer(sht3x_i2c_bus, &msgs, 1);
    if (ret != 1) {
        data->error = 2;
        return -RT_ERROR;
    }

    /* CRC 校验 */
    if (sht3x_crc8(raw, 2) != raw[2]) {
        rt_kprintf("SHT3X: Temperature CRC error!\n");
        data->error = 3;
        return -RT_ERROR;
    }
    if (sht3x_crc8(raw + 3, 2) != raw[5]) {
        rt_kprintf("SHT3X: Humidity CRC error!\n");
        data->error = 4;
        return -RT_ERROR;
    }

    /* 解析原始值 */
    temp_raw = ((uint16_t)raw[0] << 8) | raw[1];
    humi_raw = ((uint16_t)raw[3] << 8) | raw[4];

    /* 转换公式（SHT3x 数据手册 4.13 节） */
    data->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
    data->humidity    = 100.0f * ((float)humi_raw / 65535.0f);
    data->error       = 0;

    /* 限幅（防止传感器异常值） */
    if (data->temperature < -40.0f) data->temperature = -40.0f;
    if (data->temperature > 125.0f) data->temperature = 125.0f;
    if (data->humidity    <   0.0f) data->humidity    =   0.0f;
    if (data->humidity    > 100.0f) data->humidity    = 100.0f;

    return RT_EOK;
}

/**
 * @brief 软复位 SHT3x
 */
int sht3x_soft_reset(void)
{
    return sht3x_send_cmd(SHT3X_CMD_SOFT_RESET);
}

/**
 * @brief 读取状态寄存器（2 字节有效数据 + 1 字节 CRC，共读 3 字节）
 */
int sht3x_read_status(uint16_t *status)
{
    uint8_t buf[3];
    int     ret;

    if (status == RT_NULL || sht3x_i2c_bus == RT_NULL) {
        return -RT_ERROR;
    }

    ret = sht3x_send_cmd(SHT3X_CMD_READ_STATUS);
    if (ret != RT_EOK) return ret;

    rt_thread_mdelay(2);

    struct rt_i2c_msg msgs = {
        .addr  = SHT3X_I2C_ADDR,
        .flags = RT_I2C_RD,
        .len   = 3,   /* 状态寄存器返回 2 字节数据 + 1 字节 CRC */
        .buf   = buf,
    };

    ret = rt_i2c_transfer(sht3x_i2c_bus, &msgs, 1);
    if (ret != 1) return -RT_ERROR;

    if (sht3x_crc8(buf, 2) != buf[2]) {
        rt_kprintf("SHT3X: Status CRC error!\n");
        return -RT_ERROR;
    }

    *status = ((uint16_t)buf[0] << 8) | buf[1];
    return RT_EOK;
}

/**
 * @brief 初始化 SHT3x（软件 I2C1，SCL→PD10，SDA→PD8）
 */
int sht3x_init(void)
{
    int      ret;
    uint16_t status;

    /* 查找软件 I2C 总线设备（名称由 BSP_USING_I2C1 注册为 "i2c1"） */
    sht3x_i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(SHT3X_I2C_BUS_NAME);
    if (sht3x_i2c_bus == RT_NULL) {
        rt_kprintf("SHT3X: I2C bus '%s' not found!\n", SHT3X_I2C_BUS_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("SHT3X: I2C bus '%s' found\n", SHT3X_I2C_BUS_NAME);

    /* 软复位，等待传感器就绪 */
    ret = sht3x_soft_reset();
    if (ret != RT_EOK) {
        rt_kprintf("SHT3X: Soft reset failed (ret=%d)\n", ret);
        return ret;
    }
    rt_thread_mdelay(15);  /* 复位后至少等待 1 ms，给 15 ms 余量 */

    /* 读取状态寄存器，验证通信 */
    ret = sht3x_read_status(&status);
    if (ret == RT_EOK) {
        rt_kprintf("SHT3X: Status = 0x%04X\n", status);
        if (status & 0x0001) {
            rt_kprintf("SHT3X: Warning - CRC error bit set in status\n");
        }
    } else {
        rt_kprintf("SHT3X: Warning - Cannot read status register\n");
    }

    rt_kprintf("SHT3X: Init OK\n");
    return RT_EOK;
}
