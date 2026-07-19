/*
 * galloping_detect.h - 电缆舞动状态检测算法
 *
 * 三层处理管道：
 *   1. 数据预处理：滑动均值滤波(MAF) + 去直流 + 重力分离
 *   2. 特征提取：  振幅峰-峰值、过零频率、振动能量(RMS)
 *   3. 状态判断：  基于特征向量的分级判定
 *
 * 适用场景：架空电缆、输电线、桥梁拉索等柔性结构的振动监测
 */

#ifndef __GALLOPING_DETECT_H__
#define __GALLOPING_DETECT_H__

#include <rtthread.h>
#include <stdint.h>

/* ================================================================
 * 1. 常数定义
 * ================================================================ */

/* 采样与窗口 */
#define GD_SAMPLE_RATE_HZ       20          /* 采样率：20 Hz（50ms/次） */
#define GD_WINDOW_SIZE          64          /* 分析窗口：64 个采样点 ≈ 3.2s */
#define GD_MAF_WINDOW           8           /* 滑动均值滤波窗口大小 */
#define GD_OVERLAP              16          /* 窗口重叠 25%，即 16 点步进 */

/* 频率参数（电缆舞动典型频率范围 0.1~3 Hz） */
#define GD_FREQ_MIN_HZ          0.1f        /* 最小检测频率 */
#define GD_FREQ_MAX_HZ          5.0f        /* 最大检测频率 */

/* 重力加速度阈值 */
#define GD_GRAVITY_THRESHOLD    0.8f        /* 重力分量阈值（g） */
#define GD_GRAVITY               9.81f      /* 标准重力加速度 */

/* 状态判定阈值（基于去直流后的动态加速度，单位 m/s²）
 * 阈值基于电缆舞动实际物理特征设定：
 *   - 静止噪声：±0.1 m/s² 以下
 *   - 微风振动：0.2~0.5 m/s²（高频小振幅）
 *   - 中等舞动：0.5~2.0 m/s²（典型电缆舞动）
 *   - 剧烈舞动：>2.0 m/s²（大振幅低频）
 *   - 覆冰舞动：大振幅 + 极低频 + 扭转
 */
#define GD_IDLE_AMP_MAX         0.20f       /* 静止：峰-峰值 < 0.20 m/s² */
#define GD_IDLE_RMS_MAX         0.15f       /* 静止：RMS < 0.15 m/s² */
#define GD_BREEZE_AMP_MIN       0.20f       /* 微风：振幅 ≥ 0.20 */
#define GD_BREEZE_AMP_MAX       0.60f       /* 微风：振幅 < 0.60 */
#define GD_BREEZE_FREQ_MIN      1.0f        /* 微风：频率 > 1.0 Hz */
#define GD_MODERATE_AMP_MIN     0.60f       /* 中等：振幅 ≥ 0.60 */
#define GD_MODERATE_AMP_MAX     2.50f       /* 中等：振幅 < 2.50 */
#define GD_MODERATE_FREQ_MIN    0.3f        /* 中等：频率 > 0.3 Hz */
#define GD_SEVERE_AMP_MIN       2.50f       /* 剧烈：振幅 ≥ 2.50 */
#define GD_SEVERE_FREQ_MAX      0.5f        /* 剧烈：频率 < 0.5 Hz */
#define GD_ICE_AMP_MIN          1.50f       /* 覆冰：振幅 ≥ 1.50 */
#define GD_ICE_FREQ_MAX         0.3f        /* 覆冰：频率 < 0.3 Hz */
#define GD_ICE_TORSION_MIN      10.0f       /* 覆冰：扭转 > 10° */

/* ================================================================
 * 2. 舞动状态枚举
 * ================================================================ */
typedef enum {
    GD_STATE_IDLE     = 0,   /* 静止/无振动 */
    GD_STATE_BREEZE   = 1,   /* 微风振动（振幅 < 0.2m，f > 1Hz） */
    GD_STATE_MODERATE = 2,   /* 中等舞动（振幅 0.2~0.5m，f 0.5~1Hz） */
    GD_STATE_SEVERE   = 3,   /* 剧烈舞动（振幅 0.5~2m，f 0.1~0.5Hz） */
    GD_STATE_ICE      = 4,   /* 覆冰舞动（振幅大 + 频率极低 + 扭转） */
    GD_STATE_UNKNOWN  = 5    /* 传感器异常 / 数据不足 */
} galloping_state_t;

