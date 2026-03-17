#pragma once

#include "types.h"

/*
 * OguzOS Graphics Primitives
 * Drawing operations on the framebuffer.
 */

namespace gfx {

// Initialize double-buffer (call once before drawing)
void init();

// Re-read framebuffer dimensions (call after fb::set_resolution)
void reinit();

// Copy back buffer to framebuffer (call once per frame)
void swap();

void clear(u32 color);
void pixel(i32 x, i32 y, u32 color);
void fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void hline(i32 x, i32 y, i32 w, u32 color);
void draw_char(i32 x, i32 y, char c, u32 fg, u32 bg);
void draw_text(i32 x, i32 y, const char *text, u32 fg, u32 bg);
void draw_text_nobg(i32 x, i32 y, const char *text, u32 fg);
i32 text_width(const char *text);

// Font metrics (from embedded bitmap font)
i32 font_w();
i32 font_h();

} // namespace gfx
