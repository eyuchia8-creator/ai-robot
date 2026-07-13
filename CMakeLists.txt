cmake_minimum_required(VERSION 3.16)
project(rk3506_voice_robot LANGUAGES C)

# -------------------------------------------------------------------
# 交叉编译工具链 — 可通过 -DTOOLCHAIN_PATH 覆盖
# -------------------------------------------------------------------
if(DEFINED TOOLCHAIN_PATH)
    SET(CMAKE_C_COMPILER "${TOOLCHAIN_PATH}/bin/aarch64-linux-gnu-gcc")
    SET(CMAKE_SYSROOT "${TOOLCHAIN_PATH}/aarch64-linux-gnu/libc")
    SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
else()
    SET(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif()

# -------------------------------------------------------------------
# C 标准与编译选项
# -------------------------------------------------------------------
SET(CMAKE_C_STANDARD 11)
SET(CMAKE_C_STANDARD_REQUIRED ON)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -std=c11")

# -------------------------------------------------------------------
# 查找第三方依赖
# -------------------------------------------------------------------
FIND_PACKAGE(ALSA REQUIRED)
FIND_PACKAGE(CURL REQUIRED)
FIND_PACKAGE(Freetype REQUIRED)
FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(LIBCONFIG REQUIRED libconfig)

# -------------------------------------------------------------------
# include 路径
# -------------------------------------------------------------------
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${ALSA_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${LIBCONFIG_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/libs/cJSON
)

# -------------------------------------------------------------------
# cJSON 源码 (OBJECT 库，直接链接入主程序)
# -------------------------------------------------------------------
IF(EXISTS "${CMAKE_SOURCE_DIR}/libs/cJSON/cJSON.c")
    ADD_LIBRARY(cjson_obj OBJECT libs/cJSON/cJSON.c)
    TARGET_INCLUDE_DIRECTORIES(cjson_obj PRIVATE ${CMAKE_SOURCE_DIR}/libs/cJSON)
ENDIF()

# -------------------------------------------------------------------
# 主程序 target
# -------------------------------------------------------------------
SET(SOURCES
    src/main.c
    src/app_config.c
    src/audio_capture.c
    src/wifi_manager.c
    src/http_client.c
    src/display.c
    src/font_renderer.c
    src/state_machine.c
    src/button_led.c
)

ADD_EXECUTABLE(voice_robot ${SOURCES})

TARGET_LINK_LIBRARIES(voice_robot
    ${ALSA_LIBRARIES}
    ${CURL_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${LIBCONFIG_LIBRARIES}
    pthread
    m
)

IF(TARGET cjson_obj)
    TARGET_LINK_LIBRARIES(voice_robot cjson_obj)
ENDIF()

# -------------------------------------------------------------------
# 安装规则（可选）
# -------------------------------------------------------------------
INSTALL(TARGETS voice_robot DESTINATION bin)
