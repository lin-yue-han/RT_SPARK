/*
 * galloping_detect.c - 电缆舞动检测算法实现
 *
 * 算法流程：
 *   raw accel → MAF滤波 → 去直流(重力) → 窗口累积 →
 *   振幅分析(峰峰值+位移估算) + 频率分析(过零法) + 能量分析(RMS) →
 *   多维度特征融合 → 状态判定
 */

#include "galloping_detect.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 检测器内部结构 */
struct gd_detector {
    char   name[16];
    int    samp_rate;             /* 采样率 (Hz) */
    int    win_size;              /* 窗口大小 */
    int    maf_win;               /* 滤波窗口 */
    int    overlap;               /* 窗口滑动步进 */

    /* 数据缓冲区 */
    gd_sample_t *buffer;          /* 原始+滤波后样本环形缓冲 */
    int    buf_head;              /* 写指针 */
    int    buf_count;             /* 已填充样本数 */

    /* MAF 滑动均值滤波用环形队列 */
    float *maf_x, *maf_y, *maf_z;
    int    maf_idx;
    float  maf_sum_x, maf_sum_y, maf_sum_z;

    /* 直流分量（重力投影）的指数平滑估计 */
    float  dc_alpha;              /* 平滑系数 */
    float  dc_x, dc_y, dc_z;      /* 当前直流估计 */

    /* 统计缓存 */
    float  amp_sum_x, amp_sum_y, amp_sum_z;

    /* 上一次分析结果 */
    gd_feature_t last_feature;
    int          feature_valid;
};

/* ---- 内部函数声明 ---- */
static void   gd_maf_push(gd_detector_t *det, float x, float y, float z);
static void   gd_update_dc(gd_detector_t *det, float x, float y, float z);
static float  gd_calc_magnitude(float x, float y, float z);
static void   gd_extract_features(gd_detector_t *det, gd_feature_t *f);
static galloping_state_t gd_classify(const gd_feature_t *f);

/* ================================================================
 * 公开 API
 * ================================================================ */

gd_detector_t *gd_create(const char *name, int samp_rate, int win_size)
{
    gd_detector_t *det;

    det = (gd_detector_t *)rt_malloc(sizeof(gd_detector_t));
    if (det == RT_NULL) return RT_NULL;

    memset(det, 0, sizeof(gd_detector_t));

    if (name) {
        rt_strncpy(det->name, name, sizeof(det->name) - 1);
    } else {
        rt_strncpy(det->name, "gd", sizeof(det->name));
    }

    det->samp_rate = (samp_rate > 0) ? samp_rate : GD_SAMPLE_RATE_HZ;
    det->win_size  = (win_size > 0)  ? win_size  : GD_WINDOW_SIZE;
    det->maf_win   = GD_MAF_WINDOW;
    det->overlap   = (win_size / 4 > 0) ? win_size / 4 : 1;
    det->dc_alpha  = 0.001f;    /* 指数平滑系数：慢速跟踪重力 */

    /* 分配缓冲区 */
    det->buffer = (gd_sample_t *)rt_malloc(sizeof(gd_sample_t) * det->win_size);
    det->maf_x  = (float *)rt_malloc(sizeof(float) * det->maf_win);
    det->maf_y  = (float *)rt_malloc(sizeof(float) * det->maf_win);
    det->maf_z  = (float *)rt_malloc(sizeof(float) * det->maf_win);

    if (det->buffer == RT_NULL || det->maf_x == RT_NULL ||
        det->maf_y  == RT_NULL || det->maf_z == RT_NULL) {
        rt_kprintf("[GD] %s: malloc failed!\n", det->name);
        gd_destroy(det);
        return RT_NULL;
    }

    memset(det->buffer, 0, sizeof(gd_sample_t) * det->win_size);
    memset(det->maf_x,  0, sizeof(float) * det->maf_win);
    memset(det->maf_y,  0, sizeof(float) * det->maf_win);
    memset(det->maf_z,  0, sizeof(float) * det->maf_win);

    rt_kprintf("[GD] %s: created (samp=%dHz, win=%d, maf=%d, overlap=%d)\n",
               det->name, det->samp_rate, det->win_size, det->maf_win, det->overlap);

    return det;
}

void gd_destroy(gd_detector_t *det)
{
    if (det == RT_NULL) return;
    if (det->buffer) rt_free(det->buffer);
    if (det->maf_x)  rt_free(det->maf_x);
    if (det->maf_y)  rt_free(det->maf_y);
    if (det->maf_z)  rt_free(det->maf_z);
    rt_free(det);
}

