#pragma once

/*
 * User-space graphics primitives — works at EL0.
 * Draws directly to the shared backbuffer (in .bss.userbuf).
 * API-compatible with gfx:: so namespace aliasing works.
 */

#include "types.h"
#include "font.h"

/* Symbols defined in graphics.cpp, placed in .bss.userbuf */
extern u32  gfx_backbuf[];
extern u32  gfx_screen_w;
extern u32  gfx_screen_h;

namespace ugfx {

/* ── Alpha blend (duplicate of kernel's, inlined for EL0) ────────── */
inline u32 _blend(u32 fg, u32 bg, u8 alpha) {
    if (alpha == 0)   return bg;
    if (alpha == 255) return fg;
    u32 a = alpha, inv = 255 - a;
    u32 r = ((fg >> 16 & 0xFF) * a + (bg >> 16 & 0xFF) * inv + 128) / 255;
    u32 g = ((fg >> 8  & 0xFF) * a + (bg >> 8  & 0xFF) * inv + 128) / 255;
    u32 b = ((fg       & 0xFF) * a + (bg       & 0xFF) * inv + 128) / 255;
    return (r << 16) | (g << 8) | b;
}

inline void init() {
    /* no-op at EL0 — kernel handles init */
}

inline void reinit() {
    /* no-op */
}

inline void swap() {
    /* no-op — kernel calls gfx::swap() */
}

inline void clear(u32 color) {
    u32 total = gfx_screen_w * gfx_screen_h;
    for (u32 i = 0; i < total; i++)
        gfx_backbuf[i] = color;
}

inline void pixel(i32 x, i32 y, u32 color) {
    if (x < 0 || y < 0 || x >= (i32)gfx_screen_w || y >= (i32)gfx_screen_h)
        return;
    gfx_backbuf[y * gfx_screen_w + x] = color;
}

inline void fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    i32 sw = (i32)gfx_screen_w;
    i32 sh = (i32)gfx_screen_h;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;
    for (i32 row = y; row < y + h; row++) {
        u32 *p = &gfx_backbuf[row * sw + x];
        for (i32 col = 0; col < w; col++)
            p[col] = color;
    }
}

inline void rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

inline void hline(i32 x, i32 y, i32 w, u32 color) {
    fill_rect(x, y, w, 1, color);
}

inline void draw_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (c < 32 || c > 126) c = '?';
    const u8 *glyph = font_alpha[c - 32];
    i32 sw = (i32)gfx_screen_w;
    i32 sh = (i32)gfx_screen_h;
    for (i32 row = 0; row < FONT_H; row++) {
        i32 py = y + row;
        if (py < 0 || py >= sh) continue;
        for (i32 col = 0; col < FONT_W; col++) {
            i32 px = x + col;
            if (px < 0 || px >= sw) continue;
            u8 a = glyph[row * FONT_W + col];
            gfx_backbuf[py * sw + px] = _blend(fg, bg, a);
        }
    }
}

inline void draw_text(i32 x, i32 y, const char *text, u32 fg, u32 bg) {
    while (*text) {
        draw_char(x, y, *text, fg, bg);
        x += FONT_W;
        text++;
    }
}

inline void draw_text_nobg(i32 x, i32 y, const char *text, u32 fg) {
    i32 sw = (i32)gfx_screen_w;
    i32 sh = (i32)gfx_screen_h;
    while (*text) {
        char c = *text;
        if (c >= 32 && c <= 126) {
            const u8 *glyph = font_alpha[c - 32];
            for (i32 row = 0; row < FONT_H; row++) {
                i32 py = y + row;
                if (py < 0 || py >= sh) continue;
                for (i32 col = 0; col < FONT_W; col++) {
                    u8 a = glyph[row * FONT_W + col];
                    if (a > 0) {
                        i32 px = x + col;
                        if (px >= 0 && px < sw) {
                            u32 bg_px = gfx_backbuf[py * sw + px];
                            gfx_backbuf[py * sw + px] = _blend(fg, bg_px, a);
                        }
                    }
                }
            }
        }
        x += FONT_W;
        text++;
    }
}

inline i32 text_width(const char *text) {
    i32 w = 0;
    while (*text++) w += FONT_W;
    return w;
}

inline i32 font_w() { return FONT_W; }
inline i32 font_h() { return FONT_H; }

} // namespace ugfx
