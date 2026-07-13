/* ===================================================================
 * display.c — Framebuffer 显示模块实现
 *
 * 功能:
 *   - Framebuffer 设备打开 / mmap 映射 / 释放
 *   - 像素写入 (32bpp ARGB / 16bpp RGB565)
 *   - 颜色管理 (前景色/背景色)
 *   - 文本渲染 (基于 GlyphBitmap + 自动换行)
 *   - 矩形填充 / 整屏填充
 *
 * 平台: Linux ARM aarch64 (RK3506)
 * =================================================================== */

#include "display.h"
#include "font_renderer.h"
#include "common_types.h"
#include "error_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

/* ===================================================================
 * 内部辅助函数
 * =================================================================== */

/**
 * 将 32-bit ARGB 颜色转换为 16-bit RGB565
 *
 * ARGB: AAAAAAAA RRRRRRRR GGGGGGGG BBBBBBBB
 * RGB565:              RRRRR GGGGGG BBBBB  (5+6+5 bits)
 */
static uint16_t argb_to_rgb565(uint32_t argb) {
    uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((argb >> 8)  & 0xFF);
    uint8_t b = (uint8_t)(argb         & 0xFF);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * 向 framebuffer 写入单个像素
 *
 * 自动根据 bpp 选择 32-bit 或 16-bit 写入路径。
 * 不做边界检查 — 调用者需确保 (x, y) 在屏幕范围内。
 */
static void display_write_pixel(const Display* d, int x, int y, uint32_t color) {
    if (d->fb_bpp == 32) {
        uint32_t* fb = (uint32_t*)d->fb_mmap;
        fb[y * (d->fb_stride / 4) + x] = color;
    } else if (d->fb_bpp == 16) {
        uint16_t* fb = (uint16_t*)d->fb_mmap;
        fb[y * (d->fb_stride / 2) + x] = argb_to_rgb565(color);
    }
}

/**
 * 在当前位置绘制单个字形位图
 *
 * 使用硬阈值 alpha 混合:
 *   - glyph_value > 128 → 写前景色
 *   - glyph_value ≤ 128 → 跳过 (保留背景)
 *
 * 绘制位置:
 *   screen_x = cursor_x + glyph->left
 *   screen_y = cursor_y - glyph->top
 *
 * 超出屏幕边界的像素自动裁剪。
 */
static void display_draw_glyph(const Display* d, const GlyphBitmap* glyph) {
    int start_x = d->cursor_x + glyph->left;
    int start_y = d->cursor_y - glyph->top;
    int row, col;

    for (row = 0; row < glyph->height; row++) {
        int screen_y = start_y + row;
        if (screen_y < 0 || screen_y >= d->fb_height) {
            continue;
        }

        for (col = 0; col < glyph->width; col++) {
            int screen_x = start_x + col;
            if (screen_x < 0 || screen_x >= d->fb_width) {
                continue;
            }

            /* 硬阈值 alpha: 灰度值 > 128 视为前景 */
            unsigned char glyph_val = glyph->buffer[row * glyph->width + col];
            if (glyph_val > 128) {
                display_write_pixel(d, screen_x, screen_y, d->fg_color);
            }
        }
    }
}

/* ===================================================================
 * 公开 API 实现
 * =================================================================== */

int display_init(Display* d, const char* fb_device,
                 const char* font_path, int font_size_pt) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    FontRenderer* fr = NULL;
    int ret;

    if (d == NULL || fb_device == NULL || font_path == NULL) {
        LOG_ERROR("display_init: NULL parameter");
        return ERR_DISPLAY_OPEN;
    }

    /* 零初始化 Display 结构体 */
    memset(d, 0, sizeof(Display));

    /* 1. 打开 framebuffer 设备 */
    d->fb_fd = open(fb_device, O_RDWR);
    if (d->fb_fd < 0) {
        LOG_ERROR("display_init: cannot open %s", fb_device);
        return ERR_DISPLAY_OPEN;
    }
    LOG_INFO("display_init: opened %s (fd=%d)", fb_device, d->fb_fd);

    /* 2. 获取可变屏幕信息 (宽/高/bpp) */
    if (ioctl(d->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        LOG_ERROR("display_init: FBIOGET_VSCREENINFO failed");
        close(d->fb_fd);
        d->fb_fd = -1;
        return ERR_DISPLAY_OPEN;
    }
    d->fb_width  = vinfo.xres;
    d->fb_height = vinfo.yres;
    d->fb_bpp    = vinfo.bits_per_pixel;

    LOG_INFO("display_init: resolution %dx%d, bpp=%d",
             d->fb_width, d->fb_height, d->fb_bpp);

    /* 验证 bpp 支持 */
    if (d->fb_bpp != 32 && d->fb_bpp != 16) {
        LOG_ERROR("display_init: unsupported bpp=%d (only 16/32 supported)", d->fb_bpp);
        close(d->fb_fd);
        d->fb_fd = -1;
        return ERR_DISPLAY_OPEN;
    }

    /* 3. 获取固定屏幕信息 (stride) */
    if (ioctl(d->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        LOG_ERROR("display_init: FBIOGET_FSCREENINFO failed");
        close(d->fb_fd);
        d->fb_fd = -1;
        return ERR_DISPLAY_OPEN;
    }
    d->fb_stride = finfo.line_length;

    LOG_DEBUG("display_init: stride=%d bytes, smem_len=%u",
              d->fb_stride, finfo.smem_len);

    /* 4. mmap 映射帧缓冲 */
    d->fb_mmap = mmap(NULL, finfo.smem_len,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, d->fb_fd, 0);
    if (d->fb_mmap == MAP_FAILED) {
        LOG_ERROR("display_init: mmap failed");
        close(d->fb_fd);
        d->fb_fd = -1;
        return ERR_DISPLAY_MMAP;
    }

    LOG_INFO("display_init: mmap framebuffer at %p, size=%u",
             d->fb_mmap, finfo.smem_len);

    /* 5. 初始化 FreeType 字体渲染器 */
    fr = (FontRenderer*)malloc(sizeof(FontRenderer));
    if (fr == NULL) {
        LOG_ERROR("display_init: malloc FontRenderer failed");
        munmap(d->fb_mmap, finfo.smem_len);
        close(d->fb_fd);
        d->fb_fd = -1;
        return ERR_MEMORY_ALLOC;
    }

    ret = font_renderer_init(fr, font_path, font_size_pt);
    if (ret != ERR_OK) {
        LOG_ERROR("display_init: font_renderer_init failed (ret=%d)", ret);
        free(fr);
        munmap(d->fb_mmap, finfo.smem_len);
        close(d->fb_fd);
        d->fb_fd = -1;
        return ret;
    }

    d->font = (void*)fr;

    /* 6. 设置默认颜色: 白底黑字 */
    d->bg_color = COLOR_WHITE;
    d->fg_color = COLOR_BLACK;

    /* 7. 光标归零 */
    d->cursor_x = 0;
    d->cursor_y = font_renderer_get_line_height(fr);

    LOG_INFO("display_init: success, font '%s' size=%dpt",
             font_path, font_size_pt);
    return ERR_OK;
}

int display_clear(Display* d) {
    if (d == NULL) {
        return ERR_DISPLAY_OPEN;
    }
    return display_fill_screen(d, d->bg_color);
}

int display_draw_text(Display* d, const char* utf8_text) {
    FontRenderer* fr;
    const char* p;
    int line_height;

    if (d == NULL || utf8_text == NULL) {
        return ERR_DISPLAY_OPEN;
    }

    fr = (FontRenderer*)d->font;
    if (fr == NULL) {
        return ERR_FONT_LOAD;
    }

    line_height = font_renderer_get_line_height(fr);
    p = utf8_text;

    while (*p != '\0') {
        unsigned int codepoint;
        GlyphBitmap glyph;
        int consumed, ret;

        /* 处理显式换行符 */
        if (*p == '\n') {
            d->cursor_x = 0;
            d->cursor_y += line_height;
            p++;

            /* 超出屏幕底部则停止 */
            if (d->cursor_y >= d->fb_height) {
                LOG_DEBUG("display_draw_text: cursor_y=%d exceeds fb_height=%d, stop",
                          d->cursor_y, d->fb_height);
                break;
            }
            continue;
        }

        /* UTF-8 解码 */
        consumed = utf8_next_codepoint(p, &codepoint);
        if (consumed <= 0) {
            /* 无效序列，跳过 1 字节继续 */
            p++;
            continue;
        }
        p += consumed;

        /* 渲染字形 */
        ret = font_renderer_render_char(fr, codepoint, &glyph);
        if (ret != ERR_OK) {
            /* 跳过不可渲染字符 (如缺少对应字形) */
            continue;
        }

        /* 自动换行: 当前光标 + 字宽超出屏幕宽度 */
        if (d->cursor_x + glyph.advance_x > d->fb_width && d->cursor_x > 0) {
            d->cursor_x = 0;
            d->cursor_y += line_height;

            if (d->cursor_y >= d->fb_height) {
                LOG_DEBUG("display_draw_text: cursor_y=%d exceeds fb_height=%d, stop",
                          d->cursor_y, d->fb_height);
                break;
            }
        }

        /* 绘制字形到 framebuffer */
        display_draw_glyph(d, &glyph);

        /* 水平步进 */
        d->cursor_x += glyph.advance_x;
    }

    return ERR_OK;
}

int display_draw_text_at(Display* d, const char* utf8_text, int x, int y) {
    int saved_x, saved_y;
    int ret;

    if (d == NULL || utf8_text == NULL) {
        return ERR_DISPLAY_OPEN;
    }

    /* 保存当前光标位置 */
    saved_x = d->cursor_x;
    saved_y = d->cursor_y;

    /* 临时设置光标到指定位置 */
    d->cursor_x = x;
    d->cursor_y = y;

    /* 绘制文本 */
    ret = display_draw_text(d, utf8_text);

    /* 恢复光标位置 */
    d->cursor_x = saved_x;
    d->cursor_y = saved_y;

    return ret;
}

int display_set_cursor(Display* d, int x, int y) {
    if (d == NULL) {
        return ERR_DISPLAY_OPEN;
    }
    d->cursor_x = x;
    d->cursor_y = y;
    return ERR_OK;
}

int display_set_color(Display* d, uint32_t bg, uint32_t fg) {
    if (d == NULL) {
        return ERR_DISPLAY_OPEN;
    }
    d->bg_color = bg;
    d->fg_color = fg;
    return ERR_OK;
}

int display_draw_rect(Display* d, int x, int y, int w, int h, uint32_t color) {
    int row, col;
    int end_x, end_y;

    if (d == NULL) {
        return ERR_DISPLAY_OPEN;
    }

    /* 裁剪到屏幕范围内 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    end_x = x + w;
    end_y = y + h;
    if (end_x > d->fb_width)  { end_x = d->fb_width; }
    if (end_y > d->fb_height) { end_y = d->fb_height; }

    for (row = y; row < end_y; row++) {
        for (col = x; col < end_x; col++) {
            display_write_pixel(d, col, row, color);
        }
    }

    return ERR_OK;
}

int display_fill_screen(Display* d, uint32_t color) {
    if (d == NULL) {
        return ERR_DISPLAY_OPEN;
    }

    if (d->fb_bpp == 32) {
        uint32_t* fb = (uint32_t*)d->fb_mmap;
        int pixels_per_line = d->fb_stride / 4;
        int total_pixels = pixels_per_line * d->fb_height;
        int i;
        for (i = 0; i < total_pixels; i++) {
            fb[i] = color;
        }
    } else if (d->fb_bpp == 16) {
        uint16_t* fb = (uint16_t*)d->fb_mmap;
        uint16_t c16 = argb_to_rgb565(color);
        int pixels_per_line = d->fb_stride / 2;
        int total_pixels = pixels_per_line * d->fb_height;
        int i;
        for (i = 0; i < total_pixels; i++) {
            fb[i] = c16;
        }
    }

    return ERR_OK;
}

int display_deinit(Display* d) {
    if (d == NULL) {
        return ERR_OK;
    }

    /* 释放 FreeType 字体渲染器 */
    if (d->font != NULL) {
        FontRenderer* fr = (FontRenderer*)d->font;
        font_renderer_deinit(fr);
        free(fr);
        d->font = NULL;
    }

    /* munmap 帧缓冲 */
    if (d->fb_mmap != NULL && d->fb_mmap != MAP_FAILED) {
        /* smem_len 在 deinit 时不可用，使用 stride * height 作为映射大小估计 */
        size_t map_size = (size_t)d->fb_stride * d->fb_height;
        munmap(d->fb_mmap, map_size);
        d->fb_mmap = NULL;
    }

    /* 关闭 framebuffer 文件描述符 */
    if (d->fb_fd >= 0) {
        close(d->fb_fd);
        d->fb_fd = -1;
    }

    LOG_INFO("display_deinit: done");
    return ERR_OK;
}
