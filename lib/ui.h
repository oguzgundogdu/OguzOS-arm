#pragma once

#include "types.h"

/*
 * OguzOS.UI — Native UI Component Library
 *
 * Provides reusable UI widgets for both native (.ogz) and C# (.cs) apps.
 * All components draw using gfx:: primitives.
 *
 * Usage in native apps:
 *   ui::Button btn = { 20, 50, 120, 30, "Click Me" };
 *   btn.draw(cx, cy);
 *   if (btn.hit_test(mx, my)) { ... }
 */

namespace ui {

// ── Label ────────────────────────────────────────────────────────────────────
struct Label {
  i32 x, y;
  char text[56];
  u32 color;

  void draw(i32 ox, i32 oy) const;
  bool hit_test(i32 mx, i32 my) const;
  void set_text(const char *t);
};

Label make_label(i32 x, i32 y, const char *text, u32 color = 0x00202020);

// ── Button ───────────────────────────────────────────────────────────────────
struct Button {
  i32 x, y, w, h;
  char text[56];
  u32 bg_color;
  u32 border_color;
  u32 text_color;

  void draw(i32 ox, i32 oy) const;
  bool hit_test(i32 mx, i32 my) const;
  void set_text(const char *t);
};

Button make_button(i32 x, i32 y, i32 w, i32 h, const char *text);

// ── TextBox ──────────────────────────────────────────────────────────────────
struct TextBox {
  i32 x, y, w, h;
  char text[56];
  bool focused;
  i32 cursor;

  void draw(i32 ox, i32 oy) const;
  bool hit_test(i32 mx, i32 my) const;
  void click(i32 mx);
  void key(i32 k);
  void focus();
  void unfocus();
  void set_text(const char *t);
  const char *get_text() const;
};

TextBox make_textbox(i32 x, i32 y, i32 w, i32 h);

// ── CheckBox ─────────────────────────────────────────────────────────────────
struct CheckBox {
  i32 x, y;
  char text[56];
  bool checked;
  u32 color;

  void draw(i32 ox, i32 oy) const;
  bool hit_test(i32 mx, i32 my) const;
  void toggle();
  void set_text(const char *t);
};

CheckBox make_checkbox(i32 x, i32 y, const char *text);

// ── Panel ────────────────────────────────────────────────────────────────────
struct Panel {
  i32 x, y, w, h;
  u32 bg_color;
  char text[56];
  u32 text_color;

  void draw(i32 ox, i32 oy) const;
  void set_text(const char *t);
};

Panel make_panel(i32 x, i32 y, i32 w, i32 h, u32 bg_color);

} // namespace ui
