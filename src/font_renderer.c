/* ===================================================================
 * font_renderer.c — FreeType 字体渲染模块实现
 *
 * 功能:
 *   - FreeType 库初始化 / 字体加载 / 释放
 *   - 单字符字形渲染为灰度位图 (GlyphBitmap)
 *   - UTF-8 文本尺寸测量 (TextMetrics)
 *   - UTF-8 解码辅助函数
 *
 * 依赖: FreeType 2 (ft2build.h, freetype.h)
 * 平台: Linux ARM aarch64 (RK3506)
 * =================================================================== */

#include "font_renderer.h"
#include "common_types.h"
#include "error_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* ===================================================================
 * UTF-8 解码
 * =================================================================== */

/**
 * 读取 UTF-8 字符串的下一个 Unicode 码点
 *
 * UTF-8 编码规则:
 *   1 字节: 0xxxxxxx                          (U+0000 - U+007F)
 *   2 字节: 110xxxxx 10xxxxxx                 (U+0080 - U+07FF)
 *   3 字节: 1110xxxx 10xxxxxx 10xxxxxx        (U+0800 - U+FFFF, 含所有 CJK)
 *   4 字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx (U+10000 - U+10FFFF)
 *
 * @param text      UTF-8 文本指针
 * @param codepoint 输出 Unicode 码点
 * @return 消耗的字节数 (1-4), 0 表示 '\0', -1 表示无效序列
 */
int utf8_next_codepoint(const char* text, unsigned int* codepoint) {
    const unsigned char* p = (const unsigned char*)text;

    if (p == NULL || codepoint == NULL) {
        return -1;
    }

    /* 字符串结尾 */
    if (p[0] == '\0') {
        *codepoint = 0;
        return 0;
    }

    /* 1 字节: ASCII (0xxxxxxx) */
    if (p[0] < 0x80) {
        *codepoint = p[0];
        return 1;
    }

    /* 2 字节: 110xxxxx 10xxxxxx */
    if ((p[0] & 0xE0) == 0xC0 && p[1] != '\0') {
        if ((p[1] & 0xC0) != 0x80) {
            goto invalid;
        }
        *codepoint = ((unsigned int)(p[0] & 0x1F) << 6)
                   |  (unsigned int)(p[1] & 0x3F);
        /* 过短编码检测 (应该用 1 字节但编码为 2 字节) */
        if (*codepoint < 0x80) {
            goto invalid;
        }
        return 2;
    }

    /* 3 字节: 1110xxxx 10xxxxxx 10xxxxxx (含中文) */
    if ((p[0] & 0xF0) == 0xE0 && p[1] != '\0' && p[2] != '\0') {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) {
            goto invalid;
        }
        *codepoint = ((unsigned int)(p[0] & 0x0F) << 12)
                   | ((unsigned int)(p[1] & 0x3F) << 6)
                   |  (unsigned int)(p[2] & 0x3F);
        /* 过短编码检测 */
        if (*codepoint < 0x800) {
            goto invalid;
        }
        /* 排除代理对范围 (surrogates: U+D800 - U+DFFF) */
        if (*codepoint >= 0xD800 && *codepoint <= 0xDFFF) {
            goto invalid;
        }
        return 3;
    }

    /* 4 字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if ((p[0] & 0xF8) == 0xF0 && p[1] != '\0' && p[2] != '\0' && p[3] != '\0') {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80
                                   || (p[3] & 0xC0) != 0x80) {
            goto invalid;
        }
        *codepoint = ((unsigned int)(p[0] & 0x07) << 18)
                   | ((unsigned int)(p[1] & 0x3F) << 12)
                   | ((unsigned int)(p[2] & 0x3F) << 6)
                   |  (unsigned int)(p[3] & 0x3F);
        /* 过短编码检测 + 上限检测 */
        if (*codepoint < 0x10000 || *codepoint > 0x10FFFF) {
            goto invalid;
        }
        return 4;
    }

invalid:
    /* 无效 UTF-8 序列 → 返回替换字符 U+FFFD, 消耗 1 字节 */
    *codepoint = 0xFFFD;
    return 1;
}

/* ===================================================================
 * 公开 API 实现
 * =================================================================== */

int font_renderer_init(FontRenderer* fr, const char* font_path, int size_pt) {
    FT_Error ft_err;
    FT_Library library = NULL;
    FT_Face    face    = NULL;
    int pixel_size;

    if (fr == NULL || font_path == NULL || size_pt <= 0) {
        LOG_ERROR("font_renderer_init: invalid parameter");
        return ERR_FONT_LOAD;
    }

    /* 零初始化 */
    memset(fr, 0, sizeof(FontRenderer));

    /* 1. 初始化 FreeType 库 */
    ft_err = FT_Init_FreeType(&library);
    if (ft_err) {
        LOG_ERROR("font_renderer_init: FT_Init_FreeType failed (err=%d)", ft_err);
        return ERR_FONT_LOAD;
    }
    fr->ft_library = (void*)library;
    LOG_DEBUG("font_renderer_init: FT_Init_FreeType OK");

    /* 2. 加载字体文件 */
    ft_err = FT_New_Face(library, font_path, 0, &face);
    if (ft_err) {
        LOG_ERROR("font_renderer_init: FT_New_Face failed for '%s' (err=%d)",
                  font_path, ft_err);
        FT_Done_FreeType(library);
        fr->ft_library = NULL;
        return ERR_FONT_LOAD;
    }
    fr->ft_face = (void*)face;

    LOG_INFO("font_renderer_init: loaded '%s', family='%s', style='%s'",
             font_path,
             face->family_name ? face->family_name : "(unknown)",
             face->style_name  ? face->style_name  : "(unknown)");

    /* 3. 设置字号 (pt → pixel 转换: pixel = pt × 96 / 72) */
    pixel_size = (size_pt * 96) / 72;
    if (pixel_size < 1) {
        pixel_size = 1;
    }

    ft_err = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size);
    if (ft_err) {
        LOG_ERROR("font_renderer_init: FT_Set_Pixel_Sizes failed (err=%d)", ft_err);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        fr->ft_face    = NULL;
        fr->ft_library = NULL;
        return ERR_FONT_LOAD;
    }

    fr->font_size_pt = size_pt;

    /* 计算行高 */
    fr->line_height = (int)(face->size->metrics.height >> 6);
    if (fr->line_height < 1) {
        fr->line_height = pixel_size; /* 回退值 */
    }

    LOG_INFO("font_renderer_init: pixel_size=%d, line_height=%d, num_glyphs=%ld",
             pixel_size, fr->line_height, face->num_glyphs);

    return ERR_OK;
}

