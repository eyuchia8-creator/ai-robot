#ifndef _ERROR_CODES_H_
#define _ERROR_CODES_H_

/* ===================================================================
 * 全局错误码 — 所有 API 返回 int，0 表示成功，负数表示错误
 *
 * 编码规则:
 *   -1xxx  音频模块
 *   -2xxx  WiFi 模块
 *   -3xxx  HTTP 模块
 *   -4xxx  显示模块
 *   -5xxx  配置模块
 *   -6xxx  按键/LED 模块
 *   -8xxx  状态机
 *   -9xxx  通用
 * =================================================================== */

typedef enum {
    ERR_OK = 0,

    /* --- 音频模块 -1xxx --- */
    ERR_AUDIO_OPEN   = -1001,
    ERR_AUDIO_PARAMS = -1002,
    ERR_AUDIO_READ   = -1003,

    /* --- WiFi 模块 -2xxx --- */
    ERR_WIFI_DISCONNECTED = -2001,
    ERR_WIFI_TIMEOUT      = -2002,

    /* --- HTTP 模块 -3xxx --- */
    ERR_HTTP_CONNECT      = -3001,
    ERR_HTTP_TIMEOUT      = -3002,
    ERR_HTTP_SERVER_ERROR = -3003,
    ERR_HTTP_PARSE        = -3004,

    /* --- 显示模块 -4xxx --- */
    ERR_DISPLAY_OPEN  = -4001,
    ERR_DISPLAY_MMAP  = -4002,
    ERR_FONT_LOAD     = -4003,
    ERR_FONT_RENDER   = -4004,

    /* --- 配置模块 -5xxx --- */
    ERR_CONFIG_LOAD  = -5001,
    ERR_CONFIG_PARSE = -5002,

    /* --- 按键/LED 模块 -6xxx --- */
    ERR_BUTTON_INIT = -6001,

    /* --- 通用 -9xxx --- */
    ERR_MEMORY_ALLOC = -9001,
    ERR_INTERNAL     = -9999,
} ErrorCode;

#endif /* _ERROR_CODES_H_ */
