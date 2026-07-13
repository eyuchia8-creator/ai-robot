/* ===================================================================
 * wifi_manager.c — WiFi 连接状态检测与重连管理实现
 *
 * 目标平台: Linux ARM aarch64
 * 依赖: 系统文件 /sys/class/net/<iface>/operstate
 *       wpa_cli 命令行工具
 * =================================================================== */

#include "wifi_manager.h"
#include "error_codes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* operstate 文件路径缓冲区大小 */
#define WIFI_PATH_BUF_SIZE 128

/* ---- init ---- */
int wifi_manager_init(WiFiManager* wm, const char* iface)
{
    if (wm == NULL) {
        LOG_ERROR("wifi_manager_init: NULL pointer");
        return ERR_WIFI_DISCONNECTED;
    }

    memset(wm, 0, sizeof(*wm));

    if (iface != NULL && iface[0] != '\0') {
        strncpy(wm->iface, iface, sizeof(wm->iface) - 1);
        wm->iface[sizeof(wm->iface) - 1] = '\0';
    } else {
        strncpy(wm->iface, WIFI_DEFAULT_IFACE, sizeof(wm->iface) - 1);
        wm->iface[sizeof(wm->iface) - 1] = '\0';
    }

    wm->is_connected    = 0;
    wm->reconnect_count = 0;

    LOG_DEBUG("wifi_manager_init: iface=%s", wm->iface);
    return ERR_OK;
}

/* ---- check_status ---- */
int wifi_manager_check_status(WiFiManager* wm)
{
    char path[WIFI_PATH_BUF_SIZE];
    char buf[16];
    FILE* fp;

    if (wm == NULL) {
        LOG_ERROR("wifi_manager_check_status: NULL pointer");
        return ERR_WIFI_DISCONNECTED;
    }

    /* 构造 operstate 文件路径 */
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", wm->iface);

    fp = fopen(path, "r");
    if (fp == NULL) {
        LOG_ERROR("wifi_manager_check_status: cannot open %s", path);
        wm->is_connected = 0;
        return ERR_WIFI_DISCONNECTED;
    }

    memset(buf, 0, sizeof(buf));
    if (fgets(buf, (int)sizeof(buf) - 1, fp) == NULL) {
        LOG_ERROR("wifi_manager_check_status: read failed for %s", path);
        fclose(fp);
        wm->is_connected = 0;
        return ERR_WIFI_DISCONNECTED;
    }
    fclose(fp);

    /* 去除末尾换行符 */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    if (strcmp(buf, WIFI_OPERSTATE_UP) == 0) {
        wm->is_connected = 1;
        LOG_DEBUG("wifi_manager_check_status: %s is UP", wm->iface);
        return ERR_OK;
    }

    wm->is_connected = 0;
    LOG_DEBUG("wifi_manager_check_status: %s is DOWN (state=%s)",
              wm->iface, buf);
    return ERR_WIFI_DISCONNECTED;
}

/* ---- wait_for_connection ---- */
int wifi_manager_wait_for_connection(WiFiManager* wm, int timeout_sec)
{
    int elapsed;
    int rc;

    if (wm == NULL) {
        LOG_ERROR("wifi_manager_wait_for_connection: NULL pointer");
        return ERR_WIFI_TIMEOUT;
    }

    if (timeout_sec <= 0) {
        timeout_sec = 30; /* 默认 30 秒 */
    }

    LOG_INFO("wifi_manager_wait_for_connection: waiting up to %ds for %s",
             timeout_sec, wm->iface);

    for (elapsed = 0; elapsed < timeout_sec; elapsed++) {
        rc = wifi_manager_check_status(wm);
        if (rc == ERR_OK && wm->is_connected) {
            LOG_INFO("wifi_manager_wait_for_connection: connected after %ds",
                     elapsed);
            return ERR_OK;
        }
        sleep(1);
    }

    LOG_ERROR("wifi_manager_wait_for_connection: timeout after %ds",
              timeout_sec);
    return ERR_WIFI_TIMEOUT;
}

/* ---- reconnect ---- */
int wifi_manager_reconnect(WiFiManager* wm)
{
    char cmd[128];
    int rc;

    if (wm == NULL) {
        LOG_ERROR("wifi_manager_reconnect: NULL pointer");
        return ERR_WIFI_DISCONNECTED;
    }

    wm->reconnect_count++;

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s reconfigure", wm->iface);

    LOG_INFO("wifi_manager_reconnect: attempt #%d, cmd='%s'",
             wm->reconnect_count, cmd);

    rc = system(cmd);
    if (rc != 0) {
        LOG_WARN("wifi_manager_reconnect: wpa_cli returned %d", rc);
        /* wpa_cli 返回非 0 不一定是致命错误，可能是已连接状态 */
    }

    /* 检测重连后的状态 */
    rc = wifi_manager_check_status(wm);
    if (rc != ERR_OK) {
        LOG_WARN("wifi_manager_reconnect: still disconnected after reconfigure");
        return ERR_WIFI_DISCONNECTED;
    }

    return ERR_OK;
}

/* ---- deinit ---- */
int wifi_manager_deinit(WiFiManager* wm)
{
    if (wm == NULL) {
        return ERR_OK;
    }

    memset(wm->iface, 0, sizeof(wm->iface));
    wm->is_connected    = 0;
    wm->reconnect_count = 0;

    return ERR_OK;
}
