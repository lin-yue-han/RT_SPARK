/*
 * dtu_sender.h - 4G DTU 数据发送模块接口（无线透传模式）
 *
 * 修改说明：
 *   本模块通过 UART2 向 Air778E 4G 模块发送 JSON 数据，
 *   由 Air778E 通过 TCP 透传转发到 frp 服务器，最终到达网页端。
 *   若 UART2 不可用，则回退到控制台 UART1（仅用于调试）。
 *
 * 数据流（无线）：
 *   STM32 (BNO055/SHT3X) → UART2 → Air778E → 4G → frp → bridge.js → 网页
 *
 * 协议格式：每行一条 JSON，以 \n 结尾
 *
 * 使用方式：
 *   dtu_sender_init()              — 初始化（优先 UART2，回退控制台）
 *   dtu_send_galloping(feat)       — 发送舞动检测特征
 *   dtu_send_env(temp, hum)        — 发送温湿度数据
 *   dtu_send_motor(state, pos)     — 发送电机状态
 *   dtu_send_heartbeat()           — 发送心跳包
 */

#ifndef __DTU_SENDER_H__
#define __DTU_SENDER_H__

#include <rtthread.h>
#include "galloping_detect.h"

/* 发送间隔控制 (ms) */
#define DTU_ENV_INTERVAL_MS     5000    /* 温湿度每5秒发一次 */
#define DTU_HEARTBEAT_INTERVAL_MS 10000 /* 心跳每10秒一次 */

/**
 * @brief 初始化 DTU 发送模块（获取控制台设备）
 * @return RT_EOK 成功，其他失败
 */
int dtu_sender_init(void);

/**
 * @brief 发送舞动检测特征到控制台
 * @param feat  舞动特征结构体（来自 galloping_detect）
 * @param state 舞动状态枚举
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_galloping(const gd_feature_t *feat, galloping_state_t state);

/**
 * @brief 发送温湿度数据到控制台
 * @param temperature  温度 (°C)
 * @param humidity     湿度 (%RH)
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_env(float temperature, float humidity);

/**
 * @brief 发送电机/机器人状态到控制台
 * @param motor_state  状态字符串："standby"/"forward"/"backward"/"stop"
 * @param position     当前位置 (0~100 %)
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_motor(const char *motor_state, int position);

/**
 * @brief 发送心跳包到控制台
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_heartbeat(void);

/**
 * @brief 发送系统启动消息到控制台
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_raw_accel(float ax, float ay, float az);

/**
 * @brief 发送系统启动消息到控制台
 * @return 发送字节数，<0 表示失败
 */
int dtu_send_boot(void);

/**
 * @brief 检查 DTU 发送模块是否已初始化
 * @return 1=已初始化, 0=未初始化
 */
int dtu_is_ready(void);

#endif /* __DTU_SENDER_H__ */
