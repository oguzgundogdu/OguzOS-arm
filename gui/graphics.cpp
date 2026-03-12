#include "graphics.h"
#include "fb.h"
#include "font.h"
#include "string.h"

namespace {

// Back buffer in BSS — 640*480*4 = 1,228,800 bytes (~1.2 MB)
constexpr u32 MAX_W = 1024;
constexpr u32 MAX_H = 768;
u32 backbuf[MAX_W * MAX_H];

u32 screen_w = 0;
u32 screen_h = 0;

} // anonymous namespace

namespace gfx {

void init() {
  screen_w = fb::width();
  screen_h = fb::height();
  str::memset(backbuf, 0, sizeof(backbuf));
}

void swap() {
  u32 *front = fb::buffer();
  u32 total = screen_w * screen_h;
  // Single memcpy from back buffer to framebuffer
  str::memcpy(front, backbuf, total * sizeof(u32));
}

void clear(u32 color) {
  u32 total = screen_w * screen_h;
  for (u32 i = 0; i < total; i++)
    backbuf[i] = color;
}

void pixel(i32 x, i32 y, u32 color) {
  if (x < 0 || y < 0 || x >= static_cast<i32>(screen_w) ||
      y >= static_cast<i32>(screen_h))
    return;
  backbuf[y * screen_w + x] = color;
}

void fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
  i32 sw = static_cast<i32>(screen_w);
  i32 sh = static_cast<i32>(screen_h);

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > sw) w = sw - x;
  if (y + h > sh) h = sh - y;
  if (w <= 0 || h <= 0) return;

  for (i32 row = y; row < y + h; row++) {
    u32 *p = &backbuf[row * sw + x];
    for (i32 col = 0; col < w; col++)
      p[col] = color;
  }
}

void rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
  hline(x, y, w, color);
  hline(x, y + h - 1, w, color);
  fill_rect(x, y, 1, h, color);
  fill_rect(x + w - 1, y, 1, h, color);
}

void hline(i32 x, i32 y, i32 w, u32 color) {
  fill_rect(x, y, w, 1, color);
}

void draw_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
  if (c < 32 || c > 126)
    c = '?';
  const u8 *glyph = font_8x8[c - 32];

  i32 sw = static_cast<i32>(screen_w);
  i32 sh = static_cast<i32>(screen_h);

  for (i32 row = 0; row < 8; row++) {
    i32 py = y + row;
    if (py < 0 || py >= sh) continue;
    u8 bits = glyph[row];
    for (i32 col = 0; col < 8; col++) {
      i32 px = x + col;
      if (px < 0 || px >= sw) continue;
      backbuf[py * sw + px] = (bits & (0x80 >> col)) ? fg : bg;
    }
  }
}

void draw_text(i32 x, i32 y, const char *text, u32 fg, u32 bg) {
  while (*text) {
    draw_char(x, y, *text, fg, bg);
    x += 8;
    text++;
  }
}

void draw_text_nobg(i32 x, i32 y, const char *text, u32 fg) {
  i32 sw = static_cast<i32>(screen_w);
  i32 sh = static_cast<i32>(screen_h);

  while (*text) {
    char c = *text;
    if (c >= 32 && c <= 126) {
      const u8 *glyph = font_8x8[c - 32];
      for (i32 row = 0; row < 8; row++) {
        i32 py = y + row;
        if (py < 0 || py >= sh) continue;
        u8 bits = glyph[row];
        for (i32 col = 0; col < 8; col++) {
          if (bits & (0x80 >> col)) {
            i32 px = x + col;
            if (px >= 0 && px < sw)
              backbuf[py * sw + px] = fg;
          }
        }
      }
    }
    x += 8;
    text++;
  }
}

i32 text_width(const char *text) {
  i32 w = 0;
  while (*text++) w += 8;
  return w;
}

} // namespace gfx
