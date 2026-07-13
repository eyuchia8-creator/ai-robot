/* ===================================================================
 * state_machine.c — 交互状态机核心实现
 *
 * 驱动语音机器人完整交互流程:
 *   IDLE → RECORDING → PROCESSING → RESULT/ERROR → IDLE
 *
 * 各状态的 enter/update/exit 全部在此模块中实现。
 *
 * 目标平台: Linux ARM aarch64
 * =================================================================== */

#include "state_machine.h"
#include "button_led.h"
#include "audio_capture.h"
#include "http_client.h"
#include "wifi_manager.h"
#include "display.h"
#include "error_codes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* ===================================================================
 * 模块级静态数据
 * =================================================================== */

/* 录音缓冲区 — 静态分配, 16kHz * 2 bytes/sample * 60s = 1.92MB */
static char g_audio_buffer[MAX_RECORD_BYTES];

/* PROCESSING 状态中是否已发起 HTTP 上传 (防止重复上传) */
static int g_upload_attempted = 0;

/* LED 闪烁上次切换时间 (秒) */
static time_t g_led_last_toggle = 0;
/* LED 闪烁当前值 (0=灭, 1=亮) */
static int g_led_blink_value = 0;

/* ===================================================================
 * 内部辅助函数声明
 * =================================================================== */

/* HTTP 响应回调 */
static void on_http_response(int http_code, const char* reply,
                              size_t len, void* user_data);

/* 各状态 enter 处理函数 */
static int idle_enter(StateMachine* sm);
static int recording_enter(StateMachine* sm);
static int processing_enter(StateMachine* sm);
static int result_enter(StateMachine* sm);
static int error_enter(StateMachine* sm);

/* 各状态 update 处理函数 */
static int idle_update(StateMachine* sm);
static int recording_update(StateMachine* sm);
static int processing_update(StateMachine* sm);
static int result_update(StateMachine* sm);
static int error_update(StateMachine* sm);

/* 各状态 exit 处理函数 */
static int idle_exit(StateMachine* sm);
static int recording_exit(StateMachine* sm);
static int processing_exit(StateMachine* sm);
static int result_exit(StateMachine* sm);
static int error_exit(StateMachine* sm);

/* 注册状态转换表 */
static void register_transitions(StateMachine* sm);

/* 执行状态的 enter */
static int state_enter(StateMachine* sm, StateEnum state);

/* 执行状态的 exit */
static int state_exit(StateMachine* sm, StateEnum state);

/* LED 闪烁更新 (在需要闪烁的状态的 update 中调用) */
static void led_blink_update(StateMachine* sm, int interval_sec);

/* ===================================================================
 * API 实现
 * =================================================================== */

/* ---- state_machine_init ---- */
int state_machine_init(StateMachine* sm, StateContext* ctx)
{
    if (sm == NULL || ctx == NULL) {
        LOG_ERROR("state_machine_init: NULL pointer");
        return ERR_INTERNAL;
    }

    memset(sm, 0, sizeof(*sm));

    /* 复制上下文 */
    memcpy(&sm->ctx, ctx, sizeof(StateContext));

    /* 初始化录音缓冲区 */
    sm->ctx.recording.data     = g_audio_buffer;
    sm->ctx.recording.size     = 0;
    sm->ctx.recording.capacity = MAX_RECORD_BYTES;
    sm->ctx.recording.is_valid = 1;

    /* 初始化响应字段 */
    sm->ctx.response.code       = 0;
    sm->ctx.response.reply_text = NULL;
    sm->ctx.response.reply_len  = 0;
    sm->ctx.response.is_success = 0;

    /* 初始化错误字段 */
    sm->ctx.error_code = 0;
    memset(sm->ctx.error_msg, 0, sizeof(sm->ctx.error_msg));

    /* 注册状态转换表 */
    register_transitions(sm);

    /* 设置初始状态 */
    sm->current_state    = STATE_IDLE;
    sm->prev_state       = STATE_IDLE;
    sm->state_enter_time = (int)time(NULL);

    /* 执行初始状态 enter */
    state_enter(sm, STATE_IDLE);

    LOG_INFO("state_machine_init: initialized, state=IDLE");
    return ERR_OK;
}

