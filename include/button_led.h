#ifndef _BUTTON_LED_H_
#define _BUTTON_LED_H_

#include "common_types.h"

/* ===================================================================
 * ButtonLED — GPIO 按键 + LED 驱动模块
 *
 * 通过 Linux sysfs 接口操作 GPIO:
 *   - 按键: 方向 "in", 读取 /sys/class/gpio/gpio<N>/value
 *   - LED:   方向 "out", 写入 /sys/class/gpio/gpio<N>/value
 *
 * 目标平台: Linux ARM aarch64
 * =================================================================== */

/* GPIO 导出路径模板 */
#define GPIO_EXPORT_PATH   "/sys/class/gpio/export"
#define GPIO_UNEXPORT_PATH "/sys/class/gpio/unexport"
#define GPIO_BASE_PATH     "/sys/class/gpio/gpio%d"
#define GPIO_DIRECTION     "/sys/class/gpio/gpio%d/direction"
#define GPIO_VALUE         "/sys/class/gpio/gpio%d/value"

/* GPIO 值常量 */
#define GPIO_VALUE_LOW  "0"
#define GPIO_VALUE_HIGH "1"

/* 按键/LED GPIO 默认引脚 (可根据板级配置覆盖) */
#define BUTTON_LED_DEFAULT_BUTTON_GPIO 17
#define BUTTON_LED_DEFAULT_LED_GPIO    27

/* ButtonLED 句柄 */
struct ButtonLED {
    int            button_gpio;        /* 按键 GPIO 编号 (sysfs) */
    int            led_gpio;           /* LED GPIO 编号 (sysfs) */
    int            last_button_state;  /* 上一次按键状态 (去抖用) */
    ButtonCallback on_press;           /* 按键回调函数指针 */
    void*          cb_user_data;       /* 回调透传数据 */
};

/* ---- API ---- */

/**
 * button_led_init — 初始化按键和 LED GPIO
 *
 * 1. 导出 button_gpio 和 led_gpio (echo N > /sys/class/gpio/export)
 * 2. 设置 button_gpio 方向为 "in"
 * 3. 设置 led_gpio 方向为 "out", 初始值 0 (LED 灭)
 *
 * @param bl          ButtonLED 实例指针
 * @param button_gpio 按键 GPIO 编号
 * @param led_gpio    LED GPIO 编号
 * @return            0 成功, ERR_BUTTON_INIT 失败
 */
int button_led_init(ButtonLED* bl, int button_gpio, int led_gpio);

/**
 * button_led_read_button — 读取按键当前状态
 *
 * 读取 /sys/class/gpio/gpio<N>/value 文件。
 *
 * @param bl  ButtonLED 实例指针
 * @return    0=释放 (低电平), 1=按下 (高电平), <0 ERR_BUTTON_INIT
 */
int button_led_read_button(ButtonLED* bl);

/**
 * button_led_set_led — 设置 LED 开关状态
 *
 * 写入 /sys/class/gpio/gpio<N>/value 文件。
 * LED_BLINK_SLOW/FAST 解释为 LED_ON (闪烁由状态机控制)。
 *
 * @param bl    ButtonLED 实例指针
 * @param state LED 状态 (LED_OFF / LED_ON / LED_BLINK_SLOW / LED_BLINK_FAST)
 * @return      0 成功, <0 ERR_BUTTON_INIT
 */
int button_led_set_led(ButtonLED* bl, LedState state);

/**
 * button_led_register_callback — 注册按键回调
 *
 * @param bl        ButtonLED 实例指针
 * @param cb        回调函数指针 (NULL 表示取消)
 * @param user_data 回调透传数据
 * @return          0 成功
 */
int button_led_register_callback(ButtonLED* bl, ButtonCallback cb,
                                  void* user_data);

/**
 * button_led_deinit — 释放 GPIO 资源
 *
 * 1. 设置 LED 为 off
 * 2. 取消导出 button_gpio 和 led_gpio (echo N > /sys/class/gpio/unexport)
 *
 * @param bl  ButtonLED 实例指针
 * @return    0 成功
 */
int button_led_deinit(ButtonLED* bl);

#endif /* _BUTTON_LED_H_ */