/* ================================================================
 * 3. 加速度样本
 * ================================================================ */
typedef struct {
    float x, y, z;              /* 原始加速度 (m/s²) */
    float x_filt, y_filt, z_filt; /* 滤波后 */
    float magnitude;            /* 合加速度幅值 */
    float dc_x, dc_y, dc_z;     /* 直流分量（重力投影） */
} gd_sample_t;

/* ================================================================
 * 4. 特征向量（每个分析窗口输出一组）
 * ================================================================ */
typedef struct {
    /* 振幅特征 */
    float amp_x_pp;             /* X 轴峰-峰值 (m/s²) */
    float amp_y_pp;             /* Y 轴峰-峰值 */
    float amp_z_pp;             /* Z 轴峰-峰值 */
    float amp_dominant;         /* 主导轴振幅 (m/s²) */
    float displacement_est;     /* 估算位移幅值 (m)，通过二次积分 */

    /* 频率特征 */
    float dominant_freq;        /* 主导频率 (Hz)，过零法估计 */
    float zero_cross_rate;      /* 过零率 (次/秒) */

    /* 能量特征 */
    float rms_accel;            /* 加速度均方根 (m/s²) */
    float vibr_energy;          /* 振动能量指标 */

    /* 扭转特征 */
    float torsion_deg;          /* 扭转角度变化 (°) —— 来自欧拉角 roll/pitch 变化 */

    /* 统计特征 */
    float mean_mag;             /* 合加速度均值 */
    float std_mag;              /* 合加速度标准差 */

    /* 判定的状态 */
    galloping_state_t state;
    float             confidence; /* 置信度 0.0~1.0 */
} gd_feature_t;

/* ================================================================
 * 5. 检测器句柄
 * ================================================================ */
typedef struct gd_detector gd_detector_t;

/* ================================================================
 * 6. 公开 API
 * ================================================================ */

/**
 * @brief 创建电缆舞动检测器实例
 * @param name      实例名称（调试用）
 * @param samp_rate 采样率 (Hz)，建议 10~50
 * @param win_size  分析窗口大小（采样点数），建议 32~128
 * @return 检测器句柄，失败返回 RT_NULL
 */
gd_detector_t *gd_create(const char *name, int samp_rate, int win_size);

/**
 * @brief 喂入一帧加速度数据（BNO055 原始数据）
 *        内部自动完成滤波、窗口管理、特征提取。
 *        当累积满一个分析窗口时自动触发状态判定。
 * @param det  检测器句柄
 * @param accel_x/y/z  原始加速度 (m/s²)
 * @param roll/pitch   欧拉角 (°)，用于扭转检测
 * @return 若有新特征产生则返回特征指针（内部 static buffer），否则返回 RT_NULL
 */
const gd_feature_t *gd_feed(gd_detector_t *det,
                            float accel_x, float accel_y, float accel_z,
                            float roll, float pitch);

/**
 * @brief 手动触发一次状态判定（强制对当前窗口做分析）
 */
const gd_feature_t *gd_analyze(gd_detector_t *det);

/**
 * @brief 获取最后一次分析的特征
 */
const gd_feature_t *gd_last_feature(gd_detector_t *det);

/**
 * @brief 获取当前窗口内采样点数
 */
int gd_sample_count(gd_detector_t *det);

/**
 * @brief 重置检测器（清空窗口，重算直流分量）
 */
void gd_reset(gd_detector_t *det);

/**
 * @brief 销毁检测器
 */
void gd_destroy(gd_detector_t *det);

/**
 * @brief 将状态枚举转为可读字符串
 */
const char *gd_state_name(galloping_state_t state);

/**
 * @brief 打印特征向量详情
 */
void gd_feature_print(const gd_feature_t *f);

/**
 * @brief 获取最新样本的去直流后的动态加速度
 * @param det 检测器句柄
 * @param dx, dy, dz 输出动态加速度 (m/s²)，校准期间返回 0
 */
void gd_get_dynamic_accel(gd_detector_t *det, float *dx, float *dy, float *dz);

#endif /* __GALLOPING_DETECT_H__ */
