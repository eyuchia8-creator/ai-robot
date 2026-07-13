#ifndef _FONT_RENDERER_H_
#define _FONT_RENDERER_H_

/* ===================================================================
 * FreeType 字体渲染模块 — 字形渲染 + UTF-8 解码
 *
 * 依赖: FreeType 2 (libfreetype)
 * 支持: TrueType (.ttf), OpenType (.otf), 中文字体
 * =================================================================== */

#include "display.h" /* GlyphBitmap */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
 * TextMetrics — 文本尺寸 (用于布局计算)
 * ------------------------------------------------------------------- */
typedef struct {
    int width;       /* 总宽度 (像素) — 最宽行的宽度 */
    int height;      /* 总高度 (像素) — 所有行高之和 */
    int line_height; /* 行高 (像素) */
} TextMetrics;

/* -------------------------------------------------------------------
 * FontRenderer — 字体渲染器句柄
 * ------------------------------------------------------------------- */
typedef struct FontRenderer {
    void* ft_library;    /* FT_Library 句柄 */
    void* ft_face;       /* FT_Face 句柄 */
    int   font_size_pt;  /* 字号 (磅) */
    int   line_height;   /* 行高 (像素) */
} FontRenderer;

/* -------------------------------------------------------------------
 * API 声明
 * ------------------------------------------------------------------- */

/**
 * 初始化字体渲染器
 *
 * 1. FT_Init_FreeType() 初始化 FreeType 库
 * 2. FT_New_Face() 加载 .ttf 字体文件
 * 3. FT_Set_Pixel_Sizes() 设置字号 (pt → pixel 转换: pixel = pt * 96 / 72)
 *
 * @param fr        FontRenderer 结构体指针 (调用者分配)
 * @param font_path 字体文件路径
 * @param size_pt   字号 (磅)
 * @return ERR_OK (0) 成功, ERR_FONT_LOAD (-4003) 失败
 */
int font_renderer_init(FontRenderer* fr, const char* font_path, int size_pt);

/**
 * 渲染单个 Unicode 码点为灰度位图
 *
 * 1. FT_Load_Char() 加载并渲染字形 (带 FT_LOAD_RENDER)
 * 2. 从 face->glyph->bitmap 提取灰度数据
 * 3. 填充 GlyphBitmap 结构
 *
 * @param fr        FontRenderer 句柄
 * @param codepoint Unicode 码点
 * @param out       输出 GlyphBitmap (buffer 指向 FreeType 内部数据, 不可 free)
 * @return ERR_OK (0) 成功, ERR_FONT_RENDER (-4004) 失败
 */
int font_renderer_render_char(FontRenderer* fr, unsigned int codepoint,
                               GlyphBitmap* out);

/**
 * 测量 UTF-8 文本的像素尺寸
 *
 * 逐字符累加 advance_x, 遇 '\n' 重置 X 并累加 Y。
 *
 * @param fr        FontRenderer 句柄
 * @param utf8_text UTF-8 编码文本 (以 '\0' 结尾)
 * @param out       输出 TextMetrics
 * @return ERR_OK (0) 成功
 */
int font_renderer_measure_text(FontRenderer* fr, const char* utf8_text,
                                TextMetrics* out);

/**
 * 获取当前字体的行高
 *
 * @param fr FontRenderer 句柄
 * @return 行高 (像素)
 */
int font_renderer_get_line_height(FontRenderer* fr);

/**
 * 释放字体渲染器资源
 *
 * - FT_Done_Face()
 * - FT_Done_FreeType()
 *
 * @param fr FontRenderer 句柄
 * @return ERR_OK (0) 成功
 */
int font_renderer_deinit(FontRenderer* fr);

/**
 * UTF-8 解码辅助函数 — 读取下一个 Unicode 码点
 *
 * 支持 1-4 字节 UTF-8 编码。
 * 遇到无效字节序列时返回 codepoint=0xFFFD 并跳过无效字节。
 *
 * @param text       UTF-8 文本指针
 * @param codepoint  输出 Unicode 码点
 * @return 消耗的字节数 (1-4), 0 表示到达字符串结尾, -1 表示无效序列
 */
int utf8_next_codepoint(const char* text, unsigned int* codepoint);

#ifdef __cplusplus
}
#endif

#endif /* _FONT_RENDERER_H_ */
