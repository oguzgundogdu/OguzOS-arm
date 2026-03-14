#include "app.h"
#include "fb.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "registry.h"
#include "settings.h"
#include "string.h"
#include "syslog.h"

/*
 * settings.ogz — OguzOS Settings
 *
 * System configuration: Region/Timezone, Display, Keyboard, Background.
 * Supports mouse clicks and keyboard navigation.
 */

namespace {

// ── Data ────────────────────────────────────────────────────────────────────

struct TzEntry {
  i32 offset; // minutes from UTC
  const char *label;
  const char *city;
};

constexpr TzEntry timezones[] = {
    {-720, "UTC-12:00", "Baker Island"},
    {-660, "UTC-11:00", "Pago Pago"},
    {-600, "UTC-10:00", "Honolulu"},
    {-540, "UTC-09:00", "Anchorage"},
    {-480, "UTC-08:00", "Los Angeles"},
    {-420, "UTC-07:00", "Denver"},
    {-360, "UTC-06:00", "Chicago"},
    {-300, "UTC-05:00", "New York"},
    {-240, "UTC-04:00", "Santiago"},
    {-180, "UTC-03:00", "Sao Paulo"},
    {-120, "UTC-02:00", "Mid-Atlantic"},
    {-60, "UTC-01:00", "Azores"},
    {0, "UTC+00:00", "London"},
    {60, "UTC+01:00", "Berlin"},
    {120, "UTC+02:00", "Cairo"},
    {180, "UTC+03:00", "Istanbul"},
    {210, "UTC+03:30", "Tehran"},
    {240, "UTC+04:00", "Dubai"},
    {270, "UTC+04:30", "Kabul"},
    {300, "UTC+05:00", "Karachi"},
    {330, "UTC+05:30", "Mumbai"},
    {360, "UTC+06:00", "Dhaka"},
    {420, "UTC+07:00", "Bangkok"},
    {480, "UTC+08:00", "Singapore"},
    {540, "UTC+09:00", "Tokyo"},
    {570, "UTC+09:30", "Adelaide"},
    {600, "UTC+10:00", "Sydney"},
    {660, "UTC+11:00", "Noumea"},
    {720, "UTC+12:00", "Auckland"},
};
constexpr i32 TZ_COUNT = sizeof(timezones) / sizeof(timezones[0]);

const char *kb_layouts[] = {
    "English (US)", "English (UK)", "Turkish", "German",
    "French",       "Spanish",      "Italian", "Portuguese",
};
constexpr i32 KB_COUNT = 8;

struct BgPreset {
  u32 color;
  const char *name;
};

constexpr BgPreset bg_presets[] = {
    {0x00336699, "Default Blue"}, {0x001a1a2e, "Midnight"},
    {0x00006d77, "Teal"},         {0x002d5016, "Forest"},
    {0x004b0082, "Indigo"},       {0x00333333, "Charcoal"},
    {0x00722f37, "Wine"},         {0x00001f3f, "Navy"},
    {0x003c1361, "Purple"},       {0x00856d4d, "Mocha"},
    {0x002f4f4f, "Slate"},        {0x00800020, "Burgundy"},
};
constexpr i32 BG_COUNT = sizeof(bg_presets) / sizeof(bg_presets[0]);

// ── State ───────────────────────────────────────────────────────────────────

struct SettingsState {
  i32 tab;       // 0=Region, 1=Display, 2=Keyboard, 3=Background
  i32 tz_sel;    // selected timezone
  i32 tz_scroll; // scroll offset for timezone list
  i32 kb_sel;    // selected keyboard layout
  i32 bg_sel;    // selected background color
};

static_assert(sizeof(SettingsState) <= 4096, "SettingsState exceeds app_state");

// ── Colour palette ──────────────────────────────────────────────────────────

constexpr u32 COL_BG = 0x00202030;
constexpr u32 COL_TAB_BG = 0x002A2A3C;
constexpr u32 COL_TAB_ACTIVE = 0x004488CC;
constexpr u32 COL_TAB_HOVER = 0x00384858;
constexpr u32 COL_TEXT = 0x00E0E0F0;
constexpr u32 COL_TEXT_DIM = 0x00808098;
constexpr u32 COL_SECTION = 0x007AA2F7;
constexpr u32 COL_ITEM_BG = 0x002A2A3C;
constexpr u32 COL_ITEM_SEL = 0x004488CC;
constexpr u32 COL_ITEM_HOVER = 0x00384858;
constexpr u32 COL_LABEL = 0x00A9B1D6;
constexpr u32 COL_VALUE = 0x00C0CAF5;
constexpr u32 COL_GOOD = 0x009ECE6A;
constexpr u32 COL_CHECK = 0x009ECE6A;
constexpr u32 COL_SWATCH_BORDER = 0x00606080;
constexpr u32 COL_SWATCH_SEL = 0x00FFFFFF;

constexpr i32 PAD = 8;
constexpr i32 TAB_H = 30;

// ── Helpers ─────────────────────────────────────────────────────────────────

i32 CHAR_W = 8;
i32 LINE_H = 22;

void int_to_str(i64 val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  char tmp[20];
  i32 i = 0;
  bool neg = val < 0;
  if (neg)
    val = -val;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  i32 j = 0;
  if (neg)
    buf[j++] = '-';
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// Find timezone index matching a given offset in minutes
i32 find_tz_by_offset(i32 offset) {
  for (i32 i = 0; i < TZ_COUNT; i++) {
    if (timezones[i].offset == offset)
      return i;
  }
  return 12; // default to UTC
}

// ── Tab drawing helpers ─────────────────────────────────────────────────────

const char *tab_labels[] = {"Region", "Display", "Keyboard", "Background"};
constexpr i32 TAB_COUNT = 4;

void draw_tabs(SettingsState *s, i32 cx, i32 cy, i32 cw) {
  i32 tab_w = cw / TAB_COUNT;

  for (i32 i = 0; i < TAB_COUNT; i++) {
    i32 tx = cx + i * tab_w;
    u32 bg = (i == s->tab) ? COL_TAB_ACTIVE : COL_TAB_BG;
    gfx::fill_rect(tx, cy, tab_w - 1, TAB_H, bg);

    // Active tab indicator line at bottom
    if (i == s->tab)
      gfx::fill_rect(tx, cy + TAB_H - 3, tab_w - 1, 3, 0x00FFFFFF);

    // Center label
    i32 lw = static_cast<i32>(str::len(tab_labels[i])) * CHAR_W;
    i32 lx = tx + (tab_w - 1 - lw) / 2;
    i32 ly = cy + (TAB_H - gfx::font_h()) / 2;
    gfx::draw_text(lx, ly, tab_labels[i], COL_TEXT, bg);
  }
}

// ── Region tab ──────────────────────────────────────────────────────────────

void draw_region(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  // Section header
  gfx::draw_text(x, y, "Select Timezone", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  // Compute visible items
  i32 list_h = max_y - y;
  i32 visible = list_h / LINE_H;
  if (visible < 1)
    visible = 1;

  // Ensure selected item is visible
  if (s->tz_sel < s->tz_scroll)
    s->tz_scroll = s->tz_sel;
  if (s->tz_sel >= s->tz_scroll + visible)
    s->tz_scroll = s->tz_sel - visible + 1;
  if (s->tz_scroll < 0)
    s->tz_scroll = 0;

  // Draw timezone list
  for (i32 i = 0; i < visible && (s->tz_scroll + i) < TZ_COUNT; i++) {
    i32 idx = s->tz_scroll + i;
    i32 iy = y + i * LINE_H;

    if (iy + LINE_H > max_y)
      break;

    bool selected = (idx == s->tz_sel);
    u32 bg = selected ? COL_ITEM_SEL : ((idx % 2 == 0) ? COL_BG : COL_ITEM_BG);
    gfx::fill_rect(x, iy, w, LINE_H, bg);

    // Check mark for currently active timezone
    bool is_active = (timezones[idx].offset == settings::get_tz_offset());
    if (is_active)
      gfx::draw_text(x + 2, iy + 1, "*", COL_CHECK, bg);

    // Label
    gfx::draw_text(x + CHAR_W * 2, iy + 1, timezones[idx].label,
                    selected ? 0x00FFFFFF : COL_VALUE, bg);

    // City
    gfx::draw_text(x + CHAR_W * 14, iy + 1, timezones[idx].city,
                    selected ? 0x00FFFFFF : COL_TEXT_DIM, bg);
  }

  // Scrollbar
  constexpr i32 SB_W = 12;
  if (TZ_COUNT > visible) {
    i32 sb_x = x + w - SB_W;
    i32 sb_h = list_h;
    i32 thumb_h = (visible * sb_h) / TZ_COUNT;
    if (thumb_h < 20)
      thumb_h = 20;
    i32 max_sc = TZ_COUNT - visible;
    i32 travel = sb_h - thumb_h;
    i32 thumb_y = y;
    if (max_sc > 0 && travel > 0)
      thumb_y = y + (s->tz_scroll * travel) / max_sc;
    gfx::fill_rect(sb_x, y, SB_W, sb_h, 0x00303040);
    gfx::fill_rect(sb_x + 1, thumb_y, SB_W - 2, thumb_h, 0x00707090);
    gfx::rect(sb_x + 1, thumb_y, SB_W - 2, thumb_h, 0x00909090);
  }
}

// ── Display tab ─────────────────────────────────────────────────────────────

void draw_display(SettingsState *, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "Display Information", COL_SECTION, COL_BG);
  y += LINE_H + 8;

  // Section background
  gfx::fill_rect(x, y, w, LINE_H * 6 + 8, COL_ITEM_BG);

  auto draw_kv = [&](const char *label, const char *value, u32 vcol = COL_VALUE) {
    if (y + LINE_H > max_y)
      return;
    gfx::draw_text(x + 8, y + 4, label, COL_LABEL, COL_ITEM_BG);
    i32 vx = x + CHAR_W * 16;
    gfx::draw_text(vx, y + 4, value, vcol, COL_ITEM_BG);
    y += LINE_H;
  };

  char buf[32];

  // Resolution
  if (fb::is_available()) {
    int_to_str(static_cast<i64>(fb::width()), buf);
    str::cat(buf, " x ");
    char h[8];
    int_to_str(static_cast<i64>(fb::height()), h);
    str::cat(buf, h);
    draw_kv("Resolution:", buf, COL_GOOD);
  } else {
    draw_kv("Resolution:", "N/A");
  }

  draw_kv("Color Depth:", "32-bit XRGB8888");
  draw_kv("Device:", "ramfb (QEMU)");
  draw_kv("Refresh:", "Vsync (on input)");

  // Font info
  char font_buf[32];
  int_to_str(static_cast<i64>(gfx::font_w()), font_buf);
  str::cat(font_buf, "x");
  char fh_str[8];
  int_to_str(static_cast<i64>(gfx::font_h()), fh_str);
  str::cat(font_buf, fh_str);
  str::cat(font_buf, " JetBrains Mono");
  draw_kv("Font:", font_buf);

  draw_kv("Buffer:", "Double-buffered");

  y += 16;
  gfx::draw_text(x + 8, y, "Display settings are fixed by hardware.",
                  COL_TEXT_DIM, COL_BG);
}

// ── Keyboard tab ────────────────────────────────────────────────────────────

void draw_keyboard(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "Keyboard Layout", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  for (i32 i = 0; i < KB_COUNT && y + LINE_H <= max_y; i++) {
    bool selected = (i == s->kb_sel);
    u32 bg = selected ? COL_ITEM_SEL : ((i % 2 == 0) ? COL_BG : COL_ITEM_BG);
    gfx::fill_rect(x, y, w, LINE_H, bg);

    // Check mark for active layout
    bool is_active = (i == settings::get_kbd_layout());
    if (is_active)
      gfx::draw_text(x + 2, y + 1, "*", COL_CHECK, bg);

    gfx::draw_text(x + CHAR_W * 2, y + 1, kb_layouts[i],
                    selected ? 0x00FFFFFF : COL_VALUE, bg);
    y += LINE_H;
  }

  y += 8;
  if (y + LINE_H <= max_y) {
    gfx::draw_text(x + 8, y, "Press Enter to apply selection.",
                    COL_TEXT_DIM, COL_BG);
  }
}

// ── Background tab ──────────────────────────────────────────────────────────

void draw_background(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "Desktop Background", COL_SECTION, COL_BG);
  y += LINE_H + 8;

  // Color swatches in a grid
  constexpr i32 SWATCH_W = 48;
  constexpr i32 SWATCH_H = 36;
  constexpr i32 GAP = 8;
  i32 cols = (w + GAP) / (SWATCH_W + GAP);
  if (cols < 1)
    cols = 1;

  for (i32 i = 0; i < BG_COUNT; i++) {
    i32 col = i % cols;
    i32 row = i / cols;
    i32 sx = x + col * (SWATCH_W + GAP);
    i32 sy = y + row * (SWATCH_H + GAP + LINE_H);

    if (sy + SWATCH_H + LINE_H > max_y)
      break;

    // Border (highlighted if selected)
    bool selected = (i == s->bg_sel);
    bool is_active = (bg_presets[i].color == settings::get_desktop_color());
    u32 border_col = selected ? COL_SWATCH_SEL
                     : is_active ? COL_CHECK
                                 : COL_SWATCH_BORDER;
    i32 bw = selected || is_active ? 2 : 1;

    // Draw border
    for (i32 b = 0; b < bw; b++) {
      gfx::rect(sx - b, sy - b, SWATCH_W + b * 2, SWATCH_H + b * 2,
                 border_col);
    }

    // Fill swatch
    gfx::fill_rect(sx, sy, SWATCH_W, SWATCH_H, bg_presets[i].color);

    // Label below swatch
    i32 lbl_w = static_cast<i32>(str::len(bg_presets[i].name)) * CHAR_W;
    i32 lbl_x = sx + (SWATCH_W - lbl_w) / 2;
    if (lbl_x < sx)
      lbl_x = sx;
    gfx::draw_text(lbl_x, sy + SWATCH_H + 2, bg_presets[i].name,
                    selected ? COL_TEXT : COL_TEXT_DIM, COL_BG);
  }

  // Current color preview
  i32 preview_y = y + ((BG_COUNT + cols - 1) / cols) * (SWATCH_H + GAP + LINE_H) + 8;
  if (preview_y + SWATCH_H <= max_y) {
    gfx::draw_text(x, preview_y, "Current:", COL_LABEL, COL_BG);
    gfx::fill_rect(x + CHAR_W * 10, preview_y, 60, LINE_H,
                    settings::get_desktop_color());
    gfx::rect(x + CHAR_W * 10, preview_y, 60, LINE_H, COL_SWATCH_BORDER);
  }
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void settings_open(u8 *state) {
  auto *s = reinterpret_cast<SettingsState *>(state);
  s->tab = 0;
  s->tz_sel = find_tz_by_offset(settings::get_tz_offset());
  s->tz_scroll = 0;
  s->kb_sel = settings::get_kbd_layout();
  s->bg_sel = 0;

  // Find currently active background color
  u32 cur = settings::get_desktop_color();
  for (i32 i = 0; i < BG_COUNT; i++) {
    if (bg_presets[i].color == cur) {
      s->bg_sel = i;
      break;
    }
  }

  syslog::info("settings", "settings opened");
}

void settings_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  // Background
  gfx::fill_rect(cx, cy, cw, ch, COL_BG);

  // Tab bar
  draw_tabs(s, cx, cy, cw);

  // Content area below tabs
  i32 content_y = cy + TAB_H + 2;
  i32 content_h = ch - TAB_H - 2;

  switch (s->tab) {
  case 0:
    draw_region(s, cx, content_y, cw, content_h);
    break;
  case 1:
    draw_display(s, cx, content_y, cw, content_h);
    break;
  case 2:
    draw_keyboard(s, cx, content_y, cw, content_h);
    break;
  case 3:
    draw_background(s, cx, content_y, cw, content_h);
    break;
  }
}

void apply_current(SettingsState *s) {
  switch (s->tab) {
  case 0: // Region
    if (s->tz_sel >= 0 && s->tz_sel < TZ_COUNT) {
      settings::set_tz_offset(timezones[s->tz_sel].offset);
      syslog::info("settings", "timezone set to %s (%s)",
                    timezones[s->tz_sel].label, timezones[s->tz_sel].city);
    }
    break;
  case 2: // Keyboard
    if (s->kb_sel >= 0 && s->kb_sel < KB_COUNT) {
      settings::set_kbd_layout(s->kb_sel);
      syslog::info("settings", "keyboard layout set to %s",
                    kb_layouts[s->kb_sel]);
    }
    break;
  case 3: // Background
    if (s->bg_sel >= 0 && s->bg_sel < BG_COUNT) {
      settings::set_desktop_color(bg_presets[s->bg_sel].color);
      syslog::info("settings", "desktop color set to %s",
                    bg_presets[s->bg_sel].name);
    }
    break;
  }
  settings::save();
}

bool settings_key(u8 *state, char key) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  if (key == '\r' || key == '\n') {
    apply_current(s);
    return true;
  }

