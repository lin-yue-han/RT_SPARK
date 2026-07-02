/*
 * galloping_app.c - 电缆舞动检测应用层
 *
 * 启动一个 50ms 定时采集线程：
 *   读取 BNO055 → MAF滤波 → 去直流 → 窗口累积 →
 *   窗口满时自动提取特征 + 判定状态 → 打印结果
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "gy_bn0055.h"
#include "galloping_detect.h"

/* ---- 线程配置 ---- */
#define GD_THREAD_STACK_SIZE    2048
#define GD_THREAD_PRIORITY      12
#define GD_THREAD_PERIOD_MS     50          /* 20 Hz 采样 */

/* ---- 全局句柄 ---- */
static gd_detector_t *g_detector = RT_NULL;

/**
 * @brief 状态变化回调：当检测到状态切换时打印告警
 */
static void on_state_changed(galloping_state_t old_st, galloping_state_t new_st,
                             const gd_feature_t *f)
{
    rt_kprintf("\n==========================================\n");
    rt_kprintf("  GALLOPING STATE CHANGED!\n");
    rt_kprintf("  %s  -->  %s\n",
               gd_state_name(old_st), gd_state_name(new_st));
    rt_kprintf("==========================================\n");

    switch (new_st) {
        case GD_STATE_IDLE:
            rt_kprintf("  [OK] 电缆处于静止状态\n");
            break;
        case GD_STATE_BREEZE:
            rt_kprintf("  [INFO] 检测到微风振动，振幅 %.3f g\n",
                       (double)(f->amp_dominant / 9.81f));
            break;
        case GD_STATE_MODERATE:
            rt_kprintf("  [WARN] 中等舞动！振幅 %.3f g，位移 ~%.3f m\n",
                       (double)(f->amp_dominant / 9.81f),
                       (double)f->displacement_est);
            break;
        case GD_STATE_SEVERE:
            rt_kprintf("  [ALERT] 剧烈舞动！！！振幅 %.3f g，位移 ~%.3f m\n",
                       (double)(f->amp_dominant / 9.81f),
                       (double)f->displacement_est);
            rt_kprintf("  [ALERT] 建议立即检查线路！\n");
            break;
        case GD_STATE_ICE:
            rt_kprintf("  [DANGER] 覆冰舞动！！！\n");
            rt_kprintf("  [DANGER] 频率 %.3f Hz，扭转 %.1f°，可能已覆冰\n",
                       (double)f->dominant_freq, (double)f->torsion_deg);
            rt_kprintf("  [DANGER] 强烈建议启动除冰或断线保护！\n");
            break;
        default:
            break;
    }
    rt_kprintf("\n");
}

/**
 * @brief 采集线程入口
 *
 * 每 50ms 读取 BNO055，喂入检测器。
 * 窗口满（64 点 ≈ 3.2s）时自动输出特征。
 */
static void galloping_thread_entry(void *parameter)
{
    bn0055_data_t bno;
    const gd_feature_t *feat;
    int              ret;
    galloping_state_t prev_state = GD_STATE_IDLE;
    rt_tick_t        last_print_tick = 0;
    rt_tick_t        now;

    rt_kprintf("[Galloping] Thread started, period=%dms\n", GD_THREAD_PERIOD_MS);

    /* 等待传感器稳定 */
    rt_thread_mdelay(500);

    while (1)
    {
        ret = bn0055_read(&bno);
        if (ret != RT_EOK) {
            rt_kprintf("[Galloping] BNO055 read error!\n");
            rt_thread_mdelay(GD_THREAD_PERIOD_MS);
            continue;
        }

        /* 喂入检测器：加速度 (m/s²) + 欧拉角 (°) */
        feat = gd_feed(g_detector,
                       bno.accel_x, bno.accel_y, bno.accel_z,
                       bno.euler_roll, bno.euler_pitch);

        if (feat != RT_NULL) {
            /* 窗口分析完成，输出特征 */
            gd_feature_print(feat);

            /* 状态变化告警 */
            if (feat->state != prev_state) {
                on_state_changed(prev_state, feat->state, feat);
                prev_state = feat->state;
            }
        } else {
            /* 每秒打印一次采集心跳（防止控制台静默） */
            now = rt_tick_get();
            if (now - last_print_tick > rt_tick_from_millisecond(1000)) {
                rt_kprintf("[Galloping] collecting... (%d/%d samples)\n",
                           gd_sample_count(g_detector), GD_WINDOW_SIZE);
                last_print_tick = now;
            }
        }

        rt_thread_mdelay(GD_THREAD_PERIOD_MS);
    }
}

/* ================================================================
 * MSH 命令
 * ================================================================ */

/**
 * @brief 启动舞动检测
 *        msh: galloping_start
 */
static void galloping_start(void)
{
    rt_thread_t thread;

    if (g_detector != RT_NULL) {
        rt_kprintf("[Galloping] Already running!\n");
        return;
    }

    /* 创建检测器：20Hz 采样，64 点窗口 */
    g_detector = gd_create("cable1", GD_SAMPLE_RATE_HZ, GD_WINDOW_SIZE);
    if (g_detector == RT_NULL) {
        rt_kprintf("[Galloping] Failed to create detector!\n");
        return;
    }

    thread = rt_thread_create("galloping",
                              galloping_thread_entry,
                              RT_NULL,
                              GD_THREAD_STACK_SIZE,
                              GD_THREAD_PRIORITY,
                              10);
    if (thread == RT_NULL) {
        rt_kprintf("[Galloping] Failed to create thread!\n");
        gd_destroy(g_detector);
        g_detector = RT_NULL;
        return;
    }

    rt_thread_startup(thread);
    rt_kprintf("[Galloping] Detection started\n");
}
MSH_CMD_EXPORT(galloping_start, start galloping detection);

/**
 * @brief 停止舞动检测
 *        msh: galloping_stop
 */
static void galloping_stop(void)
{
    /* 简单方案：销毁检测器，线程会在下次 bn0055_read 时因 detector 无效而自行退出 */
    if (g_detector != RT_NULL) {
        gd_destroy(g_detector);
        g_detector = RT_NULL;
        rt_kprintf("[Galloping] Detection stopped\n");
    } else {
        rt_kprintf("[Galloping] Not running\n");
    }
}
MSH_CMD_EXPORT(galloping_stop, stop galloping detection);

/**
 * @brief 单次手动分析
 *        msh: galloping_stat
 */
static void galloping_stat(void)
{
    const gd_feature_t *f;

    if (g_detector == RT_NULL) {
        rt_kprintf("[Galloping] Not running! Use galloping_start first.\n");
        return;
    }

    f = gd_last_feature(g_detector);
    if (f == RT_NULL) {
        rt_kprintf("[Galloping] No data yet (%d/%d samples)\n",
                   gd_sample_count(g_detector), GD_WINDOW_SIZE);
        return;
    }

    gd_feature_print(f);
}
MSH_CMD_EXPORT(galloping_stat, show last galloping stats);

/**
 * @brief 重置检测器（传感器位置变化后调用）
 *        msh: galloping_reset
 */
static void galloping_reset(void)
{
    if (g_detector != RT_NULL) {
        gd_reset(g_detector);
        rt_kprintf("[Galloping] Detector reset\n");
    } else {
        rt_kprintf("[Galloping] Not running\n");
    }
}
MSH_CMD_EXPORT(galloping_reset, reset galloping detector);
