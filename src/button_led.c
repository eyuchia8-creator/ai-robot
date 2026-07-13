/* ===================================================================
 * button_led.c — GPIO 按键 + LED 驱动实现
 *
 * 通过 Linux sysfs 接口操作 GPIO:
 *   1. 导出/取消导出 GPIO 引脚
 *   2. 按键: 方向 "in", 读取 value 文件
 *   3. LED:   方向 "out", 写入 value 文件
 *
 * 目标平台: Linux ARM aarch64
 * =================================================================== */

#include "button_led.h"
#include "error_codes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- 内部辅助函数声明 ---- */

/**
 * gpio_export — 导出一个 GPIO 引脚
 *
 * @param gpio_num GPIO 编号
 * @return         0 成功, <0 失败
 */
static int gpio_export(int gpio_num);

/**
 * gpio_unexport — 取消导出一个 GPIO 引脚
 *
 * @param gpio_num GPIO 编号
 * @return         0 成功, <0 失败
 */
static int gpio_unexport(int gpio_num);

/**
 * gpio_set_direction — 设置 GPIO 方向
 *
 * @param gpio_num  GPIO 编号
 * @param direction "in" 或 "out"
 * @return          0 成功, <0 失败
 */
static int gpio_set_direction(int gpio_num, const char* direction);

/**
 * gpio_write_value — 写入 GPIO 值 (0 或 1)
 *
 * @param gpio_num GPIO 编号
 * @param value    0 或 1
 * @return         0 成功, <0 失败
 */
static int gpio_write_value(int gpio_num, int value);

/**
 * gpio_read_value — 读取 GPIO 值
 *
 * @param gpio_num GPIO 编号
 * @return         0=低电平, 1=高电平, <0 错误
 */
static int gpio_read_value(int gpio_num);

/* ---- init ---- */
int button_led_init(ButtonLED* bl, int button_gpio, int led_gpio)
{
    int rc;

    if (bl == NULL) {
        LOG_ERROR("button_led_init: NULL pointer");
        return ERR_BUTTON_INIT;
    }

    memset(bl, 0, sizeof(*bl));

    /* 使用传入的 GPIO 号，0 表示使用默认值 */
    bl->button_gpio = (button_gpio > 0) ? button_gpio
                                        : BUTTON_LED_DEFAULT_BUTTON_GPIO;
    bl->led_gpio    = (led_gpio > 0) ? led_gpio
                                     : BUTTON_LED_DEFAULT_LED_GPIO;

    /* 导出按键 GPIO */
    rc = gpio_export(bl->button_gpio);
    if (rc != 0) {
        LOG_ERROR("button_led_init: failed to export button GPIO %d",
                  bl->button_gpio);
        return ERR_BUTTON_INIT;
    }

    /* 设置按键方向为 in */
    rc = gpio_set_direction(bl->button_gpio, "in");
    if (rc != 0) {
        LOG_ERROR("button_led_init: failed to set button GPIO %d direction",
                  bl->button_gpio);
        gpio_unexport(bl->button_gpio);
        return ERR_BUTTON_INIT;
    }

    /* 导出 LED GPIO */
    rc = gpio_export(bl->led_gpio);
    if (rc != 0) {
        LOG_WARN("button_led_init: failed to export LED GPIO %d (non-fatal)",
                 bl->led_gpio);
        /* LED 导出失败不致命 — 无 LED 也能运行 */
    } else {
        /* 设置 LED 方向为 out */
        rc = gpio_set_direction(bl->led_gpio, "out");
        if (rc != 0) {
            LOG_WARN("button_led_init: failed to set LED GPIO %d direction",
                     bl->led_gpio);
        } else {
            /* 初始化为灭 */
            gpio_write_value(bl->led_gpio, 0);
        }
    }

    bl->last_button_state = 0;
    bl->on_press          = NULL;
    bl->cb_user_data      = NULL;

    LOG_INFO("button_led_init: button_gpio=%d led_gpio=%d",
             bl->button_gpio, bl->led_gpio);
    return ERR_OK;
}

/* ---- read_button ---- */
int button_led_read_button(ButtonLED* bl)
{
    int value;

    if (bl == NULL) {
        LOG_ERROR("button_led_read_button: NULL pointer");
        return ERR_BUTTON_INIT;
    }

    value = gpio_read_value(bl->button_gpio);
    if (value < 0) {
        LOG_ERROR("button_led_read_button: gpio_read_value failed for GPIO %d",
                  bl->button_gpio);
        return ERR_BUTTON_INIT;
    }

    /* 检测边沿变化并触发回调 */
    if (value != bl->last_button_state) {
        bl->last_button_state = value;

        if (bl->on_press != NULL) {
            bl->on_press(value, bl->cb_user_data);
        }

        LOG_DEBUG("button_led_read_button: GPIO %d state=%d (changed)",
                  bl->button_gpio, value);
    }

    return value; /* 0=释放, 1=按下 */
}

