#include "app.h"
#include "registry.h"

#ifdef USERSPACE
#include "userapi.h"
#else
#include "assoc.h"
#include "fb.h"
#include "graphics.h"
#include "keyboard.h"
#include "menu.h"
#include "mouse.h"
#include "settings.h"
#include "string.h"
#include "syslog.h"
#endif

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

struct ResPreset {
  u32 w, h;
  const char *label;
};

constexpr ResPreset resolutions[] = {
    {800, 600, "800 x 600"},
    {1024, 768, "1024 x 768"},
    {1280, 720, "1280 x 720 (HD)"},
    {1280, 1024, "1280 x 1024"},
    {1366, 768, "1366 x 768"},
    {1600, 900, "1600 x 900"},
    {1920, 1080, "1920 x 1080 (FHD)"},
};
constexpr i32 RES_COUNT = sizeof(resolutions) / sizeof(resolutions[0]);

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
  i32 tab;       // 0=Region, 1=Display, 2=Keyboard, 3=Background, 4=File Types
  i32 tz_sel;    // selected timezone
  i32 tz_scroll; // scroll offset for timezone list
  i32 kb_sel;    // selected keyboard layout
  i32 bg_sel;    // selected background color
  bool sb_drag;  // true when scrollbar is being dragged
  i32 ft_sel;    // selected file type association
  i32 ft_scroll; // scroll offset for file types list
  // Inline editor for new/edit association
  char ft_ext[16];   // extension being edited
  char ft_app[32];   // app id being edited
  i32 ft_edit;       // -1 = not editing, >=0 = editing that index
  i32 ft_field;      // 0 = ext field, 1 = app field
  bool ft_adding;    // true when adding a new entry
  i32 mn_sel;        // selected menu entry
  i32 mn_scroll;     // scroll offset for menu list
  i32 res_sel;       // selected resolution preset
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

const char *tab_labels[] = {"Region", "Display", "Keyboard", "Backgrnd",
                            "Files", "Menu"};
constexpr i32 TAB_COUNT = 6;

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

