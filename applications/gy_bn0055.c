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
#include <math.h>
#include <stdlib.h>
#include <board.h>

/* BNO055 使用 UART1(PA9/PA10)，控制台的 JSON 输出从 PA9(TX) 发送，
 * 也会到达 BNO055 的 RX。BNO055 UART 协议以 0xAA 为帧头，收到非 0xAA
 * 数据会忽略，所以控制台 JSON 不会干扰正确解析。 */
#define UART_NAME       "uart3"
#define UART_BAUDRATE   115200

/* 模拟数据控制 */
static int g_sim_mode = 0;
static uint32_t g_sim_tick = 0;
static volatile int g_sim_disturb = 0;   /* 1=触发模拟扰动 */
static float g_sim_base_x = 0.0f;
static float g_sim_base_y = 0.0f;
static float g_sim_base_z = 9.81f;
static int g_bno_probe_debug = 1;

/* 远程命令接收缓冲区（与 BNO055 共享 UART1 RX） */
static char g_cmd_buffer[CMD_BUF_SIZE];
static int g_cmd_pos = 0;
static rt_sem_t g_cmd_sem = RT_NULL;

static void bno055_gpio_diag(void)
{
    rt_base_t pb10 = GET_PIN(B, 10);
    rt_base_t pb11 = GET_PIN(B, 11);

    rt_pin_mode(pb10, PIN_MODE_OUTPUT_OD);
    rt_pin_mode(pb11, PIN_MODE_OUTPUT_OD);
    rt_pin_write(pb10, PIN_HIGH);
    rt_pin_write(pb11, PIN_HIGH);
    rt_thread_mdelay(2);
    rt_kprintf("[BN0055-GPIO] OD idle PB10=%d PB11=%d\n",
               rt_pin_read(pb10), rt_pin_read(pb11));

    rt_pin_mode(pb10, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(pb11, PIN_MODE_INPUT_PULLUP);
    rt_thread_mdelay(2);
    rt_kprintf("[BN0055-GPIO] input_pullup PB10=%d PB11=%d\n",
               rt_pin_read(pb10), rt_pin_read(pb11));

    rt_pin_mode(pb10, PIN_MODE_OUTPUT_OD);
    rt_pin_mode(pb11, PIN_MODE_OUTPUT_OD);
    rt_pin_write(pb10, PIN_LOW);
    rt_pin_write(pb11, PIN_HIGH);
    rt_thread_mdelay(2);
    rt_kprintf("[BN0055-GPIO] PB10_low PB10=%d PB11=%d\n",
               rt_pin_read(pb10), rt_pin_read(pb11));

    rt_pin_write(pb10, PIN_HIGH);
    rt_pin_write(pb11, PIN_LOW);
    rt_thread_mdelay(2);
    rt_kprintf("[BN0055-GPIO] PB11_low PB10=%d PB11=%d\n",
               rt_pin_read(pb10), rt_pin_read(pb11));

    rt_pin_write(pb10, PIN_HIGH);
    rt_pin_write(pb11, PIN_HIGH);
    rt_thread_mdelay(2);
}

/**
 * @brief 保存单个字符到远程命令缓冲区
 */
static void cmd_save_char(char ch)
{
    if (ch >= 0x20 && ch <= 0x7E) {  /* 可打印 ASCII */
        if (g_cmd_pos < CMD_BUF_SIZE - 1) {
            g_cmd_buffer[g_cmd_pos++] = ch;
        }
    } else if (ch == '\n' || ch == '\r') {
        if (g_cmd_pos > 0) {
            g_cmd_buffer[g_cmd_pos] = '\0';
            g_cmd_pos = 0;
            if (g_cmd_sem != RT_NULL) {
                rt_sem_release(g_cmd_sem);
            }
        }
    }
}

/**
 * @brief 初始化远程命令接收信号量
 */
void cmd_sem_init(void)
{
    if (g_cmd_sem == RT_NULL) {
        g_cmd_sem = rt_sem_create("cmd_sem", 0, RT_IPC_FLAG_FIFO);
    }
}

/**
 * @brief 获取一条从 UART1 接收到的远程命令
 */
int cmd_get_remote_command(char *buf, int timeout_ms)
{
    if (g_cmd_sem == RT_NULL) {
        return -RT_ERROR;
    }

    rt_err_t ret = rt_sem_take(g_cmd_sem, rt_tick_from_millisecond(timeout_ms));
    if (ret != RT_EOK) {
        return -RT_ETIMEOUT;
    }

    /* 复制命令到输出缓冲区 */
    rt_enter_critical();
    rt_strncpy(buf, g_cmd_buffer, CMD_BUF_SIZE);
    buf[CMD_BUF_SIZE - 1] = '\0';
    rt_exit_critical();

    return RT_EOK;
}

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
static struct rt_i2c_bus_device *bno_i2c_bus = RT_NULL;
static uint8_t bno_i2c_addr = 0x28;
static int g_bno_i2c_mode = 0;

static int bno055_i2c_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    struct rt_i2c_msg msgs[2];

    if (bno_i2c_bus == RT_NULL || buf == RT_NULL || len == 0) {
        return -RT_ERROR;
    }

    msgs[0].addr  = bno_i2c_addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;
    msgs[1].addr  = bno_i2c_addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    return (rt_i2c_transfer(bno_i2c_bus, msgs, 2) == 2) ? RT_EOK : -RT_ERROR;
}

