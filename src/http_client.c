/* ===================================================================
 * http_client.c — 基于 libcurl 的 HTTP 客户端实现
 *
 * 功能:
 *   1. multipart/form-data 上传 WAV 音频文件
 *   2. cJSON 解析 AI 服务器返回的 {"reply":"..."} JSON
 *   3. ResponseCallback 回调通知上层
 *
 * 目标平台: Linux ARM aarch64
 * 依赖: libcurl, cJSON
 * =================================================================== */

#include "http_client.h"
#include "audio_capture.h"
#include "error_codes.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- 内部数据结构 ---- */

/* curl write callback 使用的响应缓冲区 */
typedef struct {
    char   data[HTTP_RESPONSE_BUF_SIZE];
    size_t size;
} HttpResponseBuf;

/* ---- 内部辅助函数声明 ---- */

static size_t write_callback(void* ptr, size_t size, size_t nmemb,
                              void* userdata);
static void   build_wav_header(char* header_buf, size_t pcm_data_size,
                                unsigned int sample_rate,
                                int channels, int bits_per_sample);
static int    parse_reply_json(const char* json_text, HttpClient* hc);

/* ---- init ---- */
int http_client_init(HttpClient* hc, const char* server_url, int timeout_sec)
{
    CURL* curl;

    if (hc == NULL) {
        LOG_ERROR("http_client_init: NULL pointer");
        return ERR_HTTP_CONNECT;
    }

    memset(hc, 0, sizeof(*hc));

    /* 设置 URL */
    if (server_url != NULL && server_url[0] != '\0') {
        strncpy(hc->server_url, server_url, sizeof(hc->server_url) - 1);
        hc->server_url[sizeof(hc->server_url) - 1] = '\0';
    }

    /* 设置超时 */
    hc->timeout_sec = (timeout_sec > 0) ? timeout_sec : HTTP_DEFAULT_TIMEOUT_SEC;

    /* 初始化 libcurl */
    curl = curl_easy_init();
    if (curl == NULL) {
        LOG_ERROR("http_client_init: curl_easy_init failed");
        return ERR_HTTP_CONNECT;
    }
    hc->curl = (void*)curl;

    /* 设置基础选项 */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)hc->timeout_sec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    LOG_DEBUG("http_client_init: url=%s timeout=%d",
              hc->server_url, hc->timeout_sec);
    return ERR_OK;
}

/* ---- set_callback ---- */
int http_client_set_callback(HttpClient* hc, ResponseCallback cb,
                              void* user_data)
{
    if (hc == NULL) {
        LOG_ERROR("http_client_set_callback: NULL pointer");
        return ERR_HTTP_CONNECT;
    }

    hc->on_response_cb = cb;
    hc->cb_user_data   = user_data;
    return ERR_OK;
}