int font_renderer_render_char(FontRenderer* fr, unsigned int codepoint,
                               GlyphBitmap* out) {
    FT_Face   face;
    FT_Error  ft_err;
    FT_UInt   glyph_index;

    if (fr == NULL || out == NULL) {
        return ERR_FONT_RENDER;
    }

    face = (FT_Face)fr->ft_face;
    if (face == NULL) {
        return ERR_FONT_RENDER;
    }

    memset(out, 0, sizeof(GlyphBitmap));

    /* 获取字形索引 */
    glyph_index = FT_Get_Char_Index(face, (FT_ULong)codepoint);
    if (glyph_index == 0) {
        /* 该码点无对应字形 (缺字) */
        LOG_DEBUG("font_renderer_render_char: no glyph for U+%04X", codepoint);
        return ERR_FONT_RENDER;
    }

    /* 加载并渲染字形为灰度位图 */
    ft_err = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER);
    if (ft_err) {
        LOG_ERROR("font_renderer_render_char: FT_Load_Glyph failed (err=%d)", ft_err);
        return ERR_FONT_RENDER;
    }

    /* 确保渲染模式为灰度 (8-bit) */
    if (face->glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        LOG_WARN("font_renderer_render_char: unexpected pixel_mode=%d for U+%04X",
                 face->glyph->bitmap.pixel_mode, codepoint);
        /* 仍然尝试渲染，但可能显示异常 */
    }

    /* 填充 GlyphBitmap */
    out->buffer    = face->glyph->bitmap.buffer;
    out->width     = (int)face->glyph->bitmap.width;
    out->height    = (int)face->glyph->bitmap.rows;
    out->left      = face->glyph->bitmap_left;
    out->top       = face->glyph->bitmap_top;
    out->advance_x = (int)(face->glyph->advance.x >> 6);

    return ERR_OK;
}

int font_renderer_measure_text(FontRenderer* fr, const char* utf8_text,
                                TextMetrics* out) {
    FT_Face face;
    const char* p;
    int current_x, current_y;
    int max_width;

    if (fr == NULL || utf8_text == NULL || out == NULL) {
        return ERR_FONT_LOAD;
    }

    face = (FT_Face)fr->ft_face;
    if (face == NULL) {
        return ERR_FONT_LOAD;
    }

    memset(out, 0, sizeof(TextMetrics));
    out->line_height = fr->line_height;

    current_x = 0;
    current_y = fr->line_height;
    max_width = 0;
    p = utf8_text;

    while (*p != '\0') {
        unsigned int codepoint;
        int consumed;
        FT_UInt glyph_index;

        /* 换行符: 重置 X, 累加 Y */
        if (*p == '\n') {
            if (current_x > max_width) {
                max_width = current_x;
            }
            current_x = 0;
            current_y += fr->line_height;
            p++;
            continue;
        }

        /* UTF-8 解码 */
        consumed = utf8_next_codepoint(p, &codepoint);
        if (consumed <= 0) {
            p++;
            continue;
        }
        p += consumed;

        /* 查找字形索引 */
        glyph_index = FT_Get_Char_Index(face, (FT_ULong)codepoint);
        if (glyph_index == 0) {
            /* 缺字，跳过 */
            continue;
        }

        /* 加载字形 (不需要渲染，只需 metrics) */
        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) == 0) {
            current_x += (int)(face->glyph->advance.x >> 6);
        }
    }

    /* 最后一行宽度 */
    if (current_x > max_width) {
        max_width = current_x;
    }

    out->width  = max_width;
    out->height = current_y;

    return ERR_OK;
}

int font_renderer_get_line_height(FontRenderer* fr) {
    if (fr == NULL) {
        return 0;
    }
    return fr->line_height;
}

int font_renderer_deinit(FontRenderer* fr) {
    if (fr == NULL) {
        return ERR_OK;
    }

    /* 释放 FT_Face */
    if (fr->ft_face != NULL) {
        FT_Done_Face((FT_Face)fr->ft_face);
        fr->ft_face = NULL;
        LOG_DEBUG("font_renderer_deinit: FT_Done_Face OK");
    }

    /* 释放 FT_Library */
    if (fr->ft_library != NULL) {
        FT_Done_FreeType((FT_Library)fr->ft_library);
        fr->ft_library = NULL;
        LOG_DEBUG("font_renderer_deinit: FT_Done_FreeType OK");
    }

    return ERR_OK;
}
