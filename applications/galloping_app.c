/*
 * galloping_app.c - 电缆舞动检测应用层
 *
 * 启动一个 50ms 定时采集线程：
 *   读取 BNO055 → MAF滤波 → 去直流 → 窗口累积 →
 *   窗口满时自动提取特征 + 判定状态 → 打印结果 + DTU发送
 *
 * 同时启动 DTU 心跳和温湿度定时发送。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "gy_bn0055.h"
#include "galloping_detect.h"
#include "dtu_sender.h"
#include "sht3x.h"

/* ---- 线程配置 ---- */
#define GD_THREAD_STACK_SIZE    2048
#define GD_THREAD_PRIORITY      12
#define GD_THREAD_PERIOD_MS     50          /* 20 Hz 采样 */

/* ---- 全局句柄 ---- */
static gd_detector_t *g_detector = RT_NULL;

/* 温湿度传感器数据 */
extern sht3x_data_t sht3x_data;   /* 定义在 sensor_app.c */
extern int g_sensor_ready;        /* 定义在 sensor_app.c */

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

            /* 通过 DTU 发送舞动特征数据 */
            if (dtu_is_ready()) {
                dtu_send_galloping(feat, feat->state);
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

    /* 初始化 DTU 发送模块 */
    if (dtu_sender_init() != RT_EOK) {
        rt_kprintf("[Galloping] WARNING: DTU init failed, data will not be sent\n");
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

/* ================================================================
 * DTU 定时上报线程（温湿度 + 心跳）
 * ================================================================ */

#define DTU_REPORT_STACK_SIZE   1536
#define DTU_REPORT_PRIORITY     15

/**
 * @brief DTU 定时上报线程入口
 *
 * 每 5 秒发送一次温湿度数据，
 * 每 10 秒发送一次心跳包。
 */
static void dtu_report_thread_entry(void *parameter)
{
    rt_tick_t last_env_tick  = 0;
    rt_tick_t last_hb_tick   = 0;
    rt_tick_t now;
    int ret;

    rt_kprintf("[DTU-Report] Thread started\n");

    /* 立即发送一次心跳（不等传感器） */
    if (dtu_is_ready()) {
        dtu_send_heartbeat();
        rt_kprintf("[DTU-Report] Initial heartbeat sent\n");
    }

    while (1)
    {
        now = rt_tick_get();

        /* 每 5 秒发送温湿度 */
        if (now - last_env_tick >= rt_tick_from_millisecond(DTU_ENV_INTERVAL_MS)) {
            if (dtu_is_ready() && g_sensor_ready) {
                ret = sht3x_read(&sht3x_data);
                if (ret == RT_EOK) {
                    dtu_send_env(sht3x_data.temperature, sht3x_data.humidity);
                }
            }
            last_env_tick = now;
        }

        /* 每 10 秒发送心跳 */
        if (now - last_hb_tick >= rt_tick_from_millisecond(DTU_HEARTBEAT_INTERVAL_MS)) {
            if (dtu_is_ready()) {
                dtu_send_heartbeat();
            }
            last_hb_tick = now;
        }

        rt_thread_mdelay(1000);  /* 1 秒检查一次 */
    }
}

/**
 * @brief 启动 DTU 定时上报
 *        msh: dtu_report_start
 */
static void dtu_report_start(void)
{
    rt_thread_t thread;

    if (!dtu_is_ready()) {
        rt_kprintf("[DTU-Report] DTU not initialized! Run galloping_start first.\n");
        return;
    }

    thread = rt_thread_create("dtu_rpt",
                              dtu_report_thread_entry,
                              RT_NULL,
                              DTU_REPORT_STACK_SIZE,
                              DTU_REPORT_PRIORITY,
                              10);
    if (thread == RT_NULL) {
        rt_kprintf("[DTU-Report] Failed to create thread!\n");
        return;
    }

    rt_thread_startup(thread);
    rt_kprintf("[DTU-Report] Started (env every %dms, heartbeat every %dms)\n",
               DTU_ENV_INTERVAL_MS, DTU_HEARTBEAT_INTERVAL_MS);
}
MSH_CMD_EXPORT(dtu_report_start, start DTU periodic reporting);

/* ================================================================
 * 上电自动启动（INIT_APP_EXPORT 会在 main() 及 sensor_auto_init 之后调用）
 * ================================================================ */

/**
 * @brief 上电自动启动舞动检测 + DTU 数据上报
 *
 * 执行顺序：
 *   1. 等待传感器就绪（最多等 5 秒）
 *   2. 初始化 DTU 发送模块（UART2）
 *   3. 创建并启动舞动检测线程
 *   4. 创建并启动 DTU 定时上报线程（温湿度 + 心跳）
 *
 * 使用 INIT_APP_EXPORT 自动注册，无需手动输入命令。
 * 仍可使用 galloping_stop / galloping_start 手动控制。
 *
 * @return 0
 */
static int galloping_auto_start(void)
{
    int wait_ms = 0;

    rt_kprintf("[AutoInit] Galloping auto-start waiting for sensor...\n");

    /* 等待传感器初始化完成（最多 5 秒） */
    while (!g_sensor_ready && wait_ms < 5000) {
        rt_thread_mdelay(100);
        wait_ms += 100;
    }

    if (!g_sensor_ready) {
        rt_kprintf("[AutoInit] WARNING: Sensor not ready after 5s, starting anyway\n");
    } else {
        rt_kprintf("[AutoInit] Sensor ready, starting galloping detection\n");
    }

    /* 初始化 DTU 发送模块 */
    if (dtu_sender_init() != RT_EOK) {
        rt_kprintf("[AutoInit] WARNING: DTU init failed, data will not be sent\n");
    }

    /* 创建检测器 */
    g_detector = gd_create("cable1", GD_SAMPLE_RATE_HZ, GD_WINDOW_SIZE);
    if (g_detector == RT_NULL) {
        rt_kprintf("[AutoInit] Failed to create detector!\n");
        return -1;
    }

    /* 启动舞动采集线程 */
    rt_thread_t galloping_thread = rt_thread_create(
        "galloping",
        galloping_thread_entry,
        RT_NULL,
        GD_THREAD_STACK_SIZE,
        GD_THREAD_PRIORITY,
        10);

    if (galloping_thread == RT_NULL) {
        rt_kprintf("[AutoInit] Failed to create galloping thread!\n");
        gd_destroy(g_detector);
        g_detector = RT_NULL;
        return -1;
    }
    rt_thread_startup(galloping_thread);

    /* 启动 DTU 定时上报线程 */
    rt_thread_t report_thread = rt_thread_create(
        "dtu_rpt",
        dtu_report_thread_entry,
        RT_NULL,
        DTU_REPORT_STACK_SIZE,
        DTU_REPORT_PRIORITY,
        10);

    if (report_thread == RT_NULL) {
        rt_kprintf("[AutoInit] Failed to create DTU report thread!\n");
    } else {
        rt_thread_startup(report_thread);
    }

    rt_kprintf("[AutoInit] Galloping detection + DTU reporting started\n");
    return 0;
}
/* 暂时注释掉自动启动，避免 BNO055 未初始化时疯狂报错 */
/* INIT_APP_EXPORT(galloping_auto_start); */
