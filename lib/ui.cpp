#include "ui.h"
#include "graphics.h"
#include "string.h"

namespace ui {

// ── Label ────────────────────────────────────────────────────────────────────

Label make_label(i32 x, i32 y, const char *text, u32 color) {
  Label l;
  l.x = x; l.y = y; l.color = color;
  str::ncpy(l.text, text, 55);
  return l;
}

void Label::draw(i32 ox, i32 oy) const {
  gfx::draw_text_nobg(ox + x, oy + y, text, color);
}

bool Label::hit_test(i32 mx, i32 my) const {
  i32 tw = gfx::text_width(text);
  return mx >= x && mx < x + tw && my >= y && my < y + gfx::font_h();
}

void Label::set_text(const char *t) {
  str::ncpy(text, t, 55);
}

// ── Button ───────────────────────────────────────────────────────────────────

Button make_button(i32 x, i32 y, i32 w, i32 h, const char *text) {
  Button b;
  b.x = x; b.y = y; b.w = w; b.h = h;
  b.bg_color = 0x00E0E0E0;
  b.border_color = 0x00999999;
  b.text_color = 0x00202020;
  str::ncpy(b.text, text, 55);
  return b;
}

void Button::draw(i32 ox, i32 oy) const {
  i32 ax = ox + x, ay = oy + y;
  gfx::fill_rect(ax, ay, w, h, bg_color);
  gfx::rect(ax, ay, w, h, border_color);
  // Highlight top-left edges
  gfx::hline(ax + 1, ay + 1, w - 2, 0x00F8F8F8);
  gfx::fill_rect(ax + 1, ay + 1, 1, h - 2, 0x00F8F8F8);
  // Center text
  i32 tw = gfx::text_width(text);
  i32 tx = ax + (w - tw) / 2;
  i32 ty = ay + (h - gfx::font_h()) / 2;
  gfx::draw_text(tx, ty, text, text_color, bg_color);
}

bool Button::hit_test(i32 mx, i32 my) const {
  return mx >= x && mx < x + w && my >= y && my < y + h;
}

void Button::set_text(const char *t) {
  str::ncpy(text, t, 55);
}

// ── TextBox ──────────────────────────────────────────────────────────────────

TextBox make_textbox(i32 x, i32 y, i32 w, i32 h) {
  TextBox t;
  t.x = x; t.y = y; t.w = w; t.h = h;
  t.text[0] = '\0';
  t.focused = false;
  t.cursor = 0;
  return t;
}

void TextBox::draw(i32 ox, i32 oy) const {
  i32 ax = ox + x, ay = oy + y;
  u32 border = focused ? 0x003399FF : 0x00AAAAAA;
  gfx::fill_rect(ax, ay, w, h, 0x00FFFFFF);
  gfx::rect(ax, ay, w, h, border);
  if (focused) gfx::rect(ax - 1, ay - 1, w + 2, h + 2, border);
  i32 ty = ay + (h - gfx::font_h()) / 2;
  gfx::draw_text(ax + 4, ty, text, 0x00202020, 0x00FFFFFF);
  if (focused) {
    i32 cx = ax + 4 + cursor * gfx::font_w();
    if (cx < ax + w - 2)
      gfx::fill_rect(cx, ay + 3, 1, h - 6, 0x00000000);
  }
}

bool TextBox::hit_test(i32 mx, i32 my) const {
  return mx >= x && mx < x + w && my >= y && my < y + h;
}

void TextBox::click(i32 mx) {
  focused = true;
  i32 col = (mx - x - 4) / gfx::font_w();
  if (col < 0) col = 0;
  i32 len = static_cast<i32>(str::len(text));
  if (col > len) col = len;
  cursor = col;
}

void TextBox::key(i32 k) {
  if (k == 0x7F || k == 0x08 || k == 8) {
    if (cursor > 0) {
      i32 len = static_cast<i32>(str::len(text));
      for (i32 i = cursor - 1; i < len - 1; i++) text[i] = text[i + 1];
      text[len - 1] = '\0';
      cursor--;
    }
  } else if (k >= 32 && k <= 126) {
    i32 len = static_cast<i32>(str::len(text));
    if (len < 54) {
      for (i32 i = len; i > cursor; i--) text[i] = text[i - 1];
      text[cursor] = static_cast<char>(k);
      cursor++;
      text[len + 1] = '\0';
    }
  }
}

void TextBox::focus() { focused = true; }
void TextBox::unfocus() { focused = false; }

void TextBox::set_text(const char *t) {
  str::ncpy(text, t, 55);
  cursor = 0;
}

const char *TextBox::get_text() const { return text; }

// ── CheckBox ─────────────────────────────────────────────────────────────────

CheckBox make_checkbox(i32 x, i32 y, const char *text) {
  CheckBox c;
  c.x = x; c.y = y; c.checked = false; c.color = 0x00202020;
  str::ncpy(c.text, text, 55);
  return c;
}

void CheckBox::draw(i32 ox, i32 oy) const {
  i32 ax = ox + x, ay = oy + y;
  i32 sz = gfx::font_h();
  gfx::fill_rect(ax, ay, sz, sz, 0x00FFFFFF);
  gfx::rect(ax, ay, sz, sz, 0x00888888);
  if (checked)
    gfx::fill_rect(ax + 3, ay + 3, sz - 6, sz - 6, 0x003399FF);
  gfx::draw_text_nobg(ax + sz + 6, ay, text, color);
}

bool CheckBox::hit_test(i32 mx, i32 my) const {
  i32 sz = gfx::font_h();
  i32 total_w = sz + 6 + gfx::text_width(text);
  return mx >= x && mx < x + total_w && my >= y && my < y + sz;
}

void CheckBox::toggle() { checked = !checked; }

void CheckBox::set_text(const char *t) {
  str::ncpy(text, t, 55);
}

// ── Panel ────────────────────────────────────────────────────────────────────

Panel make_panel(i32 x, i32 y, i32 w, i32 h, u32 bg_color) {
  Panel p;
  p.x = x; p.y = y; p.w = w; p.h = h;
  p.bg_color = bg_color;
  p.text[0] = '\0';
  p.text_color = 0x00202020;
  return p;
}

void Panel::draw(i32 ox, i32 oy) const {
  gfx::fill_rect(ox + x, oy + y, w, h, bg_color);
  if (text[0])
    gfx::draw_text_nobg(ox + x + 4, oy + y + 4, text, text_color);
}

void Panel::set_text(const char *t) {
  str::ncpy(text, t, 55);
}

} // namespace ui