/* ---- state_machine_handle_event ---- */
int state_machine_handle_event(StateMachine* sm, EventEnum event)
{
    int i;

    if (sm == NULL) {
        LOG_ERROR("state_machine_handle_event: NULL pointer");
        return ERR_INTERNAL;
    }

    /* 查找匹配的状态转换 */
    for (i = 0; i < sm->transition_count; i++) {
        if (sm->transitions[i].from_state == sm->current_state &&
            sm->transitions[i].event      == event) {

            StateEnum new_state = sm->transitions[i].to_state;

            LOG_INFO("state_machine_handle_event: %d --(%d)--> %d",
                     (int)sm->current_state, (int)event, (int)new_state);

            /* 执行旧状态 exit */
            state_exit(sm, sm->current_state);

            /* 切换状态 */
            sm->prev_state       = sm->current_state;
            sm->current_state    = new_state;
            sm->state_enter_time = (int)time(NULL);

            /* 执行新状态 enter */
            state_enter(sm, new_state);

            return ERR_OK;
        }
    }

    LOG_DEBUG("state_machine_handle_event: no transition for state=%d event=%d",
              (int)sm->current_state, (int)event);
    return ERR_INTERNAL; /* 无匹配转换 */
}

/* ---- state_machine_run ---- */
int state_machine_run(StateMachine* sm)
{
    if (sm == NULL) {
        LOG_ERROR("state_machine_run: NULL pointer");
        return ERR_INTERNAL;
    }

    /* 根据当前状态调用对应的 update 处理函数 */
    switch (sm->current_state) {
    case STATE_IDLE:
        return idle_update(sm);
    case STATE_RECORDING:
        return recording_update(sm);
    case STATE_PROCESSING:
        return processing_update(sm);
    case STATE_RESULT:
        return result_update(sm);
    case STATE_ERROR:
        return error_update(sm);
    default:
        LOG_ERROR("state_machine_run: unknown state %d",
                  (int)sm->current_state);
        return ERR_INTERNAL;
    }
}

/* ---- state_machine_get_state ---- */
StateEnum state_machine_get_state(StateMachine* sm)
{
    if (sm == NULL) {
        return STATE_ERROR;
    }
    return sm->current_state;
}

/* ---- state_machine_deinit ---- */
int state_machine_deinit(StateMachine* sm)
{
    if (sm == NULL) {
        return ERR_OK;
    }

    /* 如果当前不在 IDLE，清理当前状态 */
    if (sm->current_state != STATE_IDLE) {
        state_exit(sm, sm->current_state);
    }

    memset(sm, 0, sizeof(*sm));
    sm->current_state = STATE_ERROR; /* 标记为无效 */

    LOG_INFO("state_machine_deinit: released");
    return ERR_OK;
}

/* ===================================================================
 * 状态 enter/update/exit 实现
 * =================================================================== */

/* ---- IDLE enter ---- */
static int idle_enter(StateMachine* sm)
{
    (void)sm;

    if (sm->ctx.display != NULL) {
        display_clear(sm->ctx.display);
        display_set_cursor(sm->ctx.display, 10, 50);
        display_draw_text(sm->ctx.display, "\346\214\211\344\270\213\346\214\211\351\222\256\345\274\200\345\247\213\346\217\220\351\227\256");
        /* UTF-8: "按下按钮开始提问" */
    }

    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_OFF);
    }

    /* 重置上传标记 (安全措施) */
    g_upload_attempted = 0;

    LOG_DEBUG("idle_enter: display updated, LED off");
    return ERR_OK;
}

/* ---- IDLE update ---- */
static int idle_update(StateMachine* sm)
{
    int button_state;

    if (sm->ctx.button_led == NULL) {
        return ERR_OK;
    }

    /* 轮询按键 */
    button_state = button_led_read_button(sm->ctx.button_led);
    if (button_state < 0) {
        LOG_WARN("idle_update: button read error %d", button_state);
        return ERR_OK;
    }

    if (button_state == 1) {
        /* 按键按下 → 进入录音 */
        LOG_INFO("idle_update: button pressed, transitioning to RECORDING");
        state_machine_handle_event(sm, EVENT_BUTTON_PRESS);
    }

    return ERR_OK;
}

/* ---- IDLE exit ---- */
static int idle_exit(StateMachine* sm)
{
    (void)sm;
    LOG_DEBUG("idle_exit: leaving IDLE");
    return ERR_OK;
}