/* ---- upload_audio ---- */
int http_client_upload_audio(HttpClient* hc, const char* wav_data,
                              size_t wav_size)
{
    CURL*    curl;
    CURLcode res;
    curl_mime*    mime     = NULL;
    curl_mimepart* part    = NULL;
    HttpResponseBuf resp_buf;

    if (hc == NULL || hc->curl == NULL || wav_data == NULL || wav_size == 0) {
        LOG_ERROR("http_client_upload_audio: invalid parameters");
        return ERR_HTTP_CONNECT;
    }

    curl = (CURL*)hc->curl;

    /* 1. 构建完整 WAV 数据 (带 header) */
    char* full_wav = NULL;
    size_t full_size;
    const char* upload_data;
    size_t upload_size;

    /* 检测是否已有 WAV header ("RIFF" magic) */
    if (wav_size >= 4 && memcmp(wav_data, "RIFF", 4) == 0) {
        /* 已有 WAV header，直接使用 */
        upload_data = wav_data;
        upload_size = wav_size;
        LOG_DEBUG("http_client_upload_audio: data has WAV header, using directly");
    } else {
        /* 纯 PCM 数据，拼接 WAV header */
        full_size = WAV_HEADER_SIZE + wav_size;
        full_wav  = (char*)malloc(full_size);
        if (full_wav == NULL) {
            LOG_ERROR("http_client_upload_audio: malloc failed for WAV buffer");
            return ERR_MEMORY_ALLOC;
        }

        build_wav_header(full_wav, wav_size,
                         AUDIO_CAPTURE_DEFAULT_SAMPLE_RATE,
                         AUDIO_CAPTURE_DEFAULT_CHANNELS,
                         16);
        memcpy(full_wav + WAV_HEADER_SIZE, wav_data, wav_size);

        upload_data = full_wav;
        upload_size = full_size;
        LOG_DEBUG("http_client_upload_audio: prepended WAV header, total=%zu",
                  full_size);
    }

    /* 2. 构建 multipart/form-data */
    mime = curl_mime_init(curl);
    if (mime == NULL) {
        LOG_ERROR("http_client_upload_audio: curl_mime_init failed");
        if (full_wav != NULL) free(full_wav);
        return ERR_HTTP_CONNECT;
    }

    part = curl_mime_addpart(mime);
    if (part == NULL) {
        LOG_ERROR("http_client_upload_audio: curl_mime_addpart failed");
        curl_mime_free(mime);
        if (full_wav != NULL) free(full_wav);
        return ERR_HTTP_CONNECT;
    }

    curl_mime_name(part, "audio");
    curl_mime_filename(part, "recording.wav");
    curl_mime_type(part, "audio/wav");
    curl_mime_data(part, upload_data, upload_size);

    /* 3. 设置响应接收缓冲区 */
    memset(&resp_buf, 0, sizeof(resp_buf));

    curl_easy_setopt(curl, CURLOPT_URL, hc->server_url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp_buf);

    /* 4. 执行请求 */
    LOG_INFO("http_client_upload_audio: POST %zu bytes to %s",
             upload_size, hc->server_url);

    res = curl_easy_perform(curl);

    /* 5. 清理 MIME (数据已发送) */
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, NULL);
    curl_mime_free(mime);
    if (full_wav != NULL) free(full_wav);

    /* 6. 检查结果 */
    if (res != CURLE_OK) {
        LOG_ERROR("http_client_upload_audio: curl_easy_perform: %s",
                  curl_easy_strerror(res));
        if (res == CURLE_OPERATION_TIMEDOUT) {
            return ERR_HTTP_TIMEOUT;
        }
        return ERR_HTTP_CONNECT;
    }

    /* 检查 HTTP 状态码 */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    LOG_DEBUG("http_client_upload_audio: HTTP %ld, response=%zu bytes",
              http_code, resp_buf.size);

    if (http_code >= 400) {
        LOG_ERROR("http_client_upload_audio: server error HTTP %ld", http_code);
        if (hc->on_response_cb != NULL) {
            hc->on_response_cb((int)http_code, resp_buf.data,
                               resp_buf.size, hc->cb_user_data);
        }
        return ERR_HTTP_SERVER_ERROR;
    }

    /* 7. 解析 JSON 响应 */
    if (resp_buf.size > 0) {
        resp_buf.data[resp_buf.size] = '\0'; /* 安全截断 */
        return parse_reply_json(resp_buf.data, hc);
    }

    /* 空响应 */
    if (hc->on_response_cb != NULL) {
        hc->on_response_cb((int)http_code, "", 0, hc->cb_user_data);
    }
    return ERR_OK;
}

/* ---- deinit ---- */
int http_client_deinit(HttpClient* hc)
{
    if (hc == NULL) {
        return ERR_OK;
    }

    if (hc->curl != NULL) {
        curl_easy_cleanup((CURL*)hc->curl);
        hc->curl = NULL;
        LOG_DEBUG("http_client_deinit: curl cleaned up");
    }

    hc->on_response_cb = NULL;
    hc->cb_user_data   = NULL;
    hc->timeout_sec    = 0;

    return ERR_OK;
}

/* ===================================================================
 * 内部辅助函数
 * =================================================================== */

/* ---- curl write callback ---- */
static size_t write_callback(void* ptr, size_t size, size_t nmemb,
                              void* userdata)
{
    HttpResponseBuf* buf = (HttpResponseBuf*)userdata;
    size_t total = size * nmemb;

    if (buf == NULL) return 0;

    if (buf->size + total >= HTTP_RESPONSE_BUF_SIZE) {
        /* 缓冲区满，截断 */
        total = HTTP_RESPONSE_BUF_SIZE - buf->size - 1;
        if (total == 0) return 0;
    }

    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';

    return size * nmemb; /* 返回原始大小，保持 curl 行为一致 */
}

