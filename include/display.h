#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdint.h>

/* ===================================================================
 * 显示模块 — Framebuffer 操作 + FreeType 文本渲染
 *
 * 依赖: Linux framebuffer (/dev/fb0), font_renderer 模块
 * 支持像素格式: 32bpp (ARGB/XRGB), 16bpp (RGB565)
 * =================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
 * 预定义颜色 (ARGB 格式: 0xAARRGGBB)
 * ------------------------------------------------------------------- */
#define COLOR_BLACK    0x00000000
#define COLOR_WHITE    0x00FFFFFF
#define COLOR_RED      0x00FF0000
#define COLOR_GREEN    0x0000FF00
#define COLOR_BLUE     0x000000FF
#define COLOR_GRAY     0x00808080

/* -------------------------------------------------------------------
 * GlyphBitmap — 字形渲染结果 (font_renderer 产出, display 消费)
 *
 * buffer 指向 FreeType 内部数据，调用者不可 free。
 * 在下一次 font_renderer_render_char() 调用后 buffer 内容可能被覆盖。
 * ------------------------------------------------------------------- */
typedef struct {
    unsigned char* buffer;    /* 灰度位图 buffer (0-255, 0=透明 255=不透明) */
    int            width;     /* 位图宽 (像素) */
    int            height;    /* 位图高 (像素) */
    int            left;      /* 左边距 bearing_x (像素) */
    int            top;       /* 上边距 bearing_y (像素, 从基线向上) */
    int            advance_x; /* 水平步进 (像素, 用于光标前进) */
} GlyphBitmap;

/* -------------------------------------------------------------------
 * Display — 显示设备句柄
 * ------------------------------------------------------------------- */
typedef struct Display {
    int      fb_fd;         /* framebuffer 文件描述符 */
    void*    fb_mmap;       /* mmap 映射的帧缓冲指针 */
    int      fb_width;      /* 屏幕宽度 (像素) */
    int      fb_height;     /* 屏幕高度 (像素) */
    int      fb_bpp;        /* 每像素位深 (bits per pixel), 16 或 32 */
    int      fb_stride;     /* 每行字节数 (line_length) */
    uint32_t bg_color;      /* 背景色 (ARGB) */
    uint32_t fg_color;      /* 前景色 (ARGB) */
    void*    font;          /* FontRenderer* 句柄 */
    int      cursor_x;      /* 当前光标位置 X (像素) */
    int      cursor_y;      /* 当前光标位置 Y (像素, 基线位置) */
} Display;

/* -------------------------------------------------------------------
 * API 声明
 * ------------------------------------------------------------------- */

/**
 * 初始化显示模块
 *
 * 1. 打开 framebuffer 设备
 * 2. ioctl 获取屏幕参数 (宽/高/bpp/stride)
 * 3. mmap 映射帧缓冲到用户空间
 * 4. 初始化 FreeType 字体渲染器
 * 5. 设置默认颜色 (白底黑字), 光标归零
 *
 * @param d            Display 结构体指针 (调用者分配)
 * @param fb_device    framebuffer 设备路径 (如 "/dev/fb0")
 * @param font_path    字体文件路径 (如 "./resources/NotoSansSC-Regular.ttf")
 * @param font_size_pt 字号 (磅)
 * @return ERR_OK (0) 成功, 负数错误码 (ERR_DISPLAY_OPEN/ERR_DISPLAY_MMAP/ERR_FONT_LOAD)
 */
int display_init(Display* d, const char* fb_device,
                 const char* font_path, int font_size_pt);

/**
 * 清屏 — 整屏填充背景色
 *
 * @param d Display 句柄
 * @return ERR_OK (0) 成功
 */
int display_clear(Display* d);

/**
 * 在当前光标位置绘制 UTF-8 文本
 *
 * 自动处理:
 * - 换行符 '\n' → 光标移至下行首
 * - 行宽超出屏幕宽度 → 自动换行
 * - 行高超出一屏 → 停止绘制 (不再向下滚动)
 *
 * @param d         Display 句柄
 * @param utf8_text UTF-8 编码文本 (以 '\0' 结尾)
 * @return ERR_OK (0) 成功, 负数错误码
 */
int display_draw_text(Display* d, const char* utf8_text);

/**
 * 在指定位置绘制 UTF-8 文本 (不影响持久光标位置)
 *
 * @param d         Display 句柄
 * @param utf8_text UTF-8 编码文本
 * @param x         起始 X 坐标 (像素)
 * @param y         起始 Y 坐标 (像素, 基线)
 * @return ERR_OK (0) 成功, 负数错误码
 */
int display_draw_text_at(Display* d, const char* utf8_text, int x, int y);

/**
 * 设置光标位置
 *
 * @param d Display 句柄
 * @param x 新 X 坐标 (像素)
 * @param y 新 Y 坐标 (像素, 基线)
 * @return ERR_OK (0) 成功
 */
int display_set_cursor(Display* d, int x, int y);

/**
 * 设置前景色 / 背景色
 *
 * @param d  Display 句柄
 * @param bg 背景色 (ARGB 格式)
 * @param fg 前景色 (ARGB 格式)
 * @return ERR_OK (0) 成功
 */
int display_set_color(Display* d, uint32_t bg, uint32_t fg);

/**
 * 绘制填充矩形
 *
 * @param d     Display 句柄
 * @param x     矩形左上角 X
 * @param y     矩形左上角 Y
 * @param w     矩形宽度 (像素)
 * @param h     矩形高度 (像素)
 * @param color 填充颜色 (ARGB 格式)
 * @return ERR_OK (0) 成功
 */
int display_draw_rect(Display* d, int x, int y, int w, int h, uint32_t color);

/**
 * 整屏填充指定颜色 (不修改 bg_color)
 *
 * @param d     Display 句柄
 * @param color 填充颜色 (ARGB 格式)
 * @return ERR_OK (0) 成功
 */
int display_fill_screen(Display* d, uint32_t color);

/**
 * 释放显示模块资源
 *
 * - 释放 FreeType 字体渲染器
 * - munmap 帧缓冲
 * - 关闭 framebuffer 文件描述符
 *
 * @param d Display 句柄
 * @return ERR_OK (0) 成功
 */
int display_deinit(Display* d);

#ifdef __cplusplus
}
#endif

#endif /* _DISPLAY_H_ */
