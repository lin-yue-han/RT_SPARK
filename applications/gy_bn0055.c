/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2026-06-25     Administrator the first version
 * 2026-07-18     Administrator fix: rewrite to I2C mode (BNO055 UART binary protocol failed)
 */
#include "gy_bn0055.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* BNO055 使用 I2C1（与 SHT3X 共用），地址 0x28（ADR=GND）
 * 如果硬件 ADR 接 VCC，请改为 0x29 */
#define BNO055_I2C_BUS_NAME  "i2c1"
#define BNO055_ADDR          0x28

/* BNO055 寄存器定义 */
#define BNO055_CHIP_ID       0x00
#define BNO055_PAGE_ID       0x07
#define BNO055_ACC_DATA_X_LSB 0x08
#define BNO055_MAG_DATA_X_LSB 0x0E
#define BNO055_GYR_DATA_X_LSB 0x14
#define BNO055_EUL_HEADING_LSB 0x1A
#define BNO055_QUAT_DATA_W_LSB 0x20
#define BNO055_OPR_MODE      0x3D
#define BNO055_PWR_MODE      0x3E
#define BNO055_SYS_TRIGGER   0x3F

/* 操作模式 */
#define BNO055_MODE_CONFIG   0x00
#define BNO055_MODE_NDOF     0x0C

static struct rt_i2c_bus_device *i2c_dev = RT_NULL;

/**
 * @brief I2C 读取 BNO055 寄存器
 */
static int bno055_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    struct rt_i2c_msg msgs[2];

    msgs[0].addr  = BNO055_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &reg;
    msgs[0].len   = 1;

    msgs[1].addr  = BNO055_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf   = buf;
    msgs[1].len   = len;

    return rt_i2c_transfer(i2c_dev, msgs, 2);
}

/**
 * @brief I2C 写入 BNO055 寄存器
 */
static int bno055_write_reg(uint8_t reg, uint8_t val)
{
    struct rt_i2c_msg msg;
    uint8_t buf[2] = {reg, val};

    msg.addr  = BNO055_ADDR;
    msg.flags = RT_I2C_WR;
    msg.buf   = buf;
    msg.len   = 2;

    return rt_i2c_transfer(i2c_dev, &msg, 1);
}

/**
 * @brief 读取 int16 小端数据
 */