/* ---- build_wav_header — 生成 44 字节标准 WAV header ---- */
static void build_wav_header(char* header_buf, size_t pcm_data_size,
                              unsigned int sample_rate,
                              int channels, int bits_per_sample)
{
    unsigned int byte_rate;
    unsigned short block_align;
    unsigned int data_size;
    unsigned int file_size;

    if (header_buf == NULL) return;

    byte_rate   = sample_rate * (unsigned int)channels * (unsigned int)(bits_per_sample / 8);
    block_align = (unsigned short)(channels * (bits_per_sample / 8));
    data_size   = (unsigned int)pcm_data_size;
    file_size   = 36 + data_size; /* 36 = total header bytes after "RIFF" size field */

    /* RIFF header */
    header_buf[0]  = 'R';
    header_buf[1]  = 'I';
    header_buf[2]  = 'F';
    header_buf[3]  = 'F';
    header_buf[4]  = (char)(file_size & 0xFF);
    header_buf[5]  = (char)((file_size >> 8) & 0xFF);
    header_buf[6]  = (char)((file_size >> 16) & 0xFF);
    header_buf[7]  = (char)((file_size >> 24) & 0xFF);
    header_buf[8]  = 'W';
    header_buf[9]  = 'A';
    header_buf[10] = 'V';
    header_buf[11] = 'E';

    /* fmt chunk */
    header_buf[12] = 'f';
    header_buf[13] = 'm';
    header_buf[14] = 't';
    header_buf[15] = ' ';
    header_buf[16] = 16; /* fmt chunk size */
    header_buf[17] = 0;
    header_buf[18] = 0;
    header_buf[19] = 0;
    header_buf[20] = 1;  /* PCM format */
    header_buf[21] = 0;
    header_buf[22] = (char)(channels & 0xFF);
    header_buf[23] = (char)((channels >> 8) & 0xFF);
    header_buf[24] = (char)(sample_rate & 0xFF);
    header_buf[25] = (char)((sample_rate >> 8) & 0xFF);
    header_buf[26] = (char)((sample_rate >> 16) & 0xFF);
    header_buf[27] = (char)((sample_rate >> 24) & 0xFF);
    header_buf[28] = (char)(byte_rate & 0xFF);
    header_buf[29] = (char)((byte_rate >> 8) & 0xFF);
    header_buf[30] = (char)((byte_rate >> 16) & 0xFF);
    header_buf[31] = (char)((byte_rate >> 24) & 0xFF);
    header_buf[32] = (char)(block_align & 0xFF);
    header_buf[33] = (char)((block_align >> 8) & 0xFF);
    header_buf[34] = (char)(bits_per_sample & 0xFF);
    header_buf[35] = (char)((bits_per_sample >> 8) & 0xFF);

    /* data chunk */
    header_buf[36] = 'd';
    header_buf[37] = 'a';
    header_buf[38] = 't';
    header_buf[39] = 'a';
    header_buf[40] = (char)(data_size & 0xFF);
    header_buf[41] = (char)((data_size >> 8) & 0xFF);
    header_buf[42] = (char)((data_size >> 16) & 0xFF);
    header_buf[43] = (char)((data_size >> 24) & 0xFF);
}

/* ---- parse_reply_json — 解析 {"reply":"..."} 并回调 ---- */
static int parse_reply_json(const char* json_text, HttpClient* hc)
{
    cJSON* root;
    cJSON* reply_item;
    const char* reply_text;
    size_t reply_len;
    long http_code = 200;

    if (json_text == NULL || hc == NULL) {
        LOG_ERROR("parse_reply_json: NULL parameter");
        return ERR_HTTP_PARSE;
    }

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        LOG_ERROR("parse_reply_json: cJSON_Parse failed for '%s'", json_text);
        if (hc->on_response_cb != NULL) {
            hc->on_response_cb(502, json_text, strlen(json_text),
                               hc->cb_user_data);
        }
        return ERR_HTTP_PARSE;
    }

    reply_item = cJSON_GetObjectItem(root, "reply");
    if (reply_item == NULL) {
        LOG_WARN("parse_reply_json: no 'reply' field in response");
        reply_text = "";
        reply_len  = 0;
    } else if (cJSON_IsString(reply_item)) {
        reply_text = reply_item->valuestring;
        reply_len  = strlen(reply_text);
        LOG_INFO("parse_reply_json: reply='%s'", reply_text);
    } else {
        LOG_WARN("parse_reply_json: 'reply' field is not a string");
        reply_text = "";
        reply_len  = 0;
    }

    /* 回调通知上层 */
    if (hc->on_response_cb != NULL) {
        hc->on_response_cb((int)http_code, reply_text, reply_len,
                           hc->cb_user_data);
    }

    cJSON_Delete(root);
    return ERR_OK;
}