  return false;
}

void settings_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  if (dir == 'C') {
    // Right → next tab
    if (s->tab < TAB_COUNT - 1)
      s->tab++;
  } else if (dir == 'D') {
    // Left → previous tab
    if (s->tab > 0)
      s->tab--;
  } else if (dir == 'A') {
    // Up → previous item in list
    switch (s->tab) {
    case 0:
      if (s->tz_sel > 0)
        s->tz_sel--;
      break;
    case 2:
      if (s->kb_sel > 0)
        s->kb_sel--;
      break;
    case 3:
      if (s->bg_sel > 0)
        s->bg_sel--;
      break;
    }
  } else if (dir == 'B') {
    // Down → next item in list
    switch (s->tab) {
    case 0:
      if (s->tz_sel < TZ_COUNT - 1)
        s->tz_sel++;
      break;
    case 2:
      if (s->kb_sel < KB_COUNT - 1)
        s->kb_sel++;
      break;
    case 3:
      if (s->bg_sel < BG_COUNT - 1)
        s->bg_sel++;
      break;
    }
  }
}

void settings_click(u8 *state, i32 rx, i32 ry, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  // Tab bar click
  if (ry < TAB_H) {
    i32 tab_w = cw / TAB_COUNT;
    i32 clicked_tab = rx / tab_w;
    if (clicked_tab >= 0 && clicked_tab < TAB_COUNT)
      s->tab = clicked_tab;
    return;
  }

  // Content area click
  i32 content_y = TAB_H + 2;
  i32 content_ry = ry - content_y;

  if (content_ry < 0)
    return;

  switch (s->tab) {
  case 0: {
    // Region: click on timezone list
    i32 list_y = PAD + LINE_H + 4; // relative to content area
    i32 list_w = cw - PAD * 2;
    i32 list_h = ch - TAB_H - 2 - PAD - (LINE_H + 4) - PAD;
    i32 local_y = content_ry - list_y;
    i32 visible = list_h / LINE_H;
    if (visible < 1) visible = 1;
    i32 max_sc = TZ_COUNT - visible;
    if (max_sc < 0) max_sc = 0;

    // Check if click is on the scrollbar area (right SB_W pixels)
    constexpr i32 SB_W_CLICK = 12;
    if (rx >= PAD + list_w - SB_W_CLICK && local_y >= 0 && TZ_COUNT > visible) {
      // Jump scroll position proportionally to click Y in track
      if (list_h > 0) {
        i32 new_scroll = (local_y * max_sc) / list_h;
        if (new_scroll < 0) new_scroll = 0;
        if (new_scroll > max_sc) new_scroll = max_sc;
        s->tz_scroll = new_scroll;
      }
      break;
    }

    if (local_y >= 0) {
      i32 clicked = local_y / LINE_H + s->tz_scroll;
      if (clicked >= 0 && clicked < TZ_COUNT) {
        s->tz_sel = clicked;
        settings::set_tz_offset(timezones[s->tz_sel].offset);
        settings::save();
        syslog::info("settings", "timezone set to %s (%s)",
                      timezones[s->tz_sel].label, timezones[s->tz_sel].city);
      }
    }
    break;
  }
  case 2: {
    // Keyboard: click on layout list
    i32 list_y = PAD + LINE_H + 4;
    i32 local_y = content_ry - list_y;
    if (local_y >= 0) {
      i32 clicked = local_y / LINE_H;
      if (clicked >= 0 && clicked < KB_COUNT) {
        s->kb_sel = clicked;
        settings::set_kbd_layout(s->kb_sel);
        settings::save();
        syslog::info("settings", "keyboard layout set to %s",
                      kb_layouts[s->kb_sel]);
      }
    }
    break;
  }
  case 3: {
    // Background: click on color swatch grid
    i32 grid_y = PAD + LINE_H + 8; // relative to content area
    i32 w = cw - PAD * 2;
    constexpr i32 SWATCH_W = 48;
    constexpr i32 SWATCH_H = 36;
    constexpr i32 GAP = 8;
    i32 cols = (w + GAP) / (SWATCH_W + GAP);
    if (cols < 1)
      cols = 1;

    i32 local_x = rx - PAD;
    i32 local_y = content_ry - grid_y;

    if (local_x >= 0 && local_y >= 0) {
      i32 row_h = SWATCH_H + GAP + LINE_H;
      i32 col = local_x / (SWATCH_W + GAP);
      i32 row = local_y / row_h;

      // Check we clicked within the swatch area (not the gap)
      i32 in_col_x = local_x % (SWATCH_W + GAP);
      i32 in_row_y = local_y % row_h;

      if (in_col_x < SWATCH_W && in_row_y < SWATCH_H + LINE_H && col < cols) {
        i32 idx = row * cols + col;
        if (idx >= 0 && idx < BG_COUNT) {
          s->bg_sel = idx;
          settings::set_desktop_color(bg_presets[idx].color);
          settings::save();
          syslog::info("settings", "desktop color set to %s",
                        bg_presets[idx].name);
        }
      }
    }
    break;
  }
  }
}

