/* ===================================================================
 * main.c — RK3506 Voice Robot 主程序
 *
 * 负责:
 *   1. 加载配置文件
 *   2. 初始化所有外设模块 (音频/WiFi/HTTP/显示/按键LED)
 *   3. 组装 StateContext 并初始化状态机
 *   4. 主事件循环 (50ms 调度 state_machine_run)
 *   5. 优雅退出 (逆序释放所有模块资源)
 *
 * 目标平台: Linux ARM aarch64 (RK3506)
 * =================================================================== */

#include "common_types.h"
#include "app_config.h"
#include "error_codes.h"
#include "audio_capture.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "display.h"
#include "button_led.h"
#include "state_machine.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===================================================================
 * 全局变量
 * =================================================================== */

/* 日志级别 — 供 LOG_XXX 宏使用 (定义在 common_types.h) */
int g_log_level = 2;

/* 应用配置 — 栈分配，整个进程生命周期有效 */
AppConfig g_app_config;

/* 优雅退出标志 — 由 SIGINT/SIGTERM 信号处理函数设置 */
static volatile int g_quit = 0;

/* ===================================================================
 * 信号处理
 * =================================================================== */

/**
 * signal_handler — 捕获 SIGINT/SIGTERM 设置退出标志
 *
 * 不在此处执行清理，由主循环检测 g_quit 后统一退出。
 */
static void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        g_quit = 1;
    }
}

/* ===================================================================
 * 打印配置摘要
 * =================================================================== */

/**
 * print_config_summary — 打印当前加载的配置信息
 *
 * 用于启动时确认配置已正确加载，方便调试。
 */
static void print_config_summary(const AppConfig* cfg)
{
    LOG_INFO("========== Configuration Summary ==========");
    LOG_INFO("  Audio device:      %s", cfg->audio_device);
    LOG_INFO("  Sample rate:       %d Hz", cfg->sample_rate);
    LOG_INFO("  Channels:          %d", cfg->channels);
    LOG_INFO("  Record duration:   %d sec", cfg->record_duration_sec);
    LOG_INFO("  WiFi interface:    %s", cfg->wifi_interface);
    LOG_INFO("  WiFi SSID:         %s", cfg->wifi_ssid);
    LOG_INFO("  Server URL:        %s", cfg->server_url);
    LOG_INFO("  HTTP timeout:      %d sec", cfg->http_timeout_sec);
    LOG_INFO("  Framebuffer:       %s", cfg->fb_device);
    LOG_INFO("  Font:              %s (%dpt)",
             cfg->font_path, cfg->font_size_pt);
    LOG_INFO("  LCD resolution:    %dx%d",
             cfg->lcd_width, cfg->lcd_height);
    LOG_INFO("  Button GPIO:       %d", cfg->button_gpio);
    LOG_INFO("  LED GPIO:          %d", cfg->led_gpio);
    LOG_INFO("  Log level:         %d", cfg->log_level);
    LOG_INFO("============================================");
}

/* ===================================================================
 * main — 程序入口
 * =================================================================== */

