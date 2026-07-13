#ifndef _COMMON_TYPES_H_
#define _COMMON_TYPES_H_

#include <stddef.h>

/* ===================================================================
 * 公共类型定义 — 所有模块共享的基础数据结构与枚举
 * =================================================================== */

/* 状态枚举 */
typedef enum {
    STATE_IDLE       = 0,
    STATE_RECORDING  = 1,
    STATE_PROCESSING = 2,
    STATE_RESULT     = 3,
    STATE_ERROR      = 4,
} StateEnum;

/* 事件枚举 */
typedef enum {
    EVENT_BUTTON_PRESS   = 0,
    EVENT_BUTTON_RELEASE = 1,
    EVENT_RECORD_DONE    = 2,
    EVENT_HTTP_SUCCESS   = 3,
    EVENT_HTTP_FAIL      = 4,
    EVENT_DISPLAY_DONE   = 5,
    EVENT_TIMEOUT        = 6,
} EventEnum;

/* LED 状态 */
typedef enum {
    LED_OFF       = 0,
    LED_ON        = 1,
    LED_BLINK_SLOW = 2,  /* 录音中 */
    LED_BLINK_FAST = 3,  /* 处理中 */
} LedState;

/* 回调类型 — 模块间解耦用函数指针 */
typedef void (*ResponseCallback)(int http_code, const char* reply,
                                  size_t len, void* user_data);
typedef void (*ButtonCallback)(int pressed, void* user_data);

/* 音频缓冲区 — 静态分配，禁止动态扩容 */
typedef struct {
    char*  data;
    size_t size;
    size_t capacity;
    int    is_valid;
} AudioBuffer;

/* HTTP 响应 */
typedef struct {
    int    code;
    char*  reply_text;
    size_t reply_len;
    int    is_success;
} HttpResponse;

/* 前向声明各模块结构体（定义在各模块头文件中） */
typedef struct AudioCapture AudioCapture;
typedef struct WiFiManager  WiFiManager;
typedef struct HttpClient   HttpClient;
typedef struct Display      Display;
typedef struct ButtonLED    ButtonLED;

/* 状态机上下文 — 聚合所有模块引用 */
typedef struct {
    AudioCapture* audio;
    WiFiManager*  wifi;
    HttpClient*   http;
    Display*      display;
    ButtonLED*    button_led;
    AudioBuffer   recording;
    HttpResponse  response;
    int           error_code;
    char          error_msg[256];
} StateContext;

/* ===================================================================
 * 日志宏 — 依赖全局变量 g_log_level
 * =================================================================== */
extern int g_log_level;

#define LOG_ERROR(fmt, ...) \
    if (g_log_level >= 0)   \
        fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)  \
    if (g_log_level >= 1)   \
        fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__)

#define LOG_INFO(fmt, ...)  \
    if (g_log_level >= 2)   \
        fprintf(stdout, "[INFO]  " fmt "\n", ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    if (g_log_level >= 3)   \
        fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__)

#endif /* _COMMON_TYPES_H_ */