void gd_reset(gd_detector_t *det)
{
    if (det == RT_NULL) return;
    det->buf_head  = 0;
    det->buf_count = 0;
    det->maf_idx   = 0;
    det->maf_sum_x = 0; det->maf_sum_y = 0; det->maf_sum_z = 0;
    det->dc_x = 0; det->dc_y = 0; det->dc_z = 0;
    memset(det->buffer, 0, sizeof(gd_sample_t) * det->win_size);
    memset(det->maf_x,  0, sizeof(float) * det->maf_win);
    memset(det->maf_y,  0, sizeof(float) * det->maf_win);
    memset(det->maf_z,  0, sizeof(float) * det->maf_win);
}

int gd_sample_count(gd_detector_t *det)
{
    return det ? det->buf_count : 0;
}

const gd_feature_t *gd_last_feature(gd_detector_t *det)
{
    if (det == RT_NULL || det->feature_valid == 0) return RT_NULL;
    return &det->last_feature;
}

const char *gd_state_name(galloping_state_t state)
{
    switch (state) {
        case GD_STATE_IDLE:     return "IDLE";
        case GD_STATE_BREEZE:   return "BREEZE";
        case GD_STATE_MODERATE: return "MODERATE";
        case GD_STATE_SEVERE:   return "SEVERE";
        case GD_STATE_ICE:      return "ICE_GALLOPING";
        case GD_STATE_UNKNOWN:  return "UNKNOWN";
        default:                return "?";
    }
}

void gd_feature_print(const gd_feature_t *f)
{
    if (f == RT_NULL) return;

    rt_kprintf("=== Galloping Feature ===\n");
    rt_kprintf("  Amp:     X=%.3f  Y=%.3f  Z=%.3f  Dom=%.3f (m/s²)\n",
               (double)f->amp_x_pp, (double)f->amp_y_pp,
               (double)f->amp_z_pp, (double)f->amp_dominant);
    rt_kprintf("  Displ:   %.4f m\n", (double)f->displacement_est);
    rt_kprintf("  Freq:    %.3f Hz (ZC_rate=%.1f)\n",
               (double)f->dominant_freq, (double)f->zero_cross_rate);
    rt_kprintf("  Energy:  RMS=%.3f  VibE=%.3f\n",
               (double)f->rms_accel, (double)f->vibr_energy);
    rt_kprintf("  Torsion: %.2f°\n", (double)f->torsion_deg);
    rt_kprintf("  Stat:    mean=%.3f  std=%.3f\n",
               (double)f->mean_mag, (double)f->std_mag);
    rt_kprintf("  State:   %s (confidence=%.2f)\n",
               gd_state_name(f->state), (double)f->confidence);
}

/* ================================================================
 * 核心数据流：gd_feed
 * ================================================================ */

const gd_feature_t *gd_feed(gd_detector_t *det,
                            float accel_x, float accel_y, float accel_z,
                            float roll, float pitch)
{
    gd_sample_t *s;
    float filt_x, filt_y, filt_z;

    if (det == RT_NULL) return RT_NULL;

    /* ---- 第一层：滑动均值滤波 (MAF) ---- */
    gd_maf_push(det, accel_x, accel_y, accel_z);

    filt_x = det->maf_sum_x / det->maf_win;
    filt_y = det->maf_sum_y / det->maf_win;
    filt_z = det->maf_sum_z / det->maf_win;

    /* ---- 第二层：去直流分量（重力投影分离） ---- */
    gd_update_dc(det, filt_x, filt_y, filt_z);

    /* ---- 第三层：写入窗口缓冲区 ---- */
    s = &det->buffer[det->buf_head];
    s->x       = accel_x;
    s->y       = accel_y;
    s->z       = accel_z;
    s->x_filt  = filt_x - det->dc_x;    /* 去直流后的动态加速度 */
    s->y_filt  = filt_y - det->dc_y;
    s->z_filt  = filt_z - det->dc_z;
    s->dc_x    = det->dc_x;
    s->dc_y    = det->dc_y;
    s->dc_z    = det->dc_z;
    s->magnitude = gd_calc_magnitude(s->x_filt, s->y_filt, s->z_filt);

    det->buf_head = (det->buf_head + 1) % det->win_size;
    if (det->buf_count < det->win_size) {
        det->buf_count++;
    }

    /* ---- 窗口满时自动分析 ---- */
    if (det->buf_count >= det->win_size) {
        return gd_analyze(det);
    }

    return RT_NULL;
}

/* ================================================================
 * 强制分析
 * ================================================================ */

