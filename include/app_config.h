#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#include "error_codes.h"

/* ===================================================================
 * AppConfig — 应用配置结构体（全部使用静态数组，禁止指针/动态分配）
 *
 * 从 config/app_config.ini 解析，支持运行时 reload。
 * 使用 libconfig C 库。
 * =================================================================== */

typedef struct {
    /* 音频 */
    char audio_device[64];
    int  sample_rate;
    int  channels;
    int  record_duration_sec;

    /* WiFi */
    char wifi_interface[16];
    char wifi_ssid[64];
    char wifi_psk[64];

    /* HTTP */
    char server_url[256];
    int  http_timeout_sec;

    /* 显示 */
    char font_path[256];
    int  font_size_pt;
    char fb_device[64];
    int  lcd_width;
    int  lcd_height;

    /* GPIO */
    int button_gpio;
    int led_gpio;

    /* 系统 */
    int log_level;
} AppConfig;

/* ===================================================================
 * API
 * =================================================================== */

/*
 * 从 ini 文件加载配置到 cfg 结构体。
 * 返回 ERR_OK (0) 成功，ERR_CONFIG_LOAD 文件读取失败，
 *   ERR_CONFIG_PARSE 字段解析/校验失败。
 */
int app_config_load(AppConfig* cfg, const char* path);

/*
 * 重新加载配置 — 保留旧值为 fallback，加载失败时不覆盖。
 * 需要在 app_config_load 至少成功一次后调用。
 * 返回 0 成功，负数错误码。
 */
int app_config_reload(AppConfig* cfg);

/*
 * 获取最后一次错误描述的字符串。
 * 仅对当前线程最近一次 load/reload 调用有效。
 */
const char* app_config_error(void);

#endif /* _APP_CONFIG_H_ */