/* ---- RECORDING enter ---- */
static int recording_enter(StateMachine* sm)
{
    int rc;

    if (sm->ctx.audio == NULL) {
        LOG_ERROR("recording_enter: audio module not available");
        sm->ctx.error_code = ERR_AUDIO_OPEN;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "音频模块未初始化");
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
        return ERR_AUDIO_OPEN;
    }

    /* 打开音频设备 */
    rc = audio_capture_open(sm->ctx.audio, AUDIO_CAPTURE_DEFAULT_DEVICE);
    if (rc != ERR_OK) {
        LOG_ERROR("recording_enter: audio_capture_open failed");
        sm->ctx.error_code = ERR_AUDIO_OPEN;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "音频设备打开失败");
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
        return ERR_AUDIO_OPEN;
    }

    /* 设置 PCM 参数 */
    rc = audio_capture_set_params(sm->ctx.audio);
    if (rc != ERR_OK) {
        LOG_ERROR("recording_enter: audio_capture_set_params failed");
        audio_capture_close(sm->ctx.audio);
        sm->ctx.error_code = ERR_AUDIO_PARAMS;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "音频参数设置失败");
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
        return ERR_AUDIO_PARAMS;
    }

    /* 重置录音缓冲区 */
    sm->ctx.recording.size     = 0;
    sm->ctx.recording.is_valid = 1;

    /* 显示录音界面 */
    if (sm->ctx.display != NULL) {
        display_clear(sm->ctx.display);
        display_set_cursor(sm->ctx.display, 10, 50);
        display_draw_text(sm->ctx.display, "\346\255\243\345\234\250\350\201\206\345\220\254...");
        /* UTF-8: "正在聆听..." */
    }

    /* 点亮 LED */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_ON);
    }

    LOG_INFO("recording_enter: audio opened, recording started");
    return ERR_OK;
}

/* ---- RECORDING update ---- */
static int recording_update(StateMachine* sm)
{
    int    button_state;
    time_t now;
    int    elapsed;
    size_t remaining;
    ssize_t bytes_read;

    if (sm->ctx.audio == NULL) {
        return ERR_OK;
    }

    /* 采集音频数据 */
    remaining = sm->ctx.recording.capacity - sm->ctx.recording.size;
    if (remaining > 0) {
        bytes_read = audio_capture_read(sm->ctx.audio,
                                         sm->ctx.recording.data + sm->ctx.recording.size,
                                         remaining);
        if (bytes_read > 0) {
            sm->ctx.recording.size += (size_t)bytes_read;
        } else if (bytes_read < 0) {
            LOG_WARN("recording_update: audio read error %zd, continuing",
                     bytes_read);
        }
    }

    /* 检查超时 (60 秒) */
    now     = time(NULL);
    elapsed = (int)(now - sm->state_enter_time);
    if (elapsed >= RECORDING_TIMEOUT_SEC) {
        LOG_INFO("recording_update: timeout %ds, auto-stopping", elapsed);
        state_machine_handle_event(sm, EVENT_TIMEOUT);
        return ERR_OK;
    }

    /* 轮询按键释放 */
    if (sm->ctx.button_led != NULL) {
        button_state = button_led_read_button(sm->ctx.button_led);
        if (button_state == 0) {
            /* 按键释放 → 停止录音，进入处理 */
            LOG_INFO("recording_update: button released, stopping recording");
            state_machine_handle_event(sm, EVENT_BUTTON_RELEASE);
        }
    }

    return ERR_OK;
}

/* ---- RECORDING exit ---- */
static int recording_exit(StateMachine* sm)
{
    if (sm->ctx.audio != NULL) {
        audio_capture_close(sm->ctx.audio);
        LOG_INFO("recording_exit: audio device closed, recorded %zu bytes",
                 sm->ctx.recording.size);
    }

    /* LED 关闭 (新状态 enter 会重新设置) */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_OFF);
    }

    sm->ctx.recording.is_valid = (sm->ctx.recording.size > 0) ? 1 : 0;

    return ERR_OK;
}

