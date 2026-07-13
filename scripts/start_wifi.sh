#!/bin/sh
# ===================================================================
# start_wifi.sh — WiFi 初始化脚本
#
# 用法:  chmod +x start_wifi.sh && ./start_wifi.sh
#
# 功能:
#   1. 杀掉已有的 wpa_supplicant / dhclient 进程
#   2. 启动 wpa_supplicant 连接 AP
#   3. 通过 dhclient 获取 IP 地址
#
# 注意: 运行前请确保 config/app_config.ini 中的 WiFi SSID/PSK
#       已正确填入 wpa_supplicant.conf 模板。
# ===================================================================

WIFI_IFACE="${1:-wlan0}"
WPA_CONF="${2:-/etc/wpa_supplicant/wpa_supplicant.conf}"

set -e

echo "[WiFi] Initializing WiFi on interface ${WIFI_IFACE} ..."

# 1. 清理旧实例
echo "[WiFi] Killing old wpa_supplicant / dhclient processes ..."
pkill -f "wpa_supplicant.*${WIFI_IFACE}" 2>/dev/null || true
pkill -f "dhclient.*${WIFI_IFACE}"       2>/dev/null || true
sleep 1

# 2. 启动 wpa_supplicant
echo "[WiFi] Starting wpa_supplicant ..."
wpa_supplicant -B -i "${WIFI_IFACE}" -c "${WPA_CONF}"

# 3. 等待关联完成
echo "[WiFi] Waiting for association ..."
for _ in 1 2 3 4 5 6 7 8 9 10; do
    STATUS=$(wpa_cli -i "${WIFI_IFACE}" status 2>/dev/null | grep 'wpa_state' | cut -d= -f2)
    if [ "${STATUS}" = "COMPLETED" ]; then
        echo "[WiFi] Association completed."
        break
    fi
    sleep 1
done

# 4. 获取 IP
echo "[WiFi] Requesting IP via dhclient ..."
dhclient "${WIFI_IFACE}"

echo "[WiFi] WiFi initialization done."