void settings_mouse_down(u8 *state, i32 rx, i32 ry, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  if (s->tab != 0) return; // only Region has a scrollbar

  i32 content_y = TAB_H + 2;
  i32 content_ry = ry - content_y;
  i32 list_y = PAD + LINE_H + 4;
  i32 list_w = cw - PAD * 2;
  i32 list_h = ch - content_y - PAD - list_y - PAD;
  i32 local_y = content_ry - list_y;

  constexpr i32 SB_W_HIT = 12;
  if (rx >= PAD + list_w - SB_W_HIT && local_y >= 0 && list_h > 0) {
    i32 visible = list_h / LINE_H;
    if (visible < 1) visible = 1;
    i32 max_sc = TZ_COUNT - visible;
    if (max_sc > 0) {
      // Store drag start info in unused high bits of tz_scroll
      // Actually, just compute position directly - we'll track via mouse_move
      i32 new_scroll = (local_y * max_sc) / list_h;
      if (new_scroll < 0) new_scroll = 0;
      if (new_scroll > max_sc) new_scroll = max_sc;
      s->tz_scroll = new_scroll;
    }
  }
}

void settings_mouse_move(u8 *state, i32 /*rx*/, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  if (s->tab != 0) return;

  i32 content_y = TAB_H + 2;
  i32 content_ry = ry - content_y;
  i32 list_y = PAD + LINE_H + 4;
  i32 list_h = ch - content_y - PAD - list_y - PAD;
  i32 local_y = content_ry - list_y;

  // During drag, update scroll proportionally to mouse Y in the list area
  if (list_h > 0) {
    i32 visible = list_h / LINE_H;
    if (visible < 1) visible = 1;
    i32 max_sc = TZ_COUNT - visible;
    if (max_sc > 0) {
      i32 new_scroll = (local_y * max_sc) / list_h;
      if (new_scroll < 0) new_scroll = 0;
      if (new_scroll > max_sc) new_scroll = max_sc;
      s->tz_scroll = new_scroll;
    }
  }
}

void settings_scroll(u8 *state, i32 delta) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  if (s->tab == 0) {
    // Region tab: scroll timezone list
    s->tz_scroll -= delta * 3;
    if (s->tz_scroll < 0) s->tz_scroll = 0;
    i32 max_sc = TZ_COUNT - 1; // will be clamped in draw
    if (s->tz_scroll > max_sc) s->tz_scroll = max_sc;
  }
}

void settings_close(u8 *) {
  syslog::info("settings", "settings closed");
}

const OgzApp settings_app = {
    "Settings",        // name
    "settings.ogz",    // id
    520,               // default_w
    480,               // default_h
    settings_open,     // on_open
    settings_draw,     // on_draw
    settings_key,      // on_key
    settings_arrow,    // on_arrow
    settings_close,    // on_close
    settings_click,      // on_click
    settings_scroll,     // on_scroll
    settings_mouse_down, // on_mouse_down
    settings_mouse_move, // on_mouse_move
};

} // anonymous namespace

namespace apps {
void register_settings() { register_app(&settings_app); }
} // namespace apps