/* ---- PROCESSING enter ---- */
static int processing_enter(StateMachine* sm)
{
    (void)sm;

    /* 显示处理中界面 */
    if (sm->ctx.display != NULL) {
        display_clear(sm->ctx.display);
        display_set_cursor(sm->ctx.display, 10, 50);
        display_draw_text(sm->ctx.display, "\346\255\243\345\234\250\346\200\235\350\200\203...");
        /* UTF-8: "正在思考..." */
    }

    /* 设置 LED 快速闪烁 */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_BLINK_FAST);
    }

    /* 重置上传标记 */
    g_upload_attempted  = 0;
    g_led_last_toggle   = 0;
    g_led_blink_value   = 1; /* 初始为亮 */

    /* 初始化响应 */
    sm->ctx.response.code       = 0;
    sm->ctx.response.reply_text = NULL;
    sm->ctx.response.reply_len  = 0;
    sm->ctx.response.is_success = 0;

    LOG_INFO("processing_enter: recording=%zu bytes, starting upload",
             sm->ctx.recording.size);
    return ERR_OK;
}

/* ---- PROCESSING update ---- */
static int processing_update(StateMachine* sm)
{
    int rc;
    int wifi_rc;

    /* 如果已经发起上传，只处理 LED 闪烁 */
    if (g_upload_attempted) {
        led_blink_update(sm, 1); /* FAST blink: 1s 周期 */
        return ERR_OK;
    }

    /* 检查录音数据有效性 */
    if (!sm->ctx.recording.is_valid || sm->ctx.recording.size == 0) {
        LOG_ERROR("processing_update: no recording data");
        sm->ctx.error_code = ERR_INTERNAL;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "无录音数据");
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
        return ERR_OK;
    }

    /* 检查 WiFi 连接 */
    if (sm->ctx.wifi != NULL) {
        wifi_rc = wifi_manager_check_status(sm->ctx.wifi);
        if (wifi_rc != ERR_OK) {
            LOG_WARN("processing_update: WiFi disconnected, attempting reconnect");
            wifi_rc = wifi_manager_reconnect(sm->ctx.wifi);
            if (wifi_rc != ERR_OK) {
                LOG_ERROR("processing_update: WiFi reconnect failed");
                sm->ctx.error_code = ERR_WIFI_DISCONNECTED;
                snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                         "WiFi \345\267\262\346\226\255\345\274\200");
                /* UTF-8: "WiFi 已断开" */
                state_machine_handle_event(sm, EVENT_HTTP_FAIL);
                return ERR_OK;
            }
            LOG_INFO("processing_update: WiFi reconnected");
        }
    }

    /* 注册 HTTP 响应回调 */
    if (sm->ctx.http != NULL) {
        http_client_set_callback(sm->ctx.http, on_http_response, (void*)sm);
    } else {
        LOG_ERROR("processing_update: HTTP client not available");
        sm->ctx.error_code = ERR_HTTP_CONNECT;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "HTTP \345\256\242\346\210\267\347\253\257\346\234\252\345\210\235\345\247\213\345\214\226");
        /* UTF-8: "HTTP 客户端未初始化" */
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
        return ERR_OK;
    }

    /* 标记已发起上传 (必须在 http_client_upload_audio 前设置，
     * 因为回调可能在内部同步触发，导致 update 被再次调用) */
    g_upload_attempted = 1;

    /* 发起 HTTP 上传 (同步阻塞, 内部回调会触发状态转换) */
    rc = http_client_upload_audio(sm->ctx.http,
                                   sm->ctx.recording.data,
                                   sm->ctx.recording.size);

    /* 如果返回错误且状态未变化 (回调未被触发，curl 级错误) */
    if (rc != ERR_OK && sm->current_state == STATE_PROCESSING) {
        LOG_ERROR("processing_update: upload failed with rc=%d, no callback", rc);
        sm->ctx.error_code = (rc == ERR_HTTP_TIMEOUT) ? ERR_HTTP_TIMEOUT
                                                       : ERR_HTTP_CONNECT;
        snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                 "\347\275\221\347\273\234\351\224\231\350\257\257\357\274\210\344\273\243\347\240\201 %d\357\274\211", rc);
        /* UTF-8: "网络错误（代码 %d）" */
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
    }

    return ERR_OK;
}

/* ---- PROCESSING exit ---- */
static int processing_exit(StateMachine* sm)
{
    /* 停止 LED 闪烁 */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_OFF);
    }

    g_upload_attempted = 0;
    g_led_blink_value  = 0;

    LOG_DEBUG("processing_exit: LED blink stopped");
    return ERR_OK;
}