void draw_display(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "Screen Resolution", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  // Resolution list
  for (i32 i = 0; i < RES_COUNT && y + LINE_H <= max_y - LINE_H * 5; i++) {
    bool selected = (i == s->res_sel);
    u32 bg = selected ? COL_ITEM_SEL : ((i % 2 == 0) ? COL_BG : COL_ITEM_BG);
    gfx::fill_rect(x, y, w, LINE_H, bg);

    // Check mark for active resolution
    bool is_active = (resolutions[i].w == fb::width() &&
                      resolutions[i].h == fb::height());
    if (is_active)
      gfx::draw_text(x + 2, y + 1, "*", COL_CHECK, bg);

    gfx::draw_text(x + CHAR_W * 2, y + 1, resolutions[i].label,
                    selected ? 0x00FFFFFF : COL_VALUE, bg);
    y += LINE_H;
  }

  y += 8;

  // Display info section
  gfx::draw_text(x, y, "Display Info", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  i32 info_y = y;
  i32 info_h = LINE_H * 4 + 8;
  if (info_y + info_h <= max_y) {
    gfx::fill_rect(x, info_y, w, info_h, COL_ITEM_BG);

    auto draw_kv = [&](const char *label, const char *value, u32 vcol = COL_VALUE) {
      gfx::draw_text(x + 8, y + 4, label, COL_LABEL, COL_ITEM_BG);
      gfx::draw_text(x + CHAR_W * 16, y + 4, value, vcol, COL_ITEM_BG);
      y += LINE_H;
    };

    draw_kv("Color Depth:", "32-bit XRGB8888");
    draw_kv("Device:", "ramfb (QEMU)");

    char font_buf[32];
    int_to_str(static_cast<i64>(gfx::font_w()), font_buf);
    str::cat(font_buf, "x");
    char fh_str[8];
    int_to_str(static_cast<i64>(gfx::font_h()), fh_str);
    str::cat(font_buf, fh_str);
    str::cat(font_buf, " JetBrains Mono");
    draw_kv("Font:", font_buf);

    draw_kv("Buffer:", "Double-buffered");
  }

  y += LINE_H;
  if (y + LINE_H <= max_y) {
    gfx::draw_text(x + 8, y, "Click or Enter to apply resolution.",
                    COL_TEXT_DIM, COL_BG);
  }
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

// ── File Types tab ──────────────────────────────────────────────────────────

void draw_filetypes(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "File Type Associations", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  // Column headers
  gfx::fill_rect(x, y, w, LINE_H, 0x00383850);
  gfx::draw_text(x + 4, y + 1, "Extension", COL_LABEL, 0x00383850);
  gfx::draw_text(x + CHAR_W * 14, y + 1, "Opens with", COL_LABEL, 0x00383850);
  y += LINE_H;

  // List entries
  i32 list_top = y;
  i32 list_h = max_y - y - LINE_H - 8; // leave room for hint
  i32 visible = list_h / LINE_H;
  if (visible < 1)
    visible = 1;

  // Clamp scroll
  i32 total = assoc::count();
  i32 max_sc = total - visible;
  if (max_sc < 0)
    max_sc = 0;
  if (s->ft_scroll > max_sc)
    s->ft_scroll = max_sc;
  if (s->ft_scroll < 0)
    s->ft_scroll = 0;

  // Ensure selection visible
  if (s->ft_sel < s->ft_scroll)
    s->ft_scroll = s->ft_sel;
  if (s->ft_sel >= s->ft_scroll + visible)
    s->ft_scroll = s->ft_sel - visible + 1;

  i32 drawn = 0;
  for (i32 i = s->ft_scroll; i < total && drawn < visible; i++) {
    const char *ext = assoc::ext_at(i);
    const char *app = assoc::app_at(i);
    if (!ext)
      continue;

    i32 iy = list_top + drawn * LINE_H;
    if (iy + LINE_H > max_y - LINE_H - 8)
      break;

    bool selected = (i == s->ft_sel);
    u32 bg = selected ? COL_ITEM_SEL : ((drawn % 2 == 0) ? COL_BG : COL_ITEM_BG);
    gfx::fill_rect(x, iy, w, LINE_H, bg);
    u32 tc = selected ? 0x00FFFFFF : COL_VALUE;
    gfx::draw_text(x + 4, iy + 1, ext, tc, bg);
    gfx::draw_text(x + CHAR_W * 14, iy + 1, app, selected ? 0x00FFFFFF : COL_TEXT_DIM, bg);
    drawn++;
  }

  // Hint at bottom
  i32 hint_y = max_y - LINE_H;
  gfx::draw_text(x + 4, hint_y,
                  "Del: remove  |  Use shell: assoc .ext app.ogz",
                  COL_TEXT_DIM, COL_BG);
}

// ── Menu tab ────────────────────────────────────────────────────────────────

const char *entry_type_str(menu::EntryType t) {
  switch (t) {
  case menu::ENTRY_APP:      return "[app]";
  case menu::ENTRY_SHORTCUT: return "[shortcut]";
  case menu::ENTRY_SEP:      return "---";
  case menu::ENTRY_EXPLORER: return "[explorer]";
  case menu::ENTRY_ABOUT:    return "[about]";
  case menu::ENTRY_SHUTDOWN: return "[shutdown]";
  case menu::ENTRY_COMMAND:  return "[cmd]";
  default: return "?";
  }
}

void draw_menu_tab(SettingsState *s, i32 cx, i32 cy, i32 cw, i32 ch) {
  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;

  gfx::draw_text(x, y, "Start Menu Entries", COL_SECTION, COL_BG);
  y += LINE_H + 4;

  // Column headers
  gfx::fill_rect(x, y, w, LINE_H, 0x00383850);
  gfx::draw_text(x + 4, y + 1, "#", COL_LABEL, 0x00383850);
  gfx::draw_text(x + CHAR_W * 3, y + 1, "Type", COL_LABEL, 0x00383850);
  gfx::draw_text(x + CHAR_W * 15, y + 1, "Label", COL_LABEL, 0x00383850);
  y += LINE_H;

  i32 list_top = y;
  i32 list_h = max_y - y - LINE_H * 2 - 4;
  i32 visible = list_h / LINE_H;
  if (visible < 1)
    visible = 1;

  i32 total = menu::count();
  i32 max_sc = total - visible;
  if (max_sc < 0) max_sc = 0;
  if (s->mn_scroll > max_sc) s->mn_scroll = max_sc;
  if (s->mn_scroll < 0) s->mn_scroll = 0;
  if (s->mn_sel < s->mn_scroll) s->mn_scroll = s->mn_sel;
  if (s->mn_sel >= s->mn_scroll + visible) s->mn_scroll = s->mn_sel - visible + 1;

  i32 drawn = 0;
  for (i32 i = s->mn_scroll; i < total && drawn < visible; i++) {
    const menu::Entry *e = menu::get(i);
    if (!e) continue;

    i32 iy = list_top + drawn * LINE_H;
    if (iy + LINE_H > max_y - LINE_H * 2 - 4) break;

    bool selected = (i == s->mn_sel);
    u32 bg = selected ? COL_ITEM_SEL : ((drawn % 2 == 0) ? COL_BG : COL_ITEM_BG);
    gfx::fill_rect(x, iy, w, LINE_H, bg);

    // Index number
    char idx_buf[4];
    idx_buf[0] = '0' + static_cast<char>(i % 10);
    if (i >= 10) {
      idx_buf[0] = '0' + static_cast<char>(i / 10);
      idx_buf[1] = '0' + static_cast<char>(i % 10);
      idx_buf[2] = '\0';
    } else {
      idx_buf[1] = '\0';
    }
    u32 tc = selected ? 0x00FFFFFF : COL_TEXT_DIM;
    gfx::draw_text(x + 4, iy + 1, idx_buf, tc, bg);

    // Type
    const char *tstr = entry_type_str(e->type);
    gfx::draw_text(x + CHAR_W * 3, iy + 1, tstr,
                    selected ? 0x00FFFFFF : COL_VALUE, bg);

    // Label
    const char *lbl = (e->type == menu::ENTRY_SEP) ? "" : e->label;
    gfx::draw_text(x + CHAR_W * 15, iy + 1, lbl,
                    selected ? 0x00FFFFFF : COL_TEXT, bg);
    drawn++;
  }

  // Action buttons at bottom
  i32 btn_y = max_y - LINE_H * 2;
  constexpr i32 BTN_W = 80;
  constexpr i32 BTN_GAP = 8;

  // "Add App" button
  gfx::fill_rect(x, btn_y, BTN_W, LINE_H, 0x00336644);
  gfx::draw_text(x + 6, btn_y + 1, "+ App", COL_TEXT, 0x00336644);

  // "Add Sep" button
  gfx::fill_rect(x + BTN_W + BTN_GAP, btn_y, BTN_W, LINE_H, 0x00444466);
  gfx::draw_text(x + BTN_W + BTN_GAP + 6, btn_y + 1, "+ Sep", COL_TEXT, 0x00444466);

  // "Move Up" button
  gfx::fill_rect(x + (BTN_W + BTN_GAP) * 2, btn_y, BTN_W, LINE_H, 0x00445566);
  gfx::draw_text(x + (BTN_W + BTN_GAP) * 2 + 10, btn_y + 1, "Move Up", COL_TEXT, 0x00445566);

  // Hint
  gfx::draw_text(x + 4, btn_y + LINE_H + 2,
                  "Del: remove  |  Click buttons or use shell",
                  COL_TEXT_DIM, COL_BG);
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void settings_open(u8 *state) {
  auto *s = reinterpret_cast<SettingsState *>(state);
  s->tab = 0;
  s->tz_sel = find_tz_by_offset(settings::get_tz_offset());
  s->tz_scroll = 0;
  s->kb_sel = settings::get_kbd_layout();
  s->bg_sel = 0;
  s->sb_drag = false;
  s->ft_sel = 0;
  s->ft_scroll = 0;
  s->ft_edit = -1;
  s->ft_field = 0;
  s->ft_adding = false;
  s->ft_ext[0] = '\0';
  s->ft_app[0] = '\0';
  s->mn_sel = 0;
  s->mn_scroll = 0;
  s->res_sel = RES_COUNT - 1; // default to last (1920x1080)

  // Find currently active resolution
  u32 cw_res = fb::width();
  u32 ch_res = fb::height();
  for (i32 i = 0; i < RES_COUNT; i++) {
    if (resolutions[i].w == cw_res && resolutions[i].h == ch_res) {
      s->res_sel = i;
      break;
    }
  }

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
  case 4:
    draw_filetypes(s, cx, content_y, cw, content_h);
    break;
  case 5:
    draw_menu_tab(s, cx, content_y, cw, content_h);
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
  case 1: // Display
    if (s->res_sel >= 0 && s->res_sel < RES_COUNT) {
      u32 nw = resolutions[s->res_sel].w;
      u32 nh = resolutions[s->res_sel].h;
      if (fb::set_resolution(nw, nh)) {
        gfx::reinit();
        settings::set_resolution(nw, nh);
        syslog::info("settings", "resolution set to %s",
                      resolutions[s->res_sel].label);
      }
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

  // Delete key (DEL=0x7F) removes selected entry
  if ((key == 0x7F || key == 0x08) && s->tab == 5) {
    const menu::Entry *e = menu::get(s->mn_sel);
    if (e && e->type != menu::ENTRY_SHUTDOWN) {
      menu::remove(s->mn_sel);
      menu::save();
      if (s->mn_sel > 0 && s->mn_sel >= menu::count())
        s->mn_sel = menu::count() - 1;
    }
    return true;
  }

  if ((key == 0x7F || key == 0x08) && s->tab == 4) {
    const char *ext = assoc::ext_at(s->ft_sel);
    if (ext) {
      assoc::unset(ext);
      assoc::save();
      if (s->ft_sel > 0 && s->ft_sel >= assoc::count())
        s->ft_sel = assoc::count() - 1;
    }
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
    case 1:
      if (s->res_sel > 0)
        s->res_sel--;
      break;
    case 2:
      if (s->kb_sel > 0)
        s->kb_sel--;
      break;
    case 3:
      if (s->bg_sel > 0)
        s->bg_sel--;
      break;
    case 4:
      if (s->ft_sel > 0)
        s->ft_sel--;
      break;
    case 5:
      if (s->mn_sel > 0)
        s->mn_sel--;
      break;
    }
  } else if (dir == 'B') {
    // Down → next item in list
    switch (s->tab) {
    case 0:
      if (s->tz_sel < TZ_COUNT - 1)
        s->tz_sel++;
      break;
    case 1:
      if (s->res_sel < RES_COUNT - 1)
        s->res_sel++;
      break;
    case 2:
      if (s->kb_sel < KB_COUNT - 1)
        s->kb_sel++;
      break;
    case 3:
      if (s->bg_sel < BG_COUNT - 1)
        s->bg_sel++;
      break;
    case 4:
      if (s->ft_sel < assoc::count() - 1)
        s->ft_sel++;
      break;
    case 5:
      if (s->mn_sel < menu::count() - 1)
        s->mn_sel++;
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
  case 1: {
    // Display: click on resolution list
    i32 list_y = PAD + LINE_H + 4;
    i32 local_y = content_ry - list_y;
    if (local_y >= 0) {
      i32 clicked = local_y / LINE_H;
      if (clicked >= 0 && clicked < RES_COUNT) {
        s->res_sel = clicked;
        // Apply immediately on click
        u32 nw = resolutions[clicked].w;
        u32 nh = resolutions[clicked].h;
        if (fb::set_resolution(nw, nh)) {
          gfx::reinit();
          settings::set_resolution(nw, nh);
          settings::save();
          syslog::info("settings", "resolution set to %s",
                        resolutions[clicked].label);
        }
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
  case 4: {
    // File Types: click on association list
    // header line = LINE_H + 4, then column header = LINE_H
    i32 list_y = PAD + LINE_H + 4 + LINE_H;
    i32 local_y = content_ry - list_y;
    if (local_y >= 0) {
      i32 clicked = local_y / LINE_H + s->ft_scroll;
      if (clicked >= 0 && clicked < assoc::count())
        s->ft_sel = clicked;
    }
    break;
  }
  case 5: {
    // Menu tab
    i32 list_y = PAD + LINE_H + 4 + LINE_H;
    i32 content_h_tab = ch - TAB_H - 2;
    i32 btn_y = content_h_tab - PAD - LINE_H * 2;

    // Check button clicks
    constexpr i32 BTN_W = 80;
    constexpr i32 BTN_GAP = 8;
    if (content_ry >= btn_y && content_ry < btn_y + LINE_H) {
      i32 bx = rx - PAD;
      if (bx >= 0 && bx < BTN_W) {
        // "+ App" button: pin next unpinned app
        for (i32 i = 0; i < apps::count(); i++) {
          const OgzApp *a = apps::get(i);
          if (a && !menu::has_app(a->id)) {
            // Insert before first separator
            i32 pos = menu::count();
            for (i32 j = 0; j < menu::count(); j++) {
              const menu::Entry *e = menu::get(j);
              if (e && e->type == menu::ENTRY_SEP) { pos = j; break; }
            }
            menu::insert(pos, menu::ENTRY_APP, a->name, a->id);
            menu::save();
            s->mn_sel = pos;
            break;
          }
        }
      } else if (bx >= BTN_W + BTN_GAP && bx < BTN_W * 2 + BTN_GAP) {
        // "+ Sep" button
        menu::insert(s->mn_sel + 1, menu::ENTRY_SEP, "---", "");
        menu::save();
        s->mn_sel++;
      } else if (bx >= (BTN_W + BTN_GAP) * 2 && bx < (BTN_W + BTN_GAP) * 2 + BTN_W) {
        // "Move Up" button
        if (s->mn_sel > 0) {
          menu::move(s->mn_sel, s->mn_sel - 1);
          menu::save();
          s->mn_sel--;
        }
      }
      break;
    }

    // Click on entry list
    i32 local_y = content_ry - list_y;
    if (local_y >= 0) {
      i32 clicked = local_y / LINE_H + s->mn_scroll;
      if (clicked >= 0 && clicked < menu::count())
        s->mn_sel = clicked;
    }
    break;
  }
  }
}

void settings_mouse_down(u8 *state, i32 rx, i32 ry, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<SettingsState *>(state);

  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  s->sb_drag = false;

  if (s->tab != 0) return; // only Region has a scrollbar

  i32 content_y = TAB_H + 2;
  i32 content_ry = ry - content_y;
  i32 list_y = PAD + LINE_H + 4;
  i32 list_w = cw - PAD * 2;
  i32 list_h = ch - content_y - PAD - list_y - PAD;
  i32 local_y = content_ry - list_y;

  constexpr i32 SB_W_HIT = 12;
  if (rx >= PAD + list_w - SB_W_HIT && local_y >= 0 && list_h > 0) {
    s->sb_drag = true;
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

  if (!s->sb_drag) return;

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
    s->tz_scroll -= delta * 3;
    if (s->tz_scroll < 0) s->tz_scroll = 0;
    i32 max_sc = TZ_COUNT - 1;
    if (s->tz_scroll > max_sc) s->tz_scroll = max_sc;
  } else if (s->tab == 4) {
    s->ft_scroll -= delta * 3;
    if (s->ft_scroll < 0) s->ft_scroll = 0;
    i32 max_sc = assoc::count() - 1;
    if (max_sc < 0) max_sc = 0;
    if (s->ft_scroll > max_sc) s->ft_scroll = max_sc;
  } else if (s->tab == 5) {
    s->mn_scroll -= delta * 3;
    if (s->mn_scroll < 0) s->mn_scroll = 0;
    i32 max_sc = menu::count() - 1;
    if (max_sc < 0) max_sc = 0;
    if (s->mn_scroll > max_sc) s->mn_scroll = max_sc;
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
    nullptr,             // on_open_file
};

} // anonymous namespace

namespace apps {
void register_settings() { register_app(&settings_app); }
} // namespace apps