/* ---- set_led ---- */
int button_led_set_led(ButtonLED* bl, LedState state)
{
    int value;

    if (bl == NULL) {
        LOG_ERROR("button_led_set_led: NULL pointer");
        return ERR_BUTTON_INIT;
    }

    /* LED_BLINK_SLOW/FAST 由状态机定时器控制切换，此处统一设为 ON */
    switch (state) {
    case LED_OFF:
        value = 0;
        break;
    case LED_ON:
    case LED_BLINK_SLOW:
    case LED_BLINK_FAST:
        value = 1;
        break;
    default:
        value = 0;
        break;
    }

    if (gpio_write_value(bl->led_gpio, value) != 0) {
        LOG_WARN("button_led_set_led: gpio_write_value failed for LED GPIO %d",
                 bl->led_gpio);
        return ERR_BUTTON_INIT;
    }

    LOG_DEBUG("button_led_set_led: GPIO %d value=%d state=%d",
              bl->led_gpio, value, (int)state);
    return ERR_OK;
}

/* ---- register_callback ---- */
int button_led_register_callback(ButtonLED* bl, ButtonCallback cb,
                                  void* user_data)
{
    if (bl == NULL) {
        LOG_ERROR("button_led_register_callback: NULL pointer");
        return ERR_BUTTON_INIT;
    }

    bl->on_press     = cb;
    bl->cb_user_data = user_data;

    LOG_DEBUG("button_led_register_callback: callback registered");
    return ERR_OK;
}

/* ---- deinit ---- */
int button_led_deinit(ButtonLED* bl)
{
    if (bl == NULL) {
        return ERR_OK;
    }

    /* 关闭 LED */
    (void)gpio_write_value(bl->led_gpio, 0);

    /* 取消导出 GPIO */
    if (bl->button_gpio > 0) {
        (void)gpio_unexport(bl->button_gpio);
    }
    if (bl->led_gpio > 0) {
        (void)gpio_unexport(bl->led_gpio);
    }

    bl->button_gpio      = 0;
    bl->led_gpio         = 0;
    bl->last_button_state = 0;
    bl->on_press          = NULL;
    bl->cb_user_data      = NULL;

    LOG_INFO("button_led_deinit: GPIOs released");
    return ERR_OK;
}

/* ===================================================================
 * 内部辅助函数 — GPIO sysfs 底层操作
 * =================================================================== */

/* ---- gpio_export ---- */
static int gpio_export(int gpio_num)
{
    FILE* fp;
    char  path[64];

    /* 检查是否已经导出 (检查 gpio<N> 目录是否存在) */
    snprintf(path, sizeof(path), GPIO_BASE_PATH, gpio_num);

    /* 尝试打开 export 文件写入 GPIO 编号 */
    fp = fopen(GPIO_EXPORT_PATH, "w");
    if (fp == NULL) {
        /* 可能已经导出，不算错误 */
        LOG_DEBUG("gpio_export: GPIO %d may already be exported", gpio_num);
        return ERR_OK;
    }

    fprintf(fp, "%d", gpio_num);
    fclose(fp);

    /* 给内核一点时间创建 sysfs 节点 */
    usleep(100000); /* 100ms */

    LOG_DEBUG("gpio_export: GPIO %d exported", gpio_num);
    return ERR_OK;
}

/* ---- gpio_unexport ---- */
static int gpio_unexport(int gpio_num)
{
    FILE* fp;

    fp = fopen(GPIO_UNEXPORT_PATH, "w");
    if (fp == NULL) {
        LOG_WARN("gpio_unexport: failed to open unexport for GPIO %d", gpio_num);
        return ERR_BUTTON_INIT;
    }

    fprintf(fp, "%d", gpio_num);
    fclose(fp);

    LOG_DEBUG("gpio_unexport: GPIO %d unexported", gpio_num);
    return ERR_OK;
}

/* ---- gpio_set_direction ---- */
static int gpio_set_direction(int gpio_num, const char* direction)
{
    char  path[64];
    FILE* fp;

    snprintf(path, sizeof(path), GPIO_DIRECTION, gpio_num);

    fp = fopen(path, "w");
    if (fp == NULL) {
        LOG_ERROR("gpio_set_direction: failed to open %s", path);
        return ERR_BUTTON_INIT;
    }

    fprintf(fp, "%s", direction);
    fclose(fp);

    LOG_DEBUG("gpio_set_direction: GPIO %d set to %s", gpio_num, direction);
    return ERR_OK;
}

/* ---- gpio_write_value ---- */
static int gpio_write_value(int gpio_num, int value)
{
    char  path[64];
    FILE* fp;

    snprintf(path, sizeof(path), GPIO_VALUE, gpio_num);

    fp = fopen(path, "w");
    if (fp == NULL) {
        LOG_ERROR("gpio_write_value: failed to open %s", path);
        return ERR_BUTTON_INIT;
    }

    fprintf(fp, "%d", value);
    fclose(fp);

    return ERR_OK;
}

/* ---- gpio_read_value ---- */
static int gpio_read_value(int gpio_num)
{
    char  path[64];
    char  buf[8];
    FILE* fp;

    snprintf(path, sizeof(path), GPIO_VALUE, gpio_num);

    fp = fopen(path, "r");
    if (fp == NULL) {
        LOG_ERROR("gpio_read_value: failed to open %s", path);
        return ERR_BUTTON_INIT;
    }

    memset(buf, 0, sizeof(buf));
    if (fgets(buf, (int)sizeof(buf) - 1, fp) == NULL) {
        LOG_ERROR("gpio_read_value: failed to read %s", path);
        fclose(fp);
        return ERR_BUTTON_INIT;
    }
    fclose(fp);

    /* 去除末尾换行符 */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    return (buf[0] == '1') ? 1 : 0;
}
