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

/* 远程命令接收接口（从 UART1 共享通道中提取非 BNO055 协议数据） */
#define CMD_BUF_SIZE 128

/**
 * @brief 获取一条从 UART1 接收到的远程命令
 * @param buf 输出缓冲区，至少 CMD_BUF_SIZE 字节
 * @param timeout_ms 等待超时（毫秒），0 表示非阻塞
 * @return RT_EOK 成功，-RT_ETIMEOUT 超时，-RT_ERROR 错误
 */
int cmd_get_remote_command(char *buf, int timeout_ms);

/**
 * @brief 初始化远程命令接收信号量（应在系统启动后调用一次）
 */
void cmd_sem_init(void);

#endif /* __GY_BN0055_H__ */