/* ---- RESULT enter ---- */
static int result_enter(StateMachine* sm)
{
    const char* reply_text;

    /* 显示 AI 回复 */
    if (sm->ctx.display != NULL) {
        display_clear(sm->ctx.display);

        reply_text = sm->ctx.response.reply_text;
        if (reply_text != NULL && reply_text[0] != '\0') {
            display_set_cursor(sm->ctx.display, 10, 50);
            display_draw_text(sm->ctx.display, reply_text);
        } else {
            display_set_cursor(sm->ctx.display, 10, 50);
            display_draw_text(sm->ctx.display, "\357\274\210\346\227\240\345\233\236\345\244\215\345\206\205\345\256\271\357\274\211");
            /* UTF-8: "（无回复内容）" */
        }
    }

    /* 关闭 LED */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_OFF);
    }

    LOG_INFO("result_enter: reply displayed");
    return ERR_OK;
}

/* ---- RESULT update ---- */
static int result_update(StateMachine* sm)
{
    time_t now;
    int    elapsed;
    int    button_state;

    now     = time(NULL);
    elapsed = (int)(now - sm->state_enter_time);

    /* 5 秒超时 → 回到 IDLE */
    if (elapsed >= RESULT_DISPLAY_SEC) {
        LOG_INFO("result_update: display timeout %ds, returning to IDLE", elapsed);
        state_machine_handle_event(sm, EVENT_TIMEOUT);
        return ERR_OK;
    }

    /* 按键按下 → 提前进入 RECORDING */
    if (sm->ctx.button_led != NULL) {
        button_state = button_led_read_button(sm->ctx.button_led);
        if (button_state == 1) {
            LOG_INFO("result_update: button pressed, starting new recording");
            state_machine_handle_event(sm, EVENT_BUTTON_PRESS);
        }
    }

    return ERR_OK;
}

/* ---- RESULT exit ---- */
static int result_exit(StateMachine* sm)
{
    (void)sm;
    LOG_DEBUG("result_exit: leaving RESULT");
    return ERR_OK;
}

/* ---- ERROR enter ---- */
static int error_enter(StateMachine* sm)
{
    char display_buf[320];

    /* 显示错误信息 */
    if (sm->ctx.display != NULL) {
        display_clear(sm->ctx.display);

        /* 显示错误图标 + 错误消息 */
        display_set_cursor(sm->ctx.display, 10, 40);
        display_draw_text(sm->ctx.display, "\342\234\227 \351\224\231\350\257\257");
        /* UTF-8: "✗ 错误" */

        display_set_cursor(sm->ctx.display, 10, 80);
        if (sm->ctx.error_msg[0] != '\0') {
            display_draw_text(sm->ctx.display, sm->ctx.error_msg);
        } else {
            snprintf(display_buf, sizeof(display_buf),
                     "\351\224\231\350\257\257\344\273\243\347\240\201: %d",
                     sm->ctx.error_code);
            /* UTF-8: "错误代码: %d" */
            display_draw_text(sm->ctx.display, display_buf);
        }
    }

    /* 关闭 LED */
    if (sm->ctx.button_led != NULL) {
        button_led_set_led(sm->ctx.button_led, LED_OFF);
    }

    LOG_INFO("error_enter: code=%d msg='%s'",
             sm->ctx.error_code, sm->ctx.error_msg);
    return ERR_OK;
}

/* ---- ERROR update ---- */
static int error_update(StateMachine* sm)
{
    time_t now;
    int    elapsed;
    int    button_state;

    now     = time(NULL);
    elapsed = (int)(now - sm->state_enter_time);

    /* 3 秒超时 → 回到 IDLE */
    if (elapsed >= ERROR_DISPLAY_SEC) {
        LOG_INFO("error_update: display timeout %ds, returning to IDLE", elapsed);
        state_machine_handle_event(sm, EVENT_TIMEOUT);
        return ERR_OK;
    }

    /* 按键按下 → 提前进入 RECORDING */
    if (sm->ctx.button_led != NULL) {
        button_state = button_led_read_button(sm->ctx.button_led);
        if (button_state == 1) {
            LOG_INFO("error_update: button pressed, starting new recording");
            state_machine_handle_event(sm, EVENT_BUTTON_PRESS);
        }
    }

    return ERR_OK;
}

