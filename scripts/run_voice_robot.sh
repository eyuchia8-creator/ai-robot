#!/bin/sh
# ===================================================================
# run_voice_robot.sh — 应用启动脚本
#
# 用法:  chmod +x run_voice_robot.sh && ./run_voice_robot.sh
#
# 功能:
#   1. 设置运行时环境变量 (LD_LIBRARY_PATH 等)
#   2. 执行 voice_robot 主程序
# ===================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"

echo "============================================"
echo "  RK3506 Voice Robot — Starting..."
echo "============================================"

# --- 环境变量 ---
# 将项目 libs 目录加入动态库搜索路径（如用到自带 .so）
export LD_LIBRARY_PATH="${PROJECT_DIR}/libs:${LD_LIBRARY_PATH}"

# 切换到项目根目录，使程序能找到 config/、resources/ 等
cd "${PROJECT_DIR}"

# --- 启动应用 ---
echo "Runtime dir: ${PROJECT_DIR}"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"
echo ""

exec ./voice_robot
