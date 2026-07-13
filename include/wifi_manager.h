#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include "common_types.h"

/* ===================================================================
 * WiFiManager — WiFi 连接状态检测与重连管理
 *
 * 通过读取 /sys/class/net/<iface>/operstate 判断连接状态，
 * 避免 fork shell 进程，开销极低。
 * =================================================================== */

/* 默认网络接口名 */
#define WIFI_DEFAULT_IFACE "wlan0"

/* operstate 文件内容 */
#define WIFI_OPERSTATE_UP   "up"
#define WIFI_OPERSTATE_DOWN "down"

/* WiFi 管理句柄 */
struct WiFiManager {
    char iface[16];          /* 网络接口名, 默认 "wlan0" */
    int  is_connected;       /* 0=断开, 1=已连接 */
    int  reconnect_count;    /* 重连次数累计 */
};

/* ---- API ---- */

/**
 * wifi_manager_init — 初始化 WiFiManager
 *
 * @param wm    WiFiManager 实例指针
 * @param iface 网络接口名，NULL 表示使用默认 "wlan0"
 * @return      0 成功, <0 错误码
 */
int wifi_manager_init(WiFiManager* wm, const char* iface);

/**
 * wifi_manager_check_status — 检测当前 WiFi 连接状态
 *
 * 读取 /sys/class/net/<iface>/operstate 文件。
 *
 * @param wm  WiFiManager 实例指针
 * @return    0 成功 (status 写入 wm->is_connected), <0 ERR_WIFI_DISCONNECTED
 */
int wifi_manager_check_status(WiFiManager* wm);

/**
 * wifi_manager_wait_for_connection — 阻塞等待 WiFi 连接就绪
 *
 * 轮询 check_status() + sleep(1)，直到连接成功或超时。
 *
 * @param wm          WiFiManager 实例指针
 * @param timeout_sec 超时秒数
 * @return            0 已连接, ERR_WIFI_TIMEOUT 超时
 */
int wifi_manager_wait_for_connection(WiFiManager* wm, int timeout_sec);

/**
 * wifi_manager_reconnect — 触发 wpa_supplicant 重连
 *
 * 通过 system("wpa_cli -i <iface> reconfigure") 触发。
 *
 * @param wm  WiFiManager 实例指针
 * @return    0 成功 (命令已发送), <0 错误码
 */
int wifi_manager_reconnect(WiFiManager* wm);

/**
 * wifi_manager_deinit — 释放资源
 *
 * @param wm  WiFiManager 实例指针
 * @return    0 成功
 */
int wifi_manager_deinit(WiFiManager* wm);

#endif /* _WIFI_MANAGER_H_ */