/* ---- ERROR exit ---- */
static int error_exit(StateMachine* sm)
{
    (void)sm;
    LOG_DEBUG("error_exit: leaving ERROR");
    return ERR_OK;
}

/* ===================================================================
 * HTTP 响应回调 (内部 static)
 * =================================================================== */

/**
 * on_http_response — HTTP 上传完成回调
 *
 * 由 http_client 模块在收到 AI 服务器响应后调用。
 * 解析成功 → 触发 EVENT_HTTP_SUCCESS → 进入 RESULT
 * 解析失败 → 触发 EVENT_HTTP_FAIL    → 进入 ERROR
 */
static void on_http_response(int http_code, const char* reply,
                              size_t len, void* user_data)
{
    StateMachine* sm = (StateMachine*)user_data;

    if (sm == NULL) {
        LOG_ERROR("on_http_response: NULL state machine");
        return;
    }

    LOG_INFO("on_http_response: HTTP %d, reply_len=%zu", http_code, len);

    if (http_code >= 200 && http_code < 400 && reply != NULL) {
        /* 成功响应: 复制回复文本 */

        /* reply_text 指向静态缓冲区 — 使用 HTTP_RESPONSE_BUF_SIZE 大小的内部存储
         * 此处借助 sm->ctx.error_msg 的末尾空间临时存储 (因 error_msg[256] 足够大)
         * 更安全的做法: 将 reply_text 指向 http_client 内部的静态 buffer
         *
         * 实际方案: 直接使用 reply 指针 (指向 http_client 的 HttpResponseBuf.data)，
         * 在触发 EVENT 之后才可能被覆盖，而 EVENT 是同步处理的 (enter 立即显示)，
         * 所以直接使用指针是安全的。
         */
        sm->ctx.response.code       = http_code;
        sm->ctx.response.reply_text = (char*)reply; /* 指向 http_client 内部 buffer */
        sm->ctx.response.reply_len  = len;
        sm->ctx.response.is_success = 1;
        sm->ctx.error_code          = 0;

        LOG_INFO("on_http_response: success, reply='%s'", reply);
        state_machine_handle_event(sm, EVENT_HTTP_SUCCESS);
    } else {
        /* 失败响应 */
        sm->ctx.response.code       = http_code;
        sm->ctx.response.reply_text = NULL;
        sm->ctx.response.reply_len  = 0;
        sm->ctx.response.is_success = 0;
        sm->ctx.error_code          = ERR_HTTP_SERVER_ERROR;

        if (http_code >= 400) {
            snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                     "\346\234\215\345\212\241\345\231\250\351\224\231\350\257\257 (HTTP %d)", http_code);
            /* UTF-8: "服务器错误 (HTTP %d)" */
        } else if (reply != NULL && reply[0] != '\0') {
            snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                     "\350\247\243\346\236\220\345\244\261\350\264\245: %s", reply);
            /* UTF-8: "解析失败: %s" */
        } else {
            snprintf(sm->ctx.error_msg, sizeof(sm->ctx.error_msg),
                     "\346\234\215\345\212\241\345\231\250\351\224\231\350\257\257 (HTTP %d)", http_code);
            /* UTF-8: "服务器错误 (HTTP %d)" */
        }

        LOG_WARN("on_http_response: fail, HTTP %d", http_code);
        state_machine_handle_event(sm, EVENT_HTTP_FAIL);
    }
}

/* ===================================================================
 * 内部辅助函数
 * =================================================================== */

