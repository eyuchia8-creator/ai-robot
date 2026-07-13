#include "app_config.h"
#include "common_types.h"

#include <libconfig.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* ===================================================================
 * 模块内部状态 — 单例错误信息
 * =================================================================== */
static char g_last_error[256] = {0};

/* 设置最后一次错误描述 */
static void set_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

/* ===================================================================
 * 内部辅助: 从 config_setting_t 安全查找并读取字段
 * =================================================================== */

/* 查整数 */
static int lookup_int(const config_setting_t* parent, const char* key,
                      int* out)
{
    config_setting_t* s = config_setting_get_member(parent, key);
    if (!s) {
        set_error("missing config key: %s", key);
        return ERR_CONFIG_PARSE;
    }
    *out = config_setting_get_int(s);
    return ERR_OK;
}

/* 查字符串 */
static int lookup_string(const config_setting_t* parent, const char* key,
                         char* out, size_t out_len)
{
    config_setting_t* s = config_setting_get_member(parent, key);
    if (!s) {
        set_error("missing config key: %s", key);
        return ERR_CONFIG_PARSE;
    }
    const char* val = config_setting_get_string(s);
    if (!val) {
        set_error("config key '%s' is not a string", key);
        return ERR_CONFIG_PARSE;
    }
    strncpy(out, val, out_len - 1);
    out[out_len - 1] = '\0';
    return ERR_OK;
}

/* ===================================================================
 * 字段合理性校验
 * =================================================================== */

static int validate_config(const AppConfig* cfg)
{
    if (cfg->sample_rate <= 0) {
        set_error("invalid sample_rate: %d", cfg->sample_rate);
        return ERR_CONFIG_PARSE;
    }
    if (cfg->channels != 1 && cfg->channels != 2) {
        set_error("invalid channels: %d (expected 1 or 2)", cfg->channels);
        return ERR_CONFIG_PARSE;
    }
    if (cfg->record_duration_sec <= 0) {
        set_error("invalid record_duration_sec: %d", cfg->record_duration_sec);
        return ERR_CONFIG_PARSE;
    }
    if (cfg->http_timeout_sec <= 0) {
        set_error("invalid http_timeout_sec: %d", cfg->http_timeout_sec);
        return ERR_CONFIG_PARSE;
    }
    if (cfg->lcd_width <= 0 || cfg->lcd_height <= 0) {
        set_error("invalid lcd dimensions: %dx%d",
                  cfg->lcd_width, cfg->lcd_height);
        return ERR_CONFIG_PARSE;
    }
    if (cfg->log_level < 0) {
        set_error("invalid log_level: %d", cfg->log_level);
        return ERR_CONFIG_PARSE;
    }
    return ERR_OK;
}

/* ===================================================================
 * app_config_load — 从 ini 文件加载配置
 * =================================================================== */

int app_config_load(AppConfig* cfg, const char* path)
{
    config_t config;
    config_init(&config);

    /* 读取配置文件 */
    if (config_read_file(&config, path) == CONFIG_FALSE) {
        set_error("failed to read config file: %s (line %d: %s)",
                  path,
                  config_error_line(&config),
                  config_error_text(&config));
        config_destroy(&config);
        return ERR_CONFIG_LOAD;
    }

    /* --- [audio] --- */
    config_setting_t* audio = config_lookup(&config, "audio");
    if (!audio) {
        set_error("missing [audio] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_string(audio, "device", cfg->audio_device,
                       sizeof(cfg->audio_device)) != ERR_OK) goto parse_fail;
    if (lookup_int(audio, "sample_rate", &cfg->sample_rate) != ERR_OK)   goto parse_fail;
    if (lookup_int(audio, "channels", &cfg->channels) != ERR_OK)         goto parse_fail;
    if (lookup_int(audio, "record_duration_sec", &cfg->record_duration_sec) != ERR_OK) goto parse_fail;

    /* --- [wifi] --- */
    config_setting_t* wifi = config_lookup(&config, "wifi");
    if (!wifi) {
        set_error("missing [wifi] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_string(wifi, "interface", cfg->wifi_interface,
                       sizeof(cfg->wifi_interface)) != ERR_OK) goto parse_fail;
    if (lookup_string(wifi, "ssid", cfg->wifi_ssid,
                       sizeof(cfg->wifi_ssid)) != ERR_OK) goto parse_fail;
    if (lookup_string(wifi, "psk", cfg->wifi_psk,
                       sizeof(cfg->wifi_psk)) != ERR_OK) goto parse_fail;

    /* --- [http] --- */
    config_setting_t* http = config_lookup(&config, "http");
    if (!http) {
        set_error("missing [http] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_string(http, "server_url", cfg->server_url,
                       sizeof(cfg->server_url)) != ERR_OK) goto parse_fail;
    if (lookup_int(http, "timeout_sec", &cfg->http_timeout_sec) != ERR_OK) goto parse_fail;

    /* --- [display] --- */
    config_setting_t* display = config_lookup(&config, "display");
    if (!display) {
        set_error("missing [display] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_string(display, "fb_device", cfg->fb_device,
                       sizeof(cfg->fb_device)) != ERR_OK) goto parse_fail;
    if (lookup_string(display, "font_path", cfg->font_path,
                       sizeof(cfg->font_path)) != ERR_OK) goto parse_fail;
    if (lookup_int(display, "font_size_pt", &cfg->font_size_pt) != ERR_OK) goto parse_fail;
    if (lookup_int(display, "lcd_width", &cfg->lcd_width) != ERR_OK)     goto parse_fail;
    if (lookup_int(display, "lcd_height", &cfg->lcd_height) != ERR_OK)   goto parse_fail;

    /* --- [gpio] --- */
    config_setting_t* gpio = config_lookup(&config, "gpio");
    if (!gpio) {
        set_error("missing [gpio] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_int(gpio, "button_gpio", &cfg->button_gpio) != ERR_OK) goto parse_fail;
    if (lookup_int(gpio, "led_gpio", &cfg->led_gpio) != ERR_OK)       goto parse_fail;

    /* --- [system] --- */
    config_setting_t* sys = config_lookup(&config, "system");
    if (!sys) {
        set_error("missing [system] section");
        config_destroy(&config);
        return ERR_CONFIG_PARSE;
    }
    if (lookup_int(sys, "log_level", &cfg->log_level) != ERR_OK) goto parse_fail;

    config_destroy(&config);

    /* 字段校验 */
    if (validate_config(cfg) != ERR_OK) {
        return ERR_CONFIG_PARSE;
    }

    return ERR_OK;

parse_fail:
    config_destroy(&config);
    return ERR_CONFIG_PARSE;
}

/* ===================================================================
 * app_config_reload — 重新加载，保留旧值作为 fallback
 * =================================================================== */

int app_config_reload(AppConfig* cfg)
{
    /* 先在临时结构体中加载 */
    AppConfig new_cfg;
    int ret = app_config_load(&new_cfg, "config/app_config.ini");
    if (ret != ERR_OK) {
        /* 加载失败 — 保留旧值不覆盖，记录错误 */
        LOG_WARN("config reload failed, keeping old values: %s",
                 app_config_error());
        return ret;
    }

    /* 全部字段通过校验后才整体覆盖 */
    memcpy(cfg, &new_cfg, sizeof(AppConfig));
    LOG_INFO("config reloaded successfully");
    return ERR_OK;
}

/* ===================================================================
 * app_config_error — 获取最后一次错误描述
 * =================================================================== */

const char* app_config_error(void)
{
    return g_last_error;
}