const gd_feature_t *gd_analyze(gd_detector_t *det)
{
    if (det == RT_NULL || det->buf_count < det->win_size / 2) {
        /* 数据不足半个窗口，不做分析 */
        return RT_NULL;
    }

    gd_extract_features(det, &det->last_feature);
    det->last_feature.state      = gd_classify(&det->last_feature);
    det->feature_valid           = 1;

    return &det->last_feature;
}

/* ================================================================
 * 内部实现 —— 滤波层
 * ================================================================ */

/**
 * @brief 滑动均值滤波 (Moving Average Filter)
 *        维护环形缓冲区和运行和，O(1) 复杂度
 */
static void gd_maf_push(gd_detector_t *det, float x, float y, float z)
{
    /* 旧值出队 */
    det->maf_sum_x -= det->maf_x[det->maf_idx];
    det->maf_sum_y -= det->maf_y[det->maf_idx];
    det->maf_sum_z -= det->maf_z[det->maf_idx];

    /* 新值入队 */
    det->maf_x[det->maf_idx] = x;
    det->maf_y[det->maf_idx] = y;
    det->maf_z[det->maf_idx] = z;

    det->maf_sum_x += x;
    det->maf_sum_y += y;
    det->maf_sum_z += z;

    det->maf_idx = (det->maf_idx + 1) % det->maf_win;
}

/**
 * @brief 指数平滑估计直流分量（重力投影）
 *        dc_new = dc_old * (1 - alpha) + sample * alpha
 *        alpha 很小 → 跟踪慢变分量（重力），滤除快变分量（振动）
 */
static void gd_update_dc(gd_detector_t *det, float x, float y, float z)
{
    float a = det->dc_alpha;
    det->dc_x = det->dc_x * (1.0f - a) + x * a;
    det->dc_y = det->dc_y * (1.0f - a) + y * a;
    det->dc_z = det->dc_z * (1.0f - a) + z * a;
}