static int16_t bno055_int16_le(const uint8_t *buf)
{
    return (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief 读取 BNO055 全部数据（加速度、陀螺仪、磁力计、欧拉角、四元数）
 */
int bn0055_read(bn0055_data_t *data)
{
    uint8_t buf[24];
    int ret;
    int16_t raw;

    if (data == RT_NULL || i2c_dev == RT_NULL) return -RT_ERROR;

    memset(data, 0, sizeof(bn0055_data_t));

    /* 1. 读取加速度 (6 bytes from 0x08) */
    ret = bno055_read_reg(BNO055_ACC_DATA_X_LSB, buf, 6);
    if (ret == 1) {
        raw = bno055_int16_le(&buf[0]); data->accel_x = raw * 0.01f;   /* 1 LSB = 1 mg ≈ 0.01 m/s² */
        raw = bno055_int16_le(&buf[2]); data->accel_y = raw * 0.01f;
        raw = bno055_int16_le(&buf[4]); data->accel_z = raw * 0.01f;
    } else {
        rt_kprintf("[BN0055] I2C read ACC failed (ret=%d)\n", ret);
    }

    /* 2. 读取磁力计 (6 bytes from 0x0E) */
    ret = bno055_read_reg(BNO055_MAG_DATA_X_LSB, buf, 6);
    if (ret == 1) {
        raw = bno055_int16_le(&buf[0]); data->mag_x = raw * 0.0625f;  /* 1 LSB = 1/16 uT */
        raw = bno055_int16_le(&buf[2]); data->mag_y = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->mag_z = raw * 0.0625f;
    }

    /* 3. 读取陀螺仪 (6 bytes from 0x14) */
    ret = bno055_read_reg(BNO055_GYR_DATA_X_LSB, buf, 6);
    if (ret == 1) {
        raw = bno055_int16_le(&buf[0]); data->gyro_x = raw * 0.0625f; /* 1 LSB = 1/16 deg/s */
        raw = bno055_int16_le(&buf[2]); data->gyro_y = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->gyro_z = raw * 0.0625f;
    }

    /* 4. 读取欧拉角 (6 bytes from 0x1A) */
    ret = bno055_read_reg(BNO055_EUL_HEADING_LSB, buf, 6);
    if (ret == 1) {
        raw = bno055_int16_le(&buf[0]); data->euler_heading = raw * 0.0625f; /* 1 LSB = 1/16 deg */
        raw = bno055_int16_le(&buf[2]); data->euler_roll    = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->euler_pitch   = raw * 0.0625f;
    }

    /* 5. 读取四元数 (8 bytes from 0x20) */
    ret = bno055_read_reg(BNO055_QUAT_DATA_W_LSB, buf, 8);
    if (ret == 1) {
        data->quaternion_w = bno055_int16_le(&buf[0]) * 0.000061035f; /* 1/2^14 */
        data->quaternion_x = bno055_int16_le(&buf[2]) * 0.000061035f;
        data->quaternion_y = bno055_int16_le(&buf[4]) * 0.000061035f;
        data->quaternion_z = bno055_int16_le(&buf[6]) * 0.000061035f;
    }

    data->error = 0;
    return RT_EOK;
}

/**
 * @brief 设置 BNO055 工作模式（通过 I2C）
 */
int bn0055_set_mode(uint8_t mode)
{
    if (i2c_dev == RT_NULL) return -RT_ERROR;

    /* 先切换到 CONFIG_MODE，再切换到目标模式（BNO055 要求） */
    bno055_write_reg(BNO055_OPR_MODE, BNO055_MODE_CONFIG);
    rt_thread_mdelay(20);
    bno055_write_reg(BNO055_OPR_MODE, mode);
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: I2C set mode 0x%02X\n", mode);
    return RT_EOK;
}

/**
 * @brief 初始化 GY-BN0055（I2C 模式）
 *        使用 I2C1 总线，与 SHT3X 共用（PD8/PD10）
 */
int bn0055_init(void)
{
    uint8_t chip_id = 0;
    int ret;

    /* 1. 查找 I2C 总线 */
    i2c_dev = (struct rt_i2c_bus_device *)rt_device_find(BNO055_I2C_BUS_NAME);
    if (i2c_dev == RT_NULL) {
        rt_kprintf("BN0055: I2C bus '%s' not found!\n", BNO055_I2C_BUS_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("BN0055: I2C bus '%s' found\n", BNO055_I2C_BUS_NAME);

    /* 2. 读取 CHIP_ID 验证设备 */
    ret = bno055_read_reg(BNO055_CHIP_ID, &chip_id, 1);
    if (ret != 1) {
        rt_kprintf("BN0055: I2C read CHIP_ID failed (ret=%d)\n", ret);
        return -RT_ERROR;
    }
    if (chip_id != 0xA0) {
        rt_kprintf("BN0055: CHIP_ID error! got 0x%02X, expected 0xA0\n", chip_id);
        return -RT_ERROR;
    }
    rt_kprintf("BN0055: CHIP_ID OK (0xA0)\n");

    /* 3. 设置 PWR_MODE = NORMAL */
    bno055_write_reg(BNO055_PWR_MODE, 0x00);
    rt_thread_mdelay(10);

    /* 4. 设置 OPR_MODE = NDOF（9 轴融合模式） */
    bn0055_set_mode(BNO055_MODE_NDOF);

    rt_kprintf("BN0055: Init OK (I2C mode, addr=0x%02X, NDOF mode)\n", BNO055_ADDR);
    return RT_EOK;
}
