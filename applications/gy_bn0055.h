/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-06-25     Administrator       the first version
 */

#ifndef __GY_BN0055_H__
#define __GY_BN0055_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

/* BNO055 数据结构 */
typedef struct {
    float accel_x;      // 加速度 X (m/s²)
    float accel_y;      // 加速度 Y (m/s²)
    float accel_z;      // 加速度 Z (m/s²)
    float gyro_x;       // 角速度 X (°/s)
    float gyro_y;       // 角速度 Y (°/s)
    float gyro_z;       // 角速度 Z (°/s)
    float mag_x;        // 磁场 X (μT)
    float mag_y;        // 磁场 Y (μT)
    float mag_z;        // 磁场 Z (μT)
    float euler_heading; // 欧拉角 - 航向 (度)
    float euler_roll;    // 欧拉角 - 横滚 (度)
    float euler_pitch;   // 欧拉角 - 俯仰 (度)
    float quaternion_w;  // 四元数 W
    float quaternion_x;  // 四元数 X
    float quaternion_y;  // 四元数 Y
    float quaternion_z;  // 四元数 Z
    uint8_t system_status;
    uint8_t error;
} bn0055_data_t;

/* 函数声明 */
int bn0055_init(void);
int bn0055_read(bn0055_data_t *data);
int bn0055_set_mode(uint8_t mode);

#endif /* __GY_BN0055_H__ */

