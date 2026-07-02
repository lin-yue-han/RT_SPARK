/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-06-25     Administrator       the first version
 */


#ifndef __SHT3X_H__
#define __SHT3X_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

/* SHT3x I2C 地址 */
#define SHT3X_I2C_ADDR          0x44

/* 命令定义 */
#define SHT3X_CMD_MEAS_HIGH     0x2400   // 高重复性测量
#define SHT3X_CMD_MEAS_MED      0x240B   // 中重复性测量
#define SHT3X_CMD_MEAS_LOW      0x2416   // 低重复性测量
#define SHT3X_CMD_READ_STATUS   0xF32D   // 读取状态寄存器
#define SHT3X_CMD_CLEAR_STATUS  0x3041   // 清除状态
#define SHT3X_CMD_SOFT_RESET    0x30A2   // 软复位

/* 传感器数据结构体 */
typedef struct {
    float temperature;   // 温度 (℃)
    float humidity;      // 湿度 (%RH)
    uint16_t status;     // 状态寄存器
    uint8_t error;       // 错误标志
} sht3x_data_t;

/* 函数声明 */
int sht3x_init(void);
int sht3x_read(sht3x_data_t *data);
int sht3x_soft_reset(void);
int sht3x_read_status(uint16_t *status);

#endif /* __SHT3X_H__ */

