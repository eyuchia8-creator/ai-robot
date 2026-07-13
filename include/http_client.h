#ifndef _HTTP_CLIENT_H_
#define _HTTP_CLIENT_H_

#include "common_types.h"

/* ===================================================================
 * HttpClient — 基于 libcurl 的 HTTP 客户端
 *
 * 支持 multipart/form-data 文件上传、响应 JSON 解析。
 * 内部使用 cJSON 解析 AI 服务器返回的 {"reply":"..."} 格式。
 * =================================================================== */

/* 默认值 */
#define HTTP_DEFAULT_TIMEOUT_SEC 10
#define HTTP_SERVER_URL_MAX      256
#define HTTP_RESPONSE_BUF_SIZE   4096

/* WAV header 大小 */
#define WAV_HEADER_SIZE 44

/* HTTP 客户端句柄 */
struct HttpClient {
    char   server_url[HTTP_SERVER_URL_MAX];  /* AI 服务器地址 */
    int    timeout_sec;                       /* 请求超时 (秒) */
    void*  curl;                              /* CURL* 句柄 */
    ResponseCallback on_response_cb;          /* 响应回调 */
    void*  cb_user_data;                      /* 回调透传数据 */
};

/* ---- API ---- */

/**
 * http_client_init — 初始化 HttpClient
 *
 * 创建 CURL 句柄，设置基础选项 (超时等)。
 *
 * @param hc          HttpClient 实例指针
 * @param server_url  AI 服务器 URL，如 "http://47.81.10.119/"
 * @param timeout_sec 请求超时秒数，0 使用默认 10s
 * @return            0 成功, <0 错误码
 */
int http_client_init(HttpClient* hc, const char* server_url, int timeout_sec);

/**
 * http_client_upload_audio — 上传音频数据到 AI 服务器
 *
 * 1. 若数据为纯 PCM (不含 WAV header), 自动拼接 44 字节 WAV header
 * 2. 使用 multipart/form-data (字段名 "audio") POST 上传
 * 3. 响应 JSON 由 cJSON 解析，提取 "reply" 字段
 * 4. 通过 on_response_cb 回调通知上层
 *
 * @param hc       HttpClient 实例指针
 * @param wav_data PCM 或 WAV 数据指针
 * @param wav_size 数据字节数
 * @return         0 成功, ERR_HTTP_* 失败
 */
int http_client_upload_audio(HttpClient* hc, const char* wav_data,
                              size_t wav_size);

/**
 * http_client_set_callback — 设置响应回调
 *
 * @param hc        HttpClient 实例指针
 * @param cb        响应回调函数指针
 * @param user_data 回调透传数据
 * @return          0 成功
 */
int http_client_set_callback(HttpClient* hc, ResponseCallback cb,
                              void* user_data);

/**
 * http_client_deinit — 释放 HttpClient 资源
 *
 * 销毁 CURL 句柄。
 *
 * @param hc  HttpClient 实例指针
 * @return    0 成功
 */
int http_client_deinit(HttpClient* hc);

#endif /* _HTTP_CLIENT_H_ */