int main(int argc, char* argv[])
{
    int ret;

    (void)argc;
    (void)argv;

    /* ---- 1. 启动标志 ---- */
    fprintf(stdout, "Voice Robot Starting...\n");

    /* ---- 2. 加载配置 ---- */
    memset(&g_app_config, 0, sizeof(g_app_config));
    ret = app_config_load(&g_app_config, "config/app_config.ini");
    if (ret != ERR_OK) {
        fprintf(stderr, "[ERROR] Failed to load config: %s\n",
                app_config_error());
        LOG_WARN("Proceeding with defaults.");
    }

    /* ---- 3. 应用日志级别 ---- */
    g_log_level = g_app_config.log_level;

    /* ---- 4. 打印配置摘要 ---- */
    print_config_summary(&g_app_config);

    /* ---- 5. 注册信号处理 ---- */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ---- 6. 模块初始化 (栈分配，部分失败不阻止整体启动) ---- */

    /* 音频采集 */
    AudioCapture ac;
    memset(&ac, 0, sizeof(ac));
    ret = audio_capture_init(&ac, g_app_config.audio_device,
                             g_app_config.sample_rate,
                             g_app_config.channels);
    if (ret != ERR_OK) {
        LOG_ERROR("audio_capture_init failed (rc=%d), audio disabled", ret);
    }

    /* WiFi 管理 */
    WiFiManager wm;
    memset(&wm, 0, sizeof(wm));
    ret = wifi_manager_init(&wm, g_app_config.wifi_interface);
    if (ret != ERR_OK) {
        LOG_ERROR("wifi_manager_init failed (rc=%d), WiFi disabled", ret);
    }

    /* HTTP 客户端 */
    HttpClient hc;
    memset(&hc, 0, sizeof(hc));
    ret = http_client_init(&hc, g_app_config.server_url,
                           g_app_config.http_timeout_sec);
    if (ret != ERR_OK) {
        LOG_ERROR("http_client_init failed (rc=%d), HTTP disabled", ret);
    }

    /* 显示 */
    Display d;
    memset(&d, 0, sizeof(d));
    ret = display_init(&d, g_app_config.fb_device,
                       g_app_config.font_path,
                       g_app_config.font_size_pt);
    if (ret != ERR_OK) {
        LOG_ERROR("display_init failed (rc=%d), display disabled", ret);
    }

    /* 按键 + LED */
    ButtonLED bl;
    memset(&bl, 0, sizeof(bl));
    ret = button_led_init(&bl, g_app_config.button_gpio,
                          g_app_config.led_gpio);
    if (ret != ERR_OK) {
        LOG_ERROR("button_led_init failed (rc=%d), GPIO disabled", ret);
    }

    /* ---- 7. 组装 StateContext ---- */
    StateContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.audio      = &ac;
    ctx.wifi       = &wm;
    ctx.http       = &hc;
    ctx.display    = &d;
    ctx.button_led = &bl;

    /* 录音缓冲区 — 初始为空，由 state_machine_init 内部分配 */
    ctx.recording.data     = NULL;
    ctx.recording.size     = 0;
    ctx.recording.capacity = 0;
    ctx.recording.is_valid = 0;

    /* HTTP 响应 — 初始为空 */
    ctx.response.code       = 0;
    ctx.response.reply_text = NULL;
    ctx.response.reply_len  = 0;
    ctx.response.is_success = 0;

    /* 错误信息 — 初始为空 */
    ctx.error_code = 0;
    ctx.error_msg[0] = '\0';

    /* ---- 8. 初始化状态机 ---- */
    StateMachine sm;
    memset(&sm, 0, sizeof(sm));

    ret = state_machine_init(&sm, &ctx);
    if (ret != ERR_OK) {
        LOG_ERROR("state_machine_init failed (rc=%d), exiting", ret);
        /* 状态机初始化失败，直接进入清理流程 */
        goto cleanup;
    }

    /* ---- 9. 主事件循环 (50ms 调度周期) ---- */
    LOG_INFO("Entering main event loop...");

    while (!g_quit) {
        /* 每帧调用状态机 run：按键轮询/录音采集/WiFi检查/超时检测 */
        state_machine_run(&sm);

        /* 50ms 休眠 — 平衡响应速度 (按键去抖) 与 CPU 占用 */
        {
            struct timespec ts;
            ts.tv_sec  = 0;
            ts.tv_nsec = 50000000L; /* 50ms */
            nanosleep(&ts, NULL);
        }
    }

    LOG_INFO("Exit signal received, shutting down...");

cleanup:
    /* ---- 10. 优雅退出 (按规范顺序释放) ---- */

    /* 10.1 状态机 — 最先释放 (停止一切状态流转) */
    LOG_INFO("Shutting down state machine...");
    state_machine_deinit(&sm);

    /* 10.2 显示模块 — 清屏后释放 framebuffer */
    LOG_INFO("Shutting down display...");
    display_deinit(&d);

    /* 10.3 HTTP 客户端 — 销毁 CURL 句柄 */
    LOG_INFO("Shutting down HTTP client...");
    http_client_deinit(&hc);

    /* 10.4 WiFi 管理 — 释放网络接口资源 */
    LOG_INFO("Shutting down WiFi manager...");
    wifi_manager_deinit(&wm);

    /* 10.5 音频采集 — 关闭 PCM 设备 */
    LOG_INFO("Shutting down audio capture...");
    audio_capture_deinit(&ac);

    /* 10.6 按键/LED — 取消 GPIO 导出 */
    LOG_INFO("Shutting down button/LED...");
    button_led_deinit(&bl);

    /* ---- 11. 退出 ---- */
    LOG_INFO("Voice Robot Stopped.");
    return 0;
}