static float gd_calc_magnitude(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

/* ================================================================
 * 内部实现 —— 特征提取层
 * ================================================================ */

static void gd_extract_features(gd_detector_t *det, gd_feature_t *f)
{
    gd_sample_t *s;
    int i, count;
    float min_x, max_x, min_y, max_y, min_z, max_z;
    float sum_x, sum_y, sum_z, sum_mag, sum_sq_mag;
    float sum_sq_x, sum_sq_y, sum_sq_z;
    float prev_x, prev_y, prev_z;
    int   zc_x, zc_y, zc_z;
    float torsion_acc;

    if (det == RT_NULL || f == RT_NULL) return;
    memset(f, 0, sizeof(gd_feature_t));

    count = det->buf_count;
    if (count < 4) return;

    /* ---- 振幅分析：峰-峰值 ---- */
    s = &det->buffer[0];
    min_x = max_x = s->x_filt;
    min_y = max_y = s->y_filt;
    min_z = max_z = s->z_filt;
    sum_x = sum_y = sum_z = sum_mag = sum_sq_mag = 0.0f;
    sum_sq_x = sum_sq_y = sum_sq_z = 0.0f;

    prev_x = s->x_filt; prev_y = s->y_filt; prev_z = s->z_filt;
    zc_x = zc_y = zc_z = 0;

    for (i = 0; i < count; i++) {
        s = &det->buffer[i];
        float fx = s->x_filt, fy = s->y_filt, fz = s->z_filt;

        /* 峰-峰值追踪 */
        if (fx < min_x) min_x = fx;
        if (fx > max_x) max_x = fx;
        if (fy < min_y) min_y = fy;
        if (fy > max_y) max_y = fy;
        if (fz < min_z) min_z = fz;
        if (fz > max_z) max_z = fz;

        /* 累积统计 */
        sum_x += fx;  sum_y += fy;  sum_z += fz;
        sum_sq_x += fx * fx; sum_sq_y += fy * fy; sum_sq_z += fz * fz;
        sum_mag    += s->magnitude;
        sum_sq_mag += s->magnitude * s->magnitude;

        /* 过零检测 */
        if (i > 0) {
            if ((prev_x * fx) < 0.0f) zc_x++;
            if ((prev_y * fy) < 0.0f) zc_y++;
            if ((prev_z * fz) < 0.0f) zc_z++;
        }
        prev_x = fx; prev_y = fy; prev_z = fz;
    }

    /* 峰-峰值 */
    f->amp_x_pp = max_x - min_x;
    f->amp_y_pp = max_y - min_y;
    f->amp_z_pp = max_z - min_z;

    /* 主导轴振幅 */
    f->amp_dominant = f->amp_x_pp;
    if (f->amp_y_pp > f->amp_dominant) f->amp_dominant = f->amp_y_pp;
    if (f->amp_z_pp > f->amp_dominant) f->amp_dominant = f->amp_z_pp;

    /* ---- 位移估算：对动态加速度二次积分 ---- */
    /* 简化方法：假设正弦振动 x(t) = A*sin(2πft)，
     *          则 a(t) = -A*(2πf)²*sin(2πft)
     *          A ≈ a_peak / (2πf)² */
    float avg_zc = (float)(zc_x + zc_y + zc_z) / 3.0f;
    float duration_sec = (float)count / det->samp_rate;
    float zc_rate = avg_zc / duration_sec;              /* 过零率（次/秒） */
    float est_freq = zc_rate / 2.0f;                     /* 频率 = 过零率 / 2 */
    if (est_freq < GD_FREQ_MIN_HZ) est_freq = GD_FREQ_MIN_HZ;

    /* 位移幅值 = 加速度峰-峰值 / (2 * (2πf)²) */
    float omega_sq = 2.0f * (2.0f * 3.14159265359f * est_freq) *
                           (2.0f * 3.14159265359f * est_freq);
    if (omega_sq > 0.01f) {
        f->displacement_est = f->amp_dominant / omega_sq;
    } else {
        f->displacement_est = 0.0f;
    }

    /* ---- 频率特征 ---- */
    f->zero_cross_rate = zc_rate;
    f->dominant_freq   = est_freq;

    /* ---- 能量特征 ---- */
    float n = (float)count;
    f->rms_accel    = sqrtf(sum_sq_mag / n);
    f->vibr_energy  = (sum_sq_mag / n);                  /* 振动能量 ≈ 均方加速度 */

    /* 统计 */
    f->mean_mag = sum_mag / n;
    float var   = (sum_sq_mag / n) - (f->mean_mag * f->mean_mag);
    f->std_mag  = (var > 0.0f) ? sqrtf(var) : 0.0f;

    /* ---- 扭转特征：基于过零方向不一致 ---- */
    /* 电缆舞动常伴随扭转，表现为 X/Y 振动相位不同 */
    torsion_acc = fabsf((float)(zc_x - zc_y)) / (count * 0.1f);
    torsion_acc = (torsion_acc > 1.0f) ? 1.0f : torsion_acc;
    f->torsion_deg = torsion_acc * 45.0f;               /* 映射到 0~45° */

    /* ---- 置信度：基于数据充分性 ---- */
    f->confidence = (count >= det->win_size) ? 1.0f : (float)count / det->win_size;
}

/* ================================================================
 * 内部实现 —— 状态分类层
 * ================================================================ */

/**
 * @brief 基于特征向量判定电缆舞动状态
 *
 * 判定规则（经验阈值，可根据实际场景标定）：
 *
 *   IDLE:       振幅 < 0.05g 且 RMS < 0.05g
 *   BREEZE:     振幅 0.05~0.15g，f > 1.5Hz（高频小振幅 = 微风振动）
 *   MODERATE:   振幅 0.15~0.5g，f 0.3~1.5Hz
 *   SEVERE:     振幅 > 0.5g，f < 0.5Hz（低频大振幅 = 舞动）
 *   ICE:        振幅 > 0.3g + f < 0.3Hz + 扭转 > 15°（覆冰特征）
 */
static galloping_state_t gd_classify(const gd_feature_t *f)
{
    float amp_g  = f->amp_dominant / GD_GRAVITY;    /* 归一化为 g */
    float rms_g  = f->rms_accel    / GD_GRAVITY;
    float freq   = f->dominant_freq;
    float torsion = f->torsion_deg;

    /* 传感器异常检查 */
    if (f->confidence < 0.5f) {
        return GD_STATE_UNKNOWN;
    }

    /* 静止 */
    if (amp_g < 0.03f && rms_g < 0.03f) {
        return GD_STATE_IDLE;
    }

    /* 覆冰舞动：低频 + 大振幅 + 明显扭转 */
    if (freq < 0.3f && amp_g > 0.2f && torsion > 15.0f) {
        return GD_STATE_ICE;
    }

    /* 剧烈舞动 */
    if (freq < 0.5f && amp_g > 0.4f) {
        return GD_STATE_SEVERE;
    }

    /* 中等舞动 */
    if (freq < 1.5f && amp_g > 0.15f) {
        return GD_STATE_MODERATE;
    }

    /* 微风振动：高频小振幅 */
    if (freq > 1.5f && amp_g > 0.05f) {
        return GD_STATE_BREEZE;
    }

    /* 微弱振动也归入微风 */
    if (amp_g > 0.03f && rms_g > 0.03f) {
        return GD_STATE_BREEZE;
    }

    return GD_STATE_IDLE;
}