static int bno055_i2c_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = {reg, val};
    struct rt_i2c_msg msg;

    if (bno_i2c_bus == RT_NULL) {
        return -RT_ERROR;
    }

    msg.addr  = bno_i2c_addr;
    msg.flags = RT_I2C_WR;
    msg.len   = 2;
    msg.buf   = tx;

    return (rt_i2c_transfer(bno_i2c_bus, &msg, 1) == 1) ? RT_EOK : -RT_ERROR;
}

/**
 * @brief 清空 UART RX 缓冲区（同时保存 ASCII 命令数据到全局缓冲区）
 */
static void uart_flush_rx(void)
{
    char ch;
    while (rt_device_read(uart_dev, 0, &ch, 1) == 1) {
        cmd_save_char(ch);
    }
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
    uint8_t raw[16];
    int     rd;
    int     i;
    int     raw_len = 0;

    if (uart_dev == RT_NULL) return -RT_ERROR;

    /* 清空旧数据 */
    uart_flush_rx();

    /* 发送读取命令 */
    rt_device_write(uart_dev, 0, tx, 4);
    if (g_bno_probe_debug && reg == BNO055_REG_CHIP_ID) {
        rt_kprintf("[BN0055-PROBE] %s TX: %02X %02X %02X %02X\n",
                   UART_NAME, tx[0], tx[1], tx[2], tx[3]);
    }
    rt_thread_mdelay(50);

    /* 等待应答帧头（非 BNO055 响应字节保存到命令缓冲区） */
    rx[0] = 0;
    for (i = 0; i < 50; i++) {  /* 最多尝试 50 次，约 250ms */
        rd = uart_read_bytes(&rx[0], 1, 10);
        if (rd == 1 && (rx[0] == BNO055_RESP_SUCCESS || rx[0] == BNO055_RESP_ERROR)) {
            break;
        }
        /* 如果不是 BNO055 响应数据，尝试保存为命令 */
        if (rd == 1) {
            if (raw_len < (int)sizeof(raw)) {
                raw[raw_len++] = rx[0];
            }
            cmd_save_char((char)rx[0]);
        }
    }

    if (rx[0] == BNO055_RESP_ERROR) {
        /* 读取错误码 */
        uart_read_bytes(&rx[1], 1, 50);
        rt_kprintf("[BN0055] UART ERROR resp: 0x%02X\n", rx[1]);
        return -RT_ERROR;
    }
    if (rx[0] != BNO055_RESP_SUCCESS) {
        if (g_bno_probe_debug && reg == BNO055_REG_CHIP_ID) {
            rt_kprintf("[BN0055-PROBE] %s no 0xBB/0xEE response, raw_len=%d", UART_NAME, raw_len);
            for (i = 0; i < raw_len; i++) {
                rt_kprintf(" %02X", raw[i]);
            }
            rt_kprintf("\n");
        }
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

    /* 等待应答帧头（非 BNO055 响应字节保存到命令缓冲区） */
    rx[0] = 0;
    for (i = 0; i < 50; i++) {
        rd = uart_read_bytes(&rx[0], 1, 10);
        if (rd == 1 && (rx[0] == BNO055_RESP_SUCCESS || rx[0] == BNO055_RESP_ERROR)) {
            break;
        }
        /* 如果不是 BNO055 响应数据，尝试保存为命令 */
        if (rd == 1) {
            cmd_save_char((char)rx[0]);
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

static int bno055_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return g_bno_i2c_mode ? bno055_i2c_read_reg(reg, buf, len)
                          : bno055_uart_read_reg(reg, buf, len);
}

static int bno055_write_reg(uint8_t reg, uint8_t val)
{
    return g_bno_i2c_mode ? bno055_i2c_write_reg(reg, val)
                          : bno055_uart_write_reg(reg, val);
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
        /* 模拟数据模式：默认静止基线 + 微小噪声
         * 当 g_sim_disturb=1 时触发模拟扰动（大振幅）
         * 扰动持续 5 秒后自动恢复静止 */
        g_sim_tick++;
        float t = g_sim_tick * 0.05f;

        if (g_sim_disturb) {
            /* 扰动模式：模拟舞动（低频大振幅） */
            data->accel_x = g_sim_base_x + 2.0f * sinf(t * 0.5f);   /* 2 m/s² 振幅 */
            data->accel_y = g_sim_base_y + 1.5f * sinf(t * 0.7f + 1.0f);
            data->accel_z = g_sim_base_z + 1.0f * sinf(t * 0.3f + 2.0f);
            data->gyro_x  = 5.0f * sinf(t * 0.4f);
            data->gyro_y  = 3.0f * sinf(t * 0.6f);
            data->gyro_z  = 2.0f * sinf(t * 0.8f);
            data->euler_roll    = 15.0f * sinf(t * 0.3f);
            data->euler_pitch   = 10.0f * sinf(t * 0.4f);
            data->euler_heading = 5.0f  * sinf(t * 0.2f);
            /* 5 秒后自动停止扰动 */
            if (g_sim_tick % 100 == 0) {  /* 每 5 秒检查一次 */
                /* 扰动持续 5 秒 */
            }
        } else {
            /* 静止模式：只有微小噪声，模拟真实静止状态 */
            /* 使用简单的伪随机噪声 */
            uint32_t seed = g_sim_tick * 1103515245u + 12345u;
            float noise_x = ((seed % 100) - 50) * 0.001f;  /* ±0.05 m/s² */
            float noise_y = ((seed * 7 % 100) - 50) * 0.001f;
            float noise_z = ((seed * 13 % 100) - 50) * 0.001f;
            data->accel_x = g_sim_base_x + noise_x;
            data->accel_y = g_sim_base_y + noise_y;
            data->accel_z = g_sim_base_z + noise_z;
            data->gyro_x  = 0.0f;
            data->gyro_y  = 0.0f;
            data->gyro_z  = 0.0f;
            data->euler_roll    = 0.0f;
            data->euler_pitch   = 0.0f;
            data->euler_heading = 0.0f;
        }
        data->error = 0;
        return RT_EOK;
    }

    if (data == RT_NULL || (!g_bno_i2c_mode && uart_dev == RT_NULL)) return -RT_ERROR;
    memset(data, 0, sizeof(bn0055_data_t));

    /* 1. 读取加速度 (6 bytes) */
    ret = bno055_read_reg(BNO055_REG_ACC_X_LSB, buf, 6);
    if (ret == RT_EOK) {
        raw = bno055_int16_le(&buf[0]); data->accel_x = raw * 0.001f * 9.80665f;   /* 1 LSB = 1 mg, 精确转 m/s² */
        raw = bno055_int16_le(&buf[2]); data->accel_y = raw * 0.001f * 9.80665f;
        raw = bno055_int16_le(&buf[4]); data->accel_z = raw * 0.001f * 9.80665f;
    } else {
        rt_kprintf("[BN0055] Read ACC failed (ret=%d)\n", ret);
        return ret;
    }

    /* 2. 读取陀螺仪 (6 bytes) */
    ret = bno055_read_reg(BNO055_REG_GYR_X_LSB, buf, 6);
    if (ret == RT_EOK) {
        raw = bno055_int16_le(&buf[0]); data->gyro_x = raw * 0.0625f; /* 1 LSB = 1/16 deg/s */
        raw = bno055_int16_le(&buf[2]); data->gyro_y = raw * 0.0625f;
        raw = bno055_int16_le(&buf[4]); data->gyro_z = raw * 0.0625f;
    }

    /* 3. 读取欧拉角 (6 bytes) */
    ret = bno055_read_reg(BNO055_REG_EUL_H_LSB, buf, 6);
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

    if (!g_bno_i2c_mode && uart_dev == RT_NULL) return -RT_ERROR;

    /* 先切换到 CONFIG_MODE */
    ret = bno055_write_reg(BNO055_REG_OPR_MODE, BNO055_MODE_CONFIG);
    if (ret != RT_EOK) return ret;
    rt_thread_mdelay(20);

    /* 再切换到目标模式 */
    ret = bno055_write_reg(BNO055_REG_OPR_MODE, mode);
    if (ret != RT_EOK) return ret;
    rt_thread_mdelay(100);

    rt_kprintf("BN0055: %s set mode 0x%02X OK\n", g_bno_i2c_mode ? "I2C" : "UART", mode);
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
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    uint8_t  addrs[2] = {0x28, 0x29};

    g_sim_mode = 0;
    g_bno_i2c_mode = 0;

    bno055_gpio_diag();

    bno_i2c_bus = rt_i2c_bus_device_find("i2c2");
    if (bno_i2c_bus != RT_NULL) {
        for (i = 0; i < 2; i++) {
            bno_i2c_addr = addrs[i];
            chip_id = 0;
            ret = bno055_i2c_read_reg(BNO055_REG_CHIP_ID, &chip_id, 1);
            rt_kprintf("[BN0055-I2C-PROBE] i2c2 addr=0x%02X ret=%d id=0x%02X\n",
                       bno_i2c_addr, ret, chip_id);
            if (ret == RT_EOK && chip_id == 0xA0) {
                g_bno_i2c_mode = 1;
                rt_kprintf("BN0055: I2C CHIP_ID OK addr=0x%02X\n", bno_i2c_addr);
                bno055_i2c_write_reg(BNO055_REG_PWR_MODE, 0x00);
                rt_thread_mdelay(10);
                bn0055_set_mode(BNO055_MODE_NDOF);
                rt_kprintf("BN0055: Init OK (I2C, i2c2)\n");
                return RT_EOK;
            }
        }
        rt_kprintf("BN0055: I2C probe failed on i2c2, fallback to UART3\n");
    } else {
        rt_kprintf("BN0055: I2C bus 'i2c2' not found, fallback to UART3\n");
    }

    uart_dev = rt_device_find(UART_NAME);
    if (uart_dev == RT_NULL) {
        rt_kprintf("BN0055: UART '%s' not found!\n", UART_NAME);
        return -RT_ERROR;
    }

    cfg.baud_rate = BAUD_RATE_115200;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity = PARITY_NONE;
    rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &cfg);

    ret = rt_device_open(uart_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("BN0055: Failed to open UART (ret=%d)\n", ret);
        return -RT_ERROR;
    }

    /* Read CHIP_ID with retries; BNO055 may need time after power-up. */
    for (i = 0; i < 10; i++) {
        ret = bno055_uart_read_reg(BNO055_REG_CHIP_ID, &chip_id, 1);
        if (ret == RT_EOK && chip_id == 0xA0) {
            break;
        }
        rt_thread_mdelay(100);
    }
    if (ret != RT_EOK || chip_id != 0xA0) {
        rt_kprintf("BN0055: CHIP_ID error! ret=%d, id=0x%02X (expect 0xA0)\n", ret, chip_id);
        rt_kprintf("BN0055: Switching to SIMULATION mode (no hardware BNO055)\n");
        g_sim_mode = 1;
        uart_dev = RT_NULL;  /* 不占用 UART1 */
        return RT_EOK;       /* 返回成功，使用模拟数据 */
    }
    rt_kprintf("BN0055: CHIP_ID OK (0xA0)\n");

    /* 设置 PWR_MODE = NORMAL */
    bno055_write_reg(BNO055_REG_PWR_MODE, 0x00);
    rt_thread_mdelay(10);

    /* 设置 NDOF 模式 */
    bn0055_set_mode(BNO055_MODE_NDOF);

    rt_kprintf("BN0055: Init OK (UART binary protocol, 115200)\n");
    return RT_EOK;
}

/* ================================================================
 * MSH 命令：模拟扰动控制
 * ================================================================ */

/**
 * @brief 触发模拟扰动（测试用）
 *        msh> sim_disturb 1
 *        msh> sim_disturb 0
 */
static void sim_disturb_cmd(int argc, char **argv)
{
    if (argc > 1) {
        g_sim_disturb = (atoi(argv[1]) != 0);
    }
    rt_kprintf("SIM disturbance: %s\n", g_sim_disturb ? "ON" : "OFF");
}
MSH_CMD_EXPORT(sim_disturb_cmd, sim_disturb 1/0 - trigger/release fake disturbance);