/* ---- register_transitions ---- */
static void register_transitions(StateMachine* sm)
{
    sm->transition_count = 0;

    /* 按任务规格精确注册 9 条转换规则 */

    /* IDLE → RECORDING */
    sm->transitions[sm->transition_count].from_state = STATE_IDLE;
    sm->transitions[sm->transition_count].event      = EVENT_BUTTON_PRESS;
    sm->transitions[sm->transition_count].to_state   = STATE_RECORDING;
    sm->transition_count++;

    /* RECORDING → PROCESSING (按键释放) */
    sm->transitions[sm->transition_count].from_state = STATE_RECORDING;
    sm->transitions[sm->transition_count].event      = EVENT_BUTTON_RELEASE;
    sm->transitions[sm->transition_count].to_state   = STATE_PROCESSING;
    sm->transition_count++;

    /* RECORDING → PROCESSING (超时) */
    sm->transitions[sm->transition_count].from_state = STATE_RECORDING;
    sm->transitions[sm->transition_count].event      = EVENT_TIMEOUT;
    sm->transitions[sm->transition_count].to_state   = STATE_PROCESSING;
    sm->transition_count++;

    /* PROCESSING → RESULT */
    sm->transitions[sm->transition_count].from_state = STATE_PROCESSING;
    sm->transitions[sm->transition_count].event      = EVENT_HTTP_SUCCESS;
    sm->transitions[sm->transition_count].to_state   = STATE_RESULT;
    sm->transition_count++;

    /* PROCESSING → ERROR */
    sm->transitions[sm->transition_count].from_state = STATE_PROCESSING;
    sm->transitions[sm->transition_count].event      = EVENT_HTTP_FAIL;
    sm->transitions[sm->transition_count].to_state   = STATE_ERROR;
    sm->transition_count++;

    /* RESULT → IDLE (超时) */
    sm->transitions[sm->transition_count].from_state = STATE_RESULT;
    sm->transitions[sm->transition_count].event      = EVENT_TIMEOUT;
    sm->transitions[sm->transition_count].to_state   = STATE_IDLE;
    sm->transition_count++;

    /* RESULT → RECORDING (按键按下) */
    sm->transitions[sm->transition_count].from_state = STATE_RESULT;
    sm->transitions[sm->transition_count].event      = EVENT_BUTTON_PRESS;
    sm->transitions[sm->transition_count].to_state   = STATE_RECORDING;
    sm->transition_count++;

    /* ERROR → IDLE (超时) */
    sm->transitions[sm->transition_count].from_state = STATE_ERROR;
    sm->transitions[sm->transition_count].event      = EVENT_TIMEOUT;
    sm->transitions[sm->transition_count].to_state   = STATE_IDLE;
    sm->transition_count++;

    /* ERROR → RECORDING (按键按下) */
    sm->transitions[sm->transition_count].from_state = STATE_ERROR;
    sm->transitions[sm->transition_count].event      = EVENT_BUTTON_PRESS;
    sm->transitions[sm->transition_count].to_state   = STATE_RECORDING;
    sm->transition_count++;

    LOG_DEBUG("register_transitions: %d transitions registered",
              sm->transition_count);
}

/* ---- state_enter ---- */
static int state_enter(StateMachine* sm, StateEnum state)
{
    switch (state) {
    case STATE_IDLE:       return idle_enter(sm);
    case STATE_RECORDING:  return recording_enter(sm);
    case STATE_PROCESSING: return processing_enter(sm);
    case STATE_RESULT:     return result_enter(sm);
    case STATE_ERROR:      return error_enter(sm);
    default:
        LOG_ERROR("state_enter: unknown state %d", (int)state);
        return ERR_INTERNAL;
    }
}

/* ---- state_exit ---- */
static int state_exit(StateMachine* sm, StateEnum state)
{
    switch (state) {
    case STATE_IDLE:       return idle_exit(sm);
    case STATE_RECORDING:  return recording_exit(sm);
    case STATE_PROCESSING: return processing_exit(sm);
    case STATE_RESULT:     return result_exit(sm);
    case STATE_ERROR:      return error_exit(sm);
    default:
        LOG_ERROR("state_exit: unknown state %d", (int)state);
        return ERR_INTERNAL;
    }
}

/* ---- led_blink_update — LED 闪烁逻辑 (在 PROCESSING update 中调用) ---- */
static void led_blink_update(StateMachine* sm, int interval_sec)
{
    time_t now;

    if (sm->ctx.button_led == NULL) {
        return;
    }

    if (interval_sec <= 0) {
        interval_sec = 1;
    }

    now = time(NULL);

    /* 每隔 interval_sec 秒切换一次 LED */
    if (now - g_led_last_toggle >= (time_t)interval_sec) {
        g_led_last_toggle  = now;
        g_led_blink_value  = (g_led_blink_value == 0) ? 1 : 0;

        if (g_led_blink_value) {
            button_led_set_led(sm->ctx.button_led, LED_ON);
        } else {
            button_led_set_led(sm->ctx.button_led, LED_OFF);
        }
    }
}
