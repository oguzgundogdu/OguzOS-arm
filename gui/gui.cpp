#include "gui.h"
#include "app.h"
#include "exception.h"
#include "fb.h"
#include "fs.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "registry.h"
#include "settings.h"
#include "string.h"
#include "syslog.h"
#include "types.h"
#include "uart.h"

namespace {

// ── Colour palette ──────────────────────────────────────────────────────────
constexpr u32 COL_DESKTOP = 0x00336699;
constexpr u32 COL_TASKBAR = 0x002D2D2D;
constexpr u32 COL_TASK_TEXT = 0x00FFFFFF;
constexpr u32 COL_WIN_TITLE = 0x004488CC;
constexpr u32 COL_WIN_INACTIVE = 0x00666666;
constexpr u32 COL_WIN_BODY = 0x00F0F0F0;
constexpr u32 COL_WIN_BORDER = 0x00333333;
constexpr u32 COL_TEXT_DARK = 0x00202020;
constexpr u32 COL_TEXT_LIGHT = 0x00FFFFFF;
constexpr u32 COL_CLOSE_BTN = 0x00CC4444;
constexpr u32 COL_CLOSE_HOV = 0x00EE6666;
constexpr u32 COL_FOLDER = 0x00FFCC44;
constexpr u32 COL_FILE_ICON = 0x00AACCEE;
constexpr u32 COL_SELECTION = 0x003399FF;
constexpr u32 COL_SCROLLBAR = 0x00CCCCCC;

constexpr i32 TASKBAR_H = 30;
constexpr i32 TITLEBAR_H = 24;

// ── Window types ────────────────────────────────────────────────────────────
enum WinType { WIN_EXPLORER, WIN_TEXTVIEW, WIN_APP };

struct Window {
  i32 x, y, w, h;
  char title[64];
  bool visible;
  bool active;
  bool minimized;
  bool maximized;
  WinType type;

  // Saved geometry for restore from maximize
  i32 restore_x, restore_y, restore_w, restore_h;

  // Explorer state
  char path[256];
  i32 scroll;
  i32 selected;

  // Text viewer state
  char view_text[4096];

  // App state (WIN_APP only)
  OgzApp *app;
  u8 app_state[4096]; // private state for the app
};

constexpr i32 MAX_WINDOWS = 8;
Window windows[MAX_WINDOWS];
i32 window_count = 0;
i32 active_window = -1;

// Mouse state
i32 mouse_x = 320, mouse_y = 240;
bool prev_mouse_left = false;

// Drag state
bool dragging = false;
i32 drag_win = -1;
i32 drag_ox, drag_oy;

// Resize state
bool resizing = false;
i32 resize_win = -1;
i32 resize_edge = 0; // bitmask: 1=right, 2=bottom
i32 resize_start_x, resize_start_y;
i32 resize_orig_w, resize_orig_h;

constexpr i32 RESIZE_GRIP = 6;  // edge detection zone width
constexpr i32 MIN_WIN_W = 160;
constexpr i32 MIN_WIN_H = 80;

// Mouse-down tracking (for up-on-same-element detection)
i32 mouse_down_x = -1, mouse_down_y = -1;

// Scrollbar drag state
bool scrollbar_dragging = false;
i32 scrollbar_drag_win = -1;
i32 scrollbar_drag_start_y = 0;
i32 scrollbar_drag_start_scroll = 0;

// Start menu state
bool start_menu_open = false;
i32 start_menu_hover = -1;

constexpr i32 MENU_W = 200;
constexpr i32 MENU_ITEM_H = 26;
constexpr i32 MENU_HEADER_H = 30;

// Menu: built dynamically from apps registry + fixed items
// Layout: [apps...] [---] [File Explorer] [About] [---] [Shutdown]
constexpr i32 MAX_MENU_ITEMS = 16;
const char *menu_labels[MAX_MENU_ITEMS];
const char *menu_app_ids[MAX_MENU_ITEMS]; // non-null = app launch
i32 menu_item_count = 0;

// Special indices (set during menu build)
i32 mi_explorer = -1;
i32 mi_about = -1;
i32 mi_shutdown = -1;

i32 menu_h_computed = 0;

void build_menu() {
  menu_item_count = 0;
  str::memset(menu_app_ids, 0, sizeof(menu_app_ids));

  // Add registered .ogz apps
  i32 nap = apps::count();
  for (i32 i = 0; i < nap && menu_item_count < MAX_MENU_ITEMS - 5; i++) {
    const OgzApp *a = apps::get(i);
    if (a) {
      menu_labels[menu_item_count] = a->name;
      menu_app_ids[menu_item_count] = a->id;
      menu_item_count++;
    }
  }

  // Separator
  menu_labels[menu_item_count] = "---";
  menu_app_ids[menu_item_count] = nullptr;
  menu_item_count++;

  // File Explorer
  mi_explorer = menu_item_count;
  menu_labels[menu_item_count] = "File Explorer";
  menu_app_ids[menu_item_count] = nullptr;
  menu_item_count++;

  // About
  mi_about = menu_item_count;
  menu_labels[menu_item_count] = "About OguzOS";
  menu_app_ids[menu_item_count] = nullptr;
  menu_item_count++;

  // Separator
  menu_labels[menu_item_count] = "---";
  menu_app_ids[menu_item_count] = nullptr;
  menu_item_count++;

  // Shutdown
  mi_shutdown = menu_item_count;
  menu_labels[menu_item_count] = "Shutdown";
  menu_app_ids[menu_item_count] = nullptr;
  menu_item_count++;

  menu_h_computed = MENU_HEADER_H + menu_item_count * MENU_ITEM_H + 2;
}

constexpr u32 COL_MENU_BG = 0x003A3A3A;
constexpr u32 COL_MENU_HOVER = 0x004488CC;
constexpr u32 COL_MENU_SEP = 0x00555555;

// Flag to signal exit from GUI loop
bool should_exit = false;

// ── Mouse cursor bitmap (simple arrow, 8 wide × 14 tall) ───────────────────
constexpr i32 CURSOR_W = 8;
constexpr i32 CURSOR_H = 14;
const u8 cursor_fill[CURSOR_H] = {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE,
                                   0xFC, 0xF8, 0xD8, 0x8C, 0x0C, 0x06, 0x00};
const u8 cursor_edge[CURSOR_H] = {0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
                                   0xFE, 0xFC, 0xFC, 0xDE, 0x0E, 0x0F, 0x07};

// ── Helpers ─────────────────────────────────────────────────────────────────
void int_to_str(i64 val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  char tmp[20];
  int i = 0;
  bool neg = val < 0;
  if (neg)
    val = -val;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  int j = 0;
  if (neg)
    buf[j++] = '-';
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// Non-blocking UART read
bool uart_try_getc(char &c) {
  constexpr u64 UART_BASE = 0x09000000;
  constexpr u64 UARTFR = 0x018;
  constexpr u32 FR_RXFE = (1 << 4);
  volatile u32 *fr = reinterpret_cast<volatile u32 *>(UART_BASE + UARTFR);
  if (*fr & FR_RXFE)
    return false;
  volatile u32 *dr = reinterpret_cast<volatile u32 *>(UART_BASE);
  c = static_cast<char>(*dr & 0xFF);
  return true;
}

// ── Window management ───────────────────────────────────────────────────────
i32 create_window(const char *title, i32 x, i32 y, i32 w, i32 h, WinType t) {
  if (window_count >= MAX_WINDOWS)
    return -1;
  i32 idx = window_count++;
  Window &win = windows[idx];
  str::memset(&win, 0, sizeof(Window));
  str::ncpy(win.title, title, 63);
  win.x = x;
  win.y = y;
  win.w = w;
  win.h = h;
  win.visible = true;
  win.type = t;
  win.selected = 0;
  win.scroll = 0;

  if (active_window >= 0)
    windows[active_window].active = false;
  active_window = idx;
  win.active = true;
  return idx;
}

void open_explorer(const char *path) {
  i32 idx = create_window("File Explorer", 60, 40, 560, 440, WIN_EXPLORER);
  if (idx >= 0)
    str::ncpy(windows[idx].path, path, 255);
}

void open_text_viewer(const char *title, const char *text) {
  i32 idx = create_window(title, 100, 60, 520, 400, WIN_TEXTVIEW);
  if (idx >= 0)
    str::ncpy(windows[idx].view_text, text, 4095);
}

void close_window(i32 idx); // forward declaration

void open_app(const char *app_id) {
  const OgzApp *app = apps::find(app_id);
  if (!app) {
    syslog::error("gui", "app not found: %s", app_id);
    return;
  }
  syslog::info("gui", "opening app: %s", app->name);
  i32 idx = create_window(app->name, 80, 30, app->default_w, app->default_h,
                           WIN_APP);
  if (idx < 0) {
    syslog::error("gui", "failed to create window (max %d reached)", MAX_WINDOWS);
    return;
  }
  windows[idx].app = const_cast<OgzApp *>(app);
  str::memset(windows[idx].app_state, 0, sizeof(windows[idx].app_state));
  if (try_enter() == 0) {
    app->on_open(windows[idx].app_state);
    try_leave();
    syslog::info("gui", "app opened: %s (window %d)", app->name, idx);
  } else {
    syslog::error("gui", "app crashed during open: %s", app->name);
    close_window(idx);
  }
}

void close_window(i32 idx) {
  if (idx < 0 || idx >= window_count)
    return;
  for (i32 i = idx; i < window_count - 1; i++)
    windows[i] = windows[i + 1];
  window_count--;
  if (active_window == idx) {
    active_window = window_count > 0 ? window_count - 1 : -1;
    if (active_window >= 0)
      windows[active_window].active = true;
  } else if (active_window > idx) {
    active_window--;
  }
}

void bring_to_front(i32 idx) {
  if (idx < 0 || idx >= window_count)
    return;
  if (active_window >= 0)
    windows[active_window].active = false;
  if (idx == window_count - 1) {
    active_window = idx;
    windows[idx].active = true;
    return;
  }
  Window tmp = windows[idx];
  for (i32 i = idx; i < window_count - 1; i++)
    windows[i] = windows[i + 1];
  windows[window_count - 1] = tmp;
  active_window = window_count - 1;
  windows[active_window].active = true;
}

void minimize_window(i32 idx) {
  if (idx < 0 || idx >= window_count)
    return;
  windows[idx].minimized = true;
  windows[idx].active = false;
  // Activate next visible window
  if (active_window == idx) {
    active_window = -1;
    for (i32 i = window_count - 1; i >= 0; i--) {
      if (!windows[i].minimized) {
        active_window = i;
        windows[i].active = true;
        break;
      }
    }
  }
}

void toggle_maximize(i32 idx) {
  if (idx < 0 || idx >= window_count)
    return;
  Window &win = windows[idx];
  if (win.maximized) {
    // Restore
    win.x = win.restore_x;
    win.y = win.restore_y;
    win.w = win.restore_w;
    win.h = win.restore_h;
    win.maximized = false;
  } else {
    // Save and maximize
    win.restore_x = win.x;
    win.restore_y = win.y;
    win.restore_w = win.w;
    win.restore_h = win.h;
    win.x = 0;
    win.y = 0;
    win.w = static_cast<i32>(fb::width());
    win.h = static_cast<i32>(fb::height()) - TASKBAR_H;
    win.maximized = true;
  }
}

// ── Explorer helpers ────────────────────────────────────────────────────────
i32 explorer_item_count(Window &win) {
  i32 dir_idx = fs::resolve(win.path);
  if (dir_idx < 0)
    return 0;
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir)
    return 0;
  i32 n = static_cast<i32>(dir->child_count);
  if (dir_idx != 0)
    n++; // ".." entry
  return n;
}

void explorer_activate(Window &win) {
  i32 dir_idx = fs::resolve(win.path);
  if (dir_idx < 0)
    return;
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir)
    return;

  i32 sel = win.selected;
  bool has_dotdot = (dir_idx != 0);

  if (has_dotdot && sel == 0) {
    // Navigate up
    usize plen = str::len(win.path);
    if (plen > 1) {
      char *p = win.path + plen - 1;
      if (*p == '/' && p != win.path) {
        *p = '\0';
        plen--;
      }
      char *last = win.path;
      for (char *s = win.path; *s; s++) {
        if (*s == '/')
          last = s;
      }
      if (last == win.path)
        win.path[1] = '\0';
      else
        *last = '\0';
    }
    win.selected = 0;
    win.scroll = 0;
    return;
  }

  i32 child_sel = has_dotdot ? (sel - 1) : sel;
  if (child_sel < 0 || child_sel >= static_cast<i32>(dir->child_count))
    return;

  i32 ci = dir->children[child_sel];
  const fs::Node *child = fs::get_node(ci);
  if (!child)
    return;

  if (child->type == fs::NodeType::Directory) {
    usize plen = str::len(win.path);
    if (plen > 1)
      str::cat(win.path, "/");
    str::cat(win.path, child->name);
    win.selected = 0;
    win.scroll = 0;
  } else {
    open_text_viewer(child->name, child->content);
  }
}

// ── Drawing ─────────────────────────────────────────────────────────────────
void draw_desktop() { gfx::clear(settings::get_desktop_color()); }

void draw_taskbar() {
  i32 sh = static_cast<i32>(fb::height());

  i32 sw = static_cast<i32>(fb::width());
  gfx::fill_rect(0, sh - TASKBAR_H, sw, TASKBAR_H, COL_TASKBAR);
  gfx::hline(0, sh - TASKBAR_H, sw, 0x00444444);

  // "OguzOS" button
  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 oguz_btn_w = fw * 6 + 16;
  i32 btn_pad = (TASKBAR_H - fh) / 2;
  gfx::fill_rect(2, sh - TASKBAR_H + 3, oguz_btn_w, TASKBAR_H - 6, 0x00445577);
  gfx::draw_text(10, sh - TASKBAR_H + btn_pad, "OguzOS", COL_TASK_TEXT, 0x00445577);

  // Window list on taskbar
  i32 tab_w = fw * 10 + 12;
  i32 tx = oguz_btn_w + 8;
  for (i32 i = 0; i < window_count; i++) {
    u32 bg;
    u32 fg = COL_TASK_TEXT;
    if (i == active_window && !windows[i].minimized)
      bg = 0x00556688;
    else if (windows[i].minimized)
      bg = 0x00383838;
    else
      bg = 0x00444444;
    if (windows[i].minimized)
      fg = 0x00999999;
    gfx::fill_rect(tx, sh - TASKBAR_H + 3, tab_w, TASKBAR_H - 6, bg);
    char short_title[12];
    str::ncpy(short_title, windows[i].title, 10);
    short_title[10] = '\0';
    gfx::draw_text(tx + 4, sh - TASKBAR_H + btn_pad, short_title, fg, bg);
    tx += tab_w + 4;
  }

  // Clock — real time from NTP + timezone offset, fallback to uptime
  u64 epoch = net::get_epoch();
  u64 hr, min;
  if (epoch > 0) {
    // Apply timezone offset (in minutes)
    i32 tz_off = settings::get_tz_offset();
    i64 local_secs = static_cast<i64>(epoch % 86400) + static_cast<i64>(tz_off) * 60;
    while (local_secs < 0) local_secs += 86400;
    while (local_secs >= 86400) local_secs -= 86400;
    hr = static_cast<u64>(local_secs) / 3600;
    min = (static_cast<u64>(local_secs) / 60) % 60;
  } else {
    // Fallback: uptime
    u64 cnt2, freq2;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt2));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq2));
    if (freq2 == 0)
      freq2 = 1;
    u64 sec = cnt2 / freq2;
    hr = (sec / 3600) % 24;
    min = (sec / 60) % 60;
  }

  char ts[6];
  ts[0] = '0' + static_cast<char>(hr / 10);
  ts[1] = '0' + static_cast<char>(hr % 10);
  ts[2] = ':';
  ts[3] = '0' + static_cast<char>(min / 10);
  ts[4] = '0' + static_cast<char>(min % 10);
  ts[5] = '\0';
  gfx::draw_text(sw - fw * 5 - 8, sh - TASKBAR_H + btn_pad, ts, COL_TASK_TEXT,
                  COL_TASKBAR);
}

constexpr u32 COL_BTN_BG = 0x00555555;
constexpr u32 COL_BTN_HOVER = 0x00777777;
constexpr i32 BTN_SIZE = 18;
constexpr i32 BTN_PAD = 2;

void draw_window_frame(Window &win) {
  u32 title_col = win.active ? COL_WIN_TITLE : COL_WIN_INACTIVE;

  // Shadow
  gfx::fill_rect(win.x + 3, win.y + 3, win.w, win.h, 0x00222222);

  // Border
  gfx::rect(win.x - 1, win.y - 1, win.w + 2, win.h + 2, COL_WIN_BORDER);

  // Title bar
  gfx::fill_rect(win.x, win.y, win.w, TITLEBAR_H, title_col);
  i32 title_text_y = win.y + (TITLEBAR_H - gfx::font_h()) / 2;
  gfx::draw_text(win.x + 8, title_text_y, win.title, COL_TEXT_LIGHT, title_col);

  i32 by = win.y + (TITLEBAR_H - BTN_SIZE) / 2;

  // Icon metrics (centered in BTN_SIZE)
  i32 ico = BTN_SIZE - 8; // icon inset
  i32 ico2 = ico / 2;

  // Close button (rightmost)
  i32 close_x = win.x + win.w - BTN_SIZE - BTN_PAD;
  gfx::fill_rect(close_x, by, BTN_SIZE, BTN_SIZE, COL_CLOSE_BTN);
  // Draw X with two diagonal lines
  for (i32 k = 0; k < BTN_SIZE - ico; k++) {
    gfx::pixel(close_x + ico2 + k, by + ico2 + k, COL_TEXT_LIGHT);
    gfx::pixel(close_x + ico2 + k, by + BTN_SIZE - ico2 - 1 - k, COL_TEXT_LIGHT);
    // Thicken
    if (k > 0) {
      gfx::pixel(close_x + ico2 + k - 1, by + ico2 + k, COL_TEXT_LIGHT);
      gfx::pixel(close_x + ico2 + k, by + BTN_SIZE - ico2 - k, COL_TEXT_LIGHT);
    }
  }

  // Maximize button
  i32 max_x = close_x - BTN_SIZE - BTN_PAD;
  gfx::fill_rect(max_x, by, BTN_SIZE, BTN_SIZE, COL_BTN_BG);
  if (win.maximized) {
    gfx::rect(max_x + ico2 + 2, by + ico2, BTN_SIZE - ico - 2, BTN_SIZE - ico - 2, COL_TEXT_LIGHT);
    gfx::rect(max_x + ico2, by + ico2 + 2, BTN_SIZE - ico - 2, BTN_SIZE - ico - 2, COL_TEXT_LIGHT);
  } else {
    gfx::rect(max_x + ico2, by + ico2, BTN_SIZE - ico, BTN_SIZE - ico, COL_TEXT_LIGHT);
  }

  // Minimize button
  i32 min_x = max_x - BTN_SIZE - BTN_PAD;
  gfx::fill_rect(min_x, by, BTN_SIZE, BTN_SIZE, COL_BTN_BG);
  gfx::hline(min_x + ico2, by + BTN_SIZE - ico2 - 2, BTN_SIZE - ico, COL_TEXT_LIGHT);
  gfx::hline(min_x + ico2, by + BTN_SIZE - ico2 - 1, BTN_SIZE - ico, COL_TEXT_LIGHT);

  // Window body
  gfx::fill_rect(win.x, win.y + TITLEBAR_H, win.w, win.h - TITLEBAR_H,
                  COL_WIN_BODY);

  // Resize grip (bottom-right corner, only if not maximized)
  if (!win.maximized) {
    i32 gx = win.x + win.w - 2;
    i32 gy = win.y + win.h - 2;
    for (i32 d = 0; d < 3; d++) {
      i32 off = d * 3 + 2;
      for (i32 k = 0; k <= off && k < 10; k++)
        gfx::pixel(gx - off + k, gy - k, 0x00888888);
    }
  }
}

// Count total lines in text (for scrollbar)
i32 count_text_lines(const char *text, i32 cw, i32 fw) {
  i32 lines = 1;
  i32 tx = 0;
  for (const char *p = text; *p; p++) {
    if (*p == '\n') {
      lines++;
      tx = 0;
    } else {
      tx += fw;
      if (tx > cw) { lines++; tx = fw; }
    }
  }
  return lines;
}

// Scrollbar geometry for a window (used by drawing, hit-testing, and dragging)
constexpr i32 SB_WIDTH = 12;

struct ScrollGeom {
  i32 sb_x;       // scrollbar track left x (screen coords)
  i32 sb_y;       // scrollbar track top y (screen coords)
  i32 track_h;    // scrollbar track height
  i32 thumb_h;    // thumb height
  i32 thumb_y;    // thumb top y (screen coords)
  i32 max_scroll; // max scroll value
  bool visible;   // whether scrollbar is needed
};

ScrollGeom get_scroll_geom(Window &win) {
  ScrollGeom g{};
  i32 fh = gfx::font_h();

  i32 total_items = 0;
  i32 visible_items = 0;

  if (win.type == WIN_EXPLORER) {
    i32 pad = 6;
    i32 path_h = fh + 6;
    i32 ITEM_H = fh + 6;
    i32 ch = win.h - TITLEBAR_H - pad * 2 - path_h - 4;
    visible_items = ch / ITEM_H;
    if (visible_items < 1) visible_items = 1;
    total_items = explorer_item_count(win);
    g.sb_x = win.x + pad + (win.w - pad * 2) - SB_WIDTH;
    g.sb_y = win.y + TITLEBAR_H + pad + path_h + 4;
    g.track_h = ch;
  } else if (win.type == WIN_TEXTVIEW) {
    i32 fw = gfx::font_w();
    i32 LINE_H = fh + 2;
    i32 cw = win.w - 16;
    i32 ch = win.h - TITLEBAR_H - 16;
    visible_items = ch / LINE_H;
    if (visible_items < 1) visible_items = 1;
    total_items = count_text_lines(win.view_text, cw, fw);
    g.sb_x = win.x + 8 + cw - SB_WIDTH;
    g.sb_y = win.y + TITLEBAR_H + 8;
    g.track_h = ch;
  } else {
    return g;
  }

  g.max_scroll = total_items - visible_items;
  if (g.max_scroll < 0) g.max_scroll = 0;
  g.visible = (total_items > visible_items);

  if (g.visible && g.track_h > 0) {
    g.thumb_h = (visible_items * g.track_h) / total_items;
    if (g.thumb_h < 20) g.thumb_h = 20;
    i32 travel = g.track_h - g.thumb_h;
    g.thumb_y = g.sb_y;
    if (g.max_scroll > 0 && travel > 0)
      g.thumb_y = g.sb_y + (win.scroll * travel) / g.max_scroll;
  }

  return g;
}

// Scroll a window by delta lines (positive = scroll down)
void scroll_window(Window &win, i32 delta) {
  ScrollGeom g = get_scroll_geom(win);
  win.scroll += delta;
  if (win.scroll < 0) win.scroll = 0;
  if (win.scroll > g.max_scroll) win.scroll = g.max_scroll;
}

void draw_explorer(Window &win) {
  draw_window_frame(win);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 pad = 6;

  i32 cx = win.x + pad;
  i32 cy = win.y + TITLEBAR_H + pad;
  i32 cw = win.w - pad * 2;
  i32 ch = win.h - TITLEBAR_H - pad * 2;

  // Path bar
  i32 path_h = fh + 6;
  gfx::fill_rect(cx, cy, cw, path_h, 0x00FFFFFF);
  gfx::rect(cx, cy, cw, path_h, 0x00999999);
  gfx::draw_text(cx + 4, cy + 3, win.path, COL_TEXT_DARK, 0x00FFFFFF);
  cy += path_h + 4;
  ch -= path_h + 4;

  // Directory listing
  i32 dir_idx = fs::resolve(win.path);
  if (dir_idx < 0)
    return;
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir || dir->type != fs::NodeType::Directory)
    return;

  i32 ITEM_H = fh + 6;
  i32 icon_sz = fh - 4;
  if (icon_sz < 8) icon_sz = 8;
  i32 icon_pad = (ITEM_H - icon_sz) / 2;
  i32 text_pad = (ITEM_H - fh) / 2;
  bool has_dotdot = (dir_idx != 0);

  // Total item count
  i32 total_items = static_cast<i32>(dir->child_count);
  if (has_dotdot) total_items++;

  i32 visible_items = ch / ITEM_H;
  if (visible_items < 1) visible_items = 1;

  // Use shared scroll geometry
  ScrollGeom sg = get_scroll_geom(win);
  if (win.scroll > sg.max_scroll) win.scroll = sg.max_scroll;
  if (win.scroll < 0) win.scroll = 0;

  i32 content_w = sg.visible ? (cw - SB_WIDTH - 2) : cw;
  i32 text_x = cx + icon_sz + 12;

  if (sg.visible) {
    // Scrollbar track
    gfx::fill_rect(sg.sb_x, sg.sb_y, SB_WIDTH, sg.track_h, 0x00DDDDDD);
    // Scrollbar thumb
    gfx::fill_rect(sg.sb_x + 1, sg.thumb_y, SB_WIDTH - 2, sg.thumb_h, COL_SCROLLBAR);
    gfx::rect(sg.sb_x + 1, sg.thumb_y, SB_WIDTH - 2, sg.thumb_h, 0x00AAAAAA);
  }

  // Draw items starting from win.scroll
  i32 item_y = cy;
  for (i32 item_idx = win.scroll; item_idx < total_items; item_idx++) {
    if (item_y + ITEM_H > cy + ch)
      break;

    bool sel = (win.selected == item_idx);
    u32 bg = sel ? COL_SELECTION : COL_WIN_BODY;
    u32 fg = sel ? COL_TEXT_LIGHT : COL_TEXT_DARK;

    if (sel)
      gfx::fill_rect(cx, item_y, content_w, ITEM_H, COL_SELECTION);

    if (has_dotdot && item_idx == 0) {
      // ".." entry
      gfx::fill_rect(cx + 4, item_y + icon_pad, icon_sz, icon_sz, COL_FOLDER);
      gfx::draw_text(text_x, item_y + text_pad, "..", fg, bg);
    } else {
      i32 child_sel = has_dotdot ? (item_idx - 1) : item_idx;
      if (child_sel >= 0 && child_sel < static_cast<i32>(dir->child_count)) {
        i32 ci = dir->children[child_sel];
        const fs::Node *child = (ci >= 0) ? fs::get_node(ci) : nullptr;
        if (child && child->used) {
          // Icon
          u32 icon_col = (child->type == fs::NodeType::Directory) ? COL_FOLDER : COL_FILE_ICON;
          gfx::fill_rect(cx + 4, item_y + icon_pad, icon_sz, icon_sz, icon_col);
          // Name
          gfx::draw_text(text_x, item_y + text_pad, child->name, fg, bg);
          // Size for files
          if (child->type == fs::NodeType::File) {
            char sz[16];
            int_to_str(static_cast<i64>(child->content_len), sz);
            str::cat(sz, "B");
            gfx::draw_text(cx + content_w - fw * 6, item_y + text_pad, sz, fg, bg);
          }
        }
      }
    }

    item_y += ITEM_H;
  }
}

void draw_text_viewer(Window &win) {
  draw_window_frame(win);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 LINE_H = fh + 2;

  i32 cx = win.x + 8;
  i32 cy = win.y + TITLEBAR_H + 8;
  i32 cw = win.w - 16;
  i32 ch = win.h - TITLEBAR_H - 16;

  // Use shared scroll geometry
  ScrollGeom sg = get_scroll_geom(win);
  if (win.scroll > sg.max_scroll) win.scroll = sg.max_scroll;
  if (win.scroll < 0) win.scroll = 0;

  i32 visible_lines = ch / LINE_H;
  if (visible_lines < 1) visible_lines = 1;

  i32 text_w = sg.visible ? (cw - SB_WIDTH - 2) : cw;

  if (sg.visible) {
    gfx::fill_rect(sg.sb_x, sg.sb_y, SB_WIDTH, sg.track_h, 0x00DDDDDD);
    gfx::fill_rect(sg.sb_x + 1, sg.thumb_y, SB_WIDTH - 2, sg.thumb_h, COL_SCROLLBAR);
    gfx::rect(sg.sb_x + 1, sg.thumb_y, SB_WIDTH - 2, sg.thumb_h, 0x00AAAAAA);
  }

  // Render text with scroll offset
  const char *p = win.view_text;
  i32 line = 0;
  i32 tx = cx;
  i32 ty = cy;

  while (*p) {
    if (*p == '\n') {
      line++;
      tx = cx;
      if (line > win.scroll)
        ty += LINE_H;
    } else {
      if (line >= win.scroll && line < win.scroll + visible_lines) {
        if (tx + fw <= cx + text_w) {
          gfx::draw_char(tx, ty, *p, COL_TEXT_DARK, COL_WIN_BODY);
        }
      }
      tx += fw;
    }
    if (ty > cy + ch) break;
    p++;
  }
}

void draw_cursor() {
  for (i32 row = 0; row < CURSOR_H; row++) {
    for (i32 col = 0; col < CURSOR_W; col++) {
      u8 mask = static_cast<u8>(0x80 >> col);
      if (cursor_edge[row] & mask) {
        u32 c = (cursor_fill[row] & mask) ? 0x00FFFFFF : 0x00000000;
        gfx::pixel(mouse_x + col, mouse_y + row, c);
      }
    }
  }
}

void draw_start_menu() {
  if (!start_menu_open)
    return;

  i32 sh = static_cast<i32>(fb::height());
  i32 mx = 2;
  i32 my = sh - TASKBAR_H - menu_h_computed;

  // Background + border
  gfx::fill_rect(mx, my, MENU_W, menu_h_computed, COL_MENU_BG);
  gfx::rect(mx, my, MENU_W, menu_h_computed, 0x00555555);

  // Header bar
  i32 mfh = gfx::font_h();
  i32 mfw = gfx::font_w();
  i32 hdr_text_y = my + (MENU_HEADER_H - mfh) / 2;
  gfx::fill_rect(mx + 1, my + 1, MENU_W - 2, MENU_HEADER_H - 1, 0x00445577);
  gfx::draw_text(mx + 12, hdr_text_y, "OguzOS", COL_TEXT_LIGHT, 0x00445577);
  gfx::draw_text(mx + 12 + mfw * 7, hdr_text_y, "v1.0", 0x00AABBCC, 0x00445577);

  // Menu items
  i32 iy = my + MENU_HEADER_H + 1;

  for (i32 i = 0; i < menu_item_count; i++) {
    bool is_sep = (menu_labels[i][0] == '-');
    bool hovered = (start_menu_hover == i && !is_sep);

    if (is_sep) {
      gfx::hline(mx + 8, iy + MENU_ITEM_H / 2, MENU_W - 16, COL_MENU_SEP);
      iy += MENU_ITEM_H;
      continue;
    }

    u32 bg = hovered ? COL_MENU_HOVER : COL_MENU_BG;
    gfx::fill_rect(mx + 1, iy, MENU_W - 2, MENU_ITEM_H, bg);
    gfx::draw_text(mx + 12, iy + (MENU_ITEM_H - mfh) / 2, menu_labels[i],
                   COL_TEXT_LIGHT, bg);
    iy += MENU_ITEM_H;
  }
}

void render() {
  draw_desktop();
  for (i32 i = 0; i < window_count; i++) {
    if (!windows[i].visible || windows[i].minimized)
      continue;
    switch (windows[i].type) {
    case WIN_EXPLORER:
      draw_explorer(windows[i]);
      break;
    case WIN_TEXTVIEW:
      draw_text_viewer(windows[i]);
      break;
    case WIN_APP:
      draw_window_frame(windows[i]);
      if (windows[i].app && windows[i].app->on_draw) {
        if (try_enter() == 0) {
          windows[i].app->on_draw(
              windows[i].app_state, windows[i].x,
              windows[i].y + TITLEBAR_H, windows[i].w,
              windows[i].h - TITLEBAR_H);
          try_leave();
        } else {
          close_window(i);
          i--;
        }
      }
      break;
    }
  }
  draw_taskbar();
  draw_start_menu();
  draw_cursor();
}

// ── Input handling ──────────────────────────────────────────────────────────
void handle_menu_click(i32 item) {
  start_menu_open = false;
  start_menu_hover = -1;

  if (item < 0 || item >= menu_item_count)
    return;

  // Check if it's an app launch
  if (menu_app_ids[item]) {
    open_app(menu_app_ids[item]);
    return;
  }

  // Fixed menu items
  if (item == mi_explorer) {
    open_explorer("/");
  } else if (item == mi_about) {
    open_text_viewer("About OguzOS",
                     "OguzOS v1.0\n"
                     "A minimal ARM64 operating system\n\n"
                     "Features:\n"
                     "  - .ogz application framework\n"
                     "  - Window manager\n"
                     "  - File explorer\n"
                     "  - In-memory filesystem\n"
                     "  - Network stack (IPv4/ICMP/UDP)\n"
                     "  - DHCP client\n"
                     "  - Virtio drivers\n\n"
                     "Built with freestanding C++17\n"
                     "No standard library, no OS beneath.\n\n"
                     "Press Q to close this window.");
  } else if (item == mi_shutdown) {
    asm volatile("movz x0, #0x8400, lsl #16\n"
                 "movk x0, #0x0008\n"
                 "hvc #0\n" ::: "x0");
    for (;;)
      asm volatile("wfi");
  }
}

// Mouse-down: focus, drag start, resize start, selection (no destructive actions)
void handle_mouse_down(i32 x, i32 y) {
  i32 sh = static_cast<i32>(fb::height());

  // Press inside open start menu — just record, action on release
  if (start_menu_open) {
    i32 mx_menu = 2;
    i32 my_menu = sh - TASKBAR_H - menu_h_computed;
    if (x >= mx_menu && x < mx_menu + MENU_W && y >= my_menu + MENU_HEADER_H &&
        y < my_menu + menu_h_computed)
      return; // wait for mouse-up
    // Press outside menu — close it
    start_menu_open = false;
    start_menu_hover = -1;
  }

  // Taskbar — record, action on release
  if (y >= sh - TASKBAR_H)
    return;

  // Windows (back to front)
  for (i32 i = window_count - 1; i >= 0; i--) {
    Window &win = windows[i];
    if (!win.visible)
      continue;

    if (win.minimized)
      continue;

    // Check resize edges (not when maximized)
    if (!win.maximized) {
      bool on_right = (x >= win.x + win.w - RESIZE_GRIP &&
                       x <= win.x + win.w + 2 &&
                       y >= win.y + TITLEBAR_H && y <= win.y + win.h + 2);
      bool on_bottom = (x >= win.x &&
                        x <= win.x + win.w + 2 &&
                        y >= win.y + win.h - RESIZE_GRIP &&
                        y <= win.y + win.h + 2);

      if (on_right || on_bottom) {
        bring_to_front(i);
        resizing = true;
        resize_win = window_count - 1;
        resize_edge = (on_right ? 1 : 0) | (on_bottom ? 2 : 0);
        resize_start_x = x;
        resize_start_y = y;
        resize_orig_w = windows[resize_win].w;
        resize_orig_h = windows[resize_win].h;
        return;
      }
    }

    if (x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + win.h) {
      // Title bar buttons area — just focus, action on release
      i32 by = win.y + 3;
      i32 btn_left_edge = win.x + win.w - (BTN_SIZE + BTN_PAD) * 3;
      if (y >= by && y < by + BTN_SIZE && x >= btn_left_edge) {
        bring_to_front(i);
        return;
      }

      // Title bar → start drag (auto-unmaximize if dragging a maximized window)
      if (y < win.y + TITLEBAR_H) {
        bring_to_front(i);
        if (windows[window_count - 1].maximized) {
          // Unmaximize and reposition so cursor stays on title bar
          Window &mw = windows[window_count - 1];
          i32 old_w = mw.restore_w;
          toggle_maximize(window_count - 1);
          // Center the restored window under the cursor
          mw.x = x - old_w / 2;
          mw.y = y - TITLEBAR_H / 2;
          if (mw.x < 0)
            mw.x = 0;
          if (mw.y < 0)
            mw.y = 0;
        }
        dragging = true;
        drag_win = window_count - 1;
        drag_ox = x - windows[drag_win].x;
        drag_oy = y - windows[drag_win].y;
        return;
      }

      // Content area — focus + select
      bring_to_front(i);
      Window &w = windows[window_count - 1];

      // Check for scrollbar click
      ScrollGeom sg = get_scroll_geom(w);
      if (sg.visible &&
          x >= sg.sb_x && x < sg.sb_x + SB_WIDTH &&
          y >= sg.sb_y && y < sg.sb_y + sg.track_h) {
        scrollbar_dragging = true;
        scrollbar_drag_win = window_count - 1;
        scrollbar_drag_start_y = y;
        scrollbar_drag_start_scroll = w.scroll;
        return;
      }

      if (w.type == WIN_EXPLORER) {
        i32 exp_pad = 6;
        i32 exp_path_h = gfx::font_h() + 6;
        i32 exp_item_h = gfx::font_h() + 6;
        i32 list_top = w.y + TITLEBAR_H + exp_pad + exp_path_h + 4;
        i32 local_y = y - list_top;
        if (local_y >= 0) {
          i32 clicked_item = w.scroll + local_y / exp_item_h;
          i32 total = explorer_item_count(w);
          if (clicked_item >= 0 && clicked_item < total)
            w.selected = clicked_item;
        }
      } else if (w.type == WIN_APP && w.app && w.app->on_mouse_down) {
        i32 rx = x - w.x;
        i32 ry = y - (w.y + TITLEBAR_H);
        if (try_enter() == 0) {
          w.app->on_mouse_down(w.app_state, rx, ry, w.w, w.h - TITLEBAR_H);
          try_leave();
        }
      }
      return;
    }
  }
}

// Mouse-up: trigger actions (close, menu, taskbar)
void handle_mouse_up(i32 x, i32 y) {
  i32 sh = static_cast<i32>(fb::height());

  // Was it a drag? If the mouse moved far from press point, skip actions
  i32 dx = x - mouse_down_x;
  i32 dy = y - mouse_down_y;
  bool was_drag = (dx * dx + dy * dy > 64);

  if (was_drag)
    return;

  // Start menu item
  if (start_menu_open) {
    i32 mx_menu = 2;
    i32 my_menu = sh - TASKBAR_H - menu_h_computed;

    if (x >= mx_menu && x < mx_menu + MENU_W && y >= my_menu + MENU_HEADER_H &&
        y < my_menu + menu_h_computed) {
      i32 item = (y - my_menu - MENU_HEADER_H - 1) / MENU_ITEM_H;
      if (item >= 0 && item < menu_item_count) {
        bool is_sep = (menu_labels[item][0] == '-');
        if (!is_sep)
          handle_menu_click(item);
      }
      return;
    }
  }

  // Taskbar
  if (y >= sh - TASKBAR_H) {
    i32 fw2 = gfx::font_w();
    i32 oguz_w = fw2 * 6 + 16;
    i32 tab_w2 = fw2 * 10 + 12;
    if (x >= 2 && x <= oguz_w + 2) {
      start_menu_open = !start_menu_open;
      start_menu_hover = -1;
    } else {
      start_menu_open = false;
      i32 tx = oguz_w + 8;
      for (i32 i = 0; i < window_count; i++) {
        if (x >= tx && x < tx + tab_w2) {
          if (windows[i].minimized) {
            windows[i].minimized = false;
            bring_to_front(i);
          } else if (i == active_window) {
            minimize_window(i);
          } else {
            bring_to_front(i);
          }
          break;
        }
        tx += tab_w2 + 4;
      }
    }
    return;
  }

  // Window title bar buttons (release must match press location)
  for (i32 i = window_count - 1; i >= 0; i--) {
    Window &win = windows[i];
    if (!win.visible || win.minimized)
      continue;

    if (x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + win.h) {
      i32 by = win.y + 3;

      // Close button (rightmost)
      i32 close_x = win.x + win.w - BTN_SIZE - BTN_PAD;
      if (x >= close_x && x < close_x + BTN_SIZE &&
          y >= by && y < by + BTN_SIZE) {
        if (mouse_down_x >= close_x && mouse_down_x < close_x + BTN_SIZE &&
            mouse_down_y >= by && mouse_down_y < by + BTN_SIZE) {
          close_window(i);
        }
        return;
      }

      // Maximize button
      i32 max_x = close_x - BTN_SIZE - BTN_PAD;
      if (x >= max_x && x < max_x + BTN_SIZE &&
          y >= by && y < by + BTN_SIZE) {
        if (mouse_down_x >= max_x && mouse_down_x < max_x + BTN_SIZE &&
            mouse_down_y >= by && mouse_down_y < by + BTN_SIZE) {
          toggle_maximize(i);
        }
        return;
      }

      // Minimize button
      i32 min_x = max_x - BTN_SIZE - BTN_PAD;
      if (x >= min_x && x < min_x + BTN_SIZE &&
          y >= by && y < by + BTN_SIZE) {
        if (mouse_down_x >= min_x && mouse_down_x < min_x + BTN_SIZE &&
            mouse_down_y >= by && mouse_down_y < by + BTN_SIZE) {
          minimize_window(i);
        }
        return;
      }

      // Content area click → route to app
      if (y >= win.y + TITLEBAR_H && y < win.y + win.h) {
        if (win.type == WIN_APP && win.app && win.app->on_click) {
          i32 rx = x - win.x;
          i32 ry = y - (win.y + TITLEBAR_H);
          i32 content_w = win.w;
          i32 content_h = win.h - TITLEBAR_H;
          if (try_enter() == 0) {
            win.app->on_click(win.app_state, rx, ry, content_w, content_h);
            try_leave();
          } else {
            syslog::error("gui", "app crashed on click: %s", win.app->name);
            close_window(i);
          }
        }
      }

      return;
    }
  }
}

void handle_double_click(i32 x, i32 y) {
  if (active_window < 0)
    return;
  Window &win = windows[active_window];
  if (win.minimized)
    return;

  // Double-click title bar → toggle maximize
  if (x >= win.x && x < win.x + win.w &&
      y >= win.y && y < win.y + TITLEBAR_H) {
    toggle_maximize(active_window);
    return;
  }

  // Double-click explorer content → activate item
  if (win.type == WIN_EXPLORER) {
    i32 exp_pad = 6;
    i32 exp_path_h = gfx::font_h() + 6;
    i32 list_top = win.y + TITLEBAR_H + exp_pad + exp_path_h + 4;
    if (x >= win.x && x < win.x + win.w &&
        y >= list_top && y < win.y + win.h) {
      explorer_activate(win);
    }
  }
}

} // anonymous namespace

// ── Public entry point ──────────────────────────────────────────────────────
namespace gui {

void run() {
  window_count = 0;
  active_window = -1;
  mouse_x = 320;
  mouse_y = 240;
  dragging = false;
  resizing = false;
  scrollbar_dragging = false;
  prev_mouse_left = false;
  start_menu_open = false;
  start_menu_hover = -1;
  should_exit = false;

  gfx::init();
  build_menu();
  open_explorer("/");

  u64 last_click_time = 0;
  i32 last_click_x = 0, last_click_y = 0;
  bool needs_redraw = true;

  while (true) {
    // ── Mouse input ───────────────────────────────────────────────────
    i32 mx, my, mscroll;
    bool ml, mr;
    if (mouse::poll(mx, my, ml, mr, mscroll)) {
      if (mx != mouse_x || my != mouse_y) {
        needs_redraw = true;

        // Track start menu hover
        if (start_menu_open) {
          i32 sh = static_cast<i32>(fb::height());
          i32 menu_x = 2;
          i32 menu_y = sh - TASKBAR_H - menu_h_computed + MENU_HEADER_H + 1;
          if (mx >= menu_x && mx < menu_x + MENU_W && my >= menu_y &&
              my < menu_y + menu_item_count * MENU_ITEM_H) {
            start_menu_hover = (my - menu_y) / MENU_ITEM_H;
          } else {
            start_menu_hover = -1;
          }
        }
      }
      mouse_x = mx;
      mouse_y = my;

      // Drag (move)
      if (dragging && ml) {
        if (drag_win >= 0 && drag_win < window_count) {
          windows[drag_win].x = mouse_x - drag_ox;
          windows[drag_win].y = mouse_y - drag_oy;
          needs_redraw = true;
        }
      } else if (!ml) {
        dragging = false;
      }

      // Resize
      if (resizing && ml) {
        if (resize_win >= 0 && resize_win < window_count) {
          Window &rw = windows[resize_win];
          if (resize_edge & 1) { // right
            i32 new_w = resize_orig_w + (mouse_x - resize_start_x);
            if (new_w < MIN_WIN_W)
              new_w = MIN_WIN_W;
            rw.w = new_w;
          }
          if (resize_edge & 2) { // bottom
            i32 new_h = resize_orig_h + (mouse_y - resize_start_y);
            if (new_h < MIN_WIN_H)
              new_h = MIN_WIN_H;
            rw.h = new_h;
          }
          needs_redraw = true;
        }
      } else if (!ml) {
        resizing = false;
      }

      // Mouse-down edge: focus, drag start, selection
      if (ml && !prev_mouse_left) {
        mouse_down_x = mouse_x;
        mouse_down_y = mouse_y;
        handle_mouse_down(mouse_x, mouse_y);
        needs_redraw = true;
      }

      // Mouse-up edge: trigger actions (close, menu, taskbar)
      if (!ml && prev_mouse_left) {
        u64 cnt, freq;
        asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
        asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));

        bool is_double = false;
        if (freq > 0) {
          u64 elapsed_ms = (cnt - last_click_time) * 1000 / freq;
          i32 dx = mouse_x - last_click_x;
          i32 dy = mouse_y - last_click_y;
          if (elapsed_ms < 400 && dx * dx + dy * dy < 100)
            is_double = true;
        }

        if (is_double)
          handle_double_click(mouse_x, mouse_y);

        handle_mouse_up(mouse_x, mouse_y);

        last_click_time = cnt;
        last_click_x = mouse_x;
        last_click_y = mouse_y;
        needs_redraw = true;
      }

      // Scrollbar drag
      if (scrollbar_dragging && ml) {
        if (scrollbar_drag_win >= 0 && scrollbar_drag_win < window_count) {
          Window &sw = windows[scrollbar_drag_win];
          ScrollGeom sg = get_scroll_geom(sw);
          i32 travel = sg.track_h - sg.thumb_h;
          if (sg.visible && travel > 0 && sg.max_scroll > 0) {
            i32 dy = mouse_y - scrollbar_drag_start_y;
            i32 new_scroll = scrollbar_drag_start_scroll +
                             (dy * sg.max_scroll) / travel;
            if (new_scroll < 0) new_scroll = 0;
            if (new_scroll > sg.max_scroll) new_scroll = sg.max_scroll;
            sw.scroll = new_scroll;
            needs_redraw = true;
          }
        }
      } else if (!ml) {
        scrollbar_dragging = false;
      }

      // App mouse-move (drag) — only when no other drag is active
      if (ml && !dragging && !resizing && !scrollbar_dragging) {
        if (active_window >= 0 && active_window < window_count) {
          Window &aw = windows[active_window];
          if (aw.type == WIN_APP && aw.app && aw.app->on_mouse_move &&
              mouse_x >= aw.x && mouse_x < aw.x + aw.w &&
              mouse_y >= aw.y + TITLEBAR_H && mouse_y < aw.y + aw.h) {
            i32 rx = mouse_x - aw.x;
            i32 ry = mouse_y - (aw.y + TITLEBAR_H);
            if (try_enter() == 0) {
              aw.app->on_mouse_move(aw.app_state, rx, ry, aw.w,
                                    aw.h - TITLEBAR_H);
              try_leave();
              needs_redraw = true;
            }
          }
        }
      }

      // Scroll wheel
      if (mscroll != 0) {
        // Find window under cursor
        for (i32 i = window_count - 1; i >= 0; i--) {
          Window &sw = windows[i];
          if (!sw.visible || sw.minimized) continue;
          if (mouse_x >= sw.x && mouse_x < sw.x + sw.w &&
              mouse_y >= sw.y + TITLEBAR_H && mouse_y < sw.y + sw.h) {
            if (sw.type == WIN_EXPLORER || sw.type == WIN_TEXTVIEW) {
              scroll_window(sw, -mscroll * 3);
              needs_redraw = true;
            } else if (sw.type == WIN_APP && sw.app && sw.app->on_scroll) {
              if (try_enter() == 0) {
                sw.app->on_scroll(sw.app_state, mscroll);
                try_leave();
                needs_redraw = true;
              }
            }
            break;
          }
        }
      }

      prev_mouse_left = ml;
    }

    // ── Helper: handle arrow key ──────────────────────────────────────
    auto handle_arrow = [&](char dir) {
      if (active_window >= 0) {
        Window &w = windows[active_window];
        if (w.type == WIN_EXPLORER) {
          i32 total = explorer_item_count(w);
          if (dir == 'A' && w.selected > 0)
            w.selected--;
          else if (dir == 'B' && w.selected < total - 1)
            w.selected++;
          // Auto-scroll to keep selection visible
          i32 exp_fh = gfx::font_h();
          i32 exp_item_h = exp_fh + 6;
          i32 exp_ch = w.h - TITLEBAR_H - 12 - exp_fh - 6 - 4;
          i32 vis = exp_ch / exp_item_h;
          if (vis < 1) vis = 1;
          if (w.selected < w.scroll)
            w.scroll = w.selected;
          else if (w.selected >= w.scroll + vis)
            w.scroll = w.selected - vis + 1;
        } else if (w.type == WIN_APP && w.app && w.app->on_arrow) {
          if (try_enter() == 0) {
            w.app->on_arrow(w.app_state, dir);
            try_leave();
          } else {
            close_window(active_window);
          }
        }
      }
      needs_redraw = true;
    };

    // ── Helper: handle regular key ──────────────────────────────────
    auto handle_key = [&](char key) {
      if (key == '\t') {
        if (window_count > 1) {
          bring_to_front(0);
          needs_redraw = true;
        }
      } else if (active_window >= 0 &&
                 windows[active_window].type == WIN_APP) {
        Window &w = windows[active_window];
        syslog::debug("gui", "key 0x%x -> app '%s'",
                      static_cast<unsigned int>(static_cast<u8>(key)),
                      w.app ? w.app->name : "?");
        if (w.app && w.app->on_key) {
          if (try_enter() == 0) {
            bool consumed = w.app->on_key(w.app_state, key);
            try_leave();
            if (consumed)
              needs_redraw = true;
          } else {
            syslog::error("gui", "app crashed on key: %s", w.app->name);
            close_window(active_window);
            needs_redraw = true;
          }
        }
      } else if (key == '\r' || key == '\n') {
        if (active_window >= 0 &&
            windows[active_window].type == WIN_EXPLORER) {
          explorer_activate(windows[active_window]);
          needs_redraw = true;
        }
      } else if (key == 'q' || key == 'Q') {
        if (active_window >= 0 &&
            windows[active_window].type == WIN_TEXTVIEW) {
          close_window(active_window);
          needs_redraw = true;
        }
      }
    };

    // ── Keyboard input (UART serial) ─────────────────────────────────
    char key;
    if (uart_try_getc(key)) {
      if (key == 0x1B) {
        // Escape or arrow-key sequence
        bool is_seq = false;
        for (int wait = 0; wait < 10000; wait++) {
          char k2;
          if (uart_try_getc(k2)) {
            if (k2 == '[') {
              for (int w2 = 0; w2 < 10000; w2++) {
                char k3;
                if (uart_try_getc(k3)) {
                  is_seq = true;
                  handle_arrow(k3);
                  break;
                }
              }
            }
            break;
          }
        }
        if (!is_seq) {
          if (start_menu_open) {
            start_menu_open = false;
            start_menu_hover = -1;
            needs_redraw = true;
          }
        }
      } else {
        handle_key(key);
      }
    }

    // ── Keyboard input (virtio-keyboard from QEMU GUI window) ────────
    keyboard::poll();
    {
      char vk;
      while (keyboard::get_key(vk))
        handle_key(vk);
      char arrow;
      while (keyboard::get_arrow(arrow))
        handle_arrow(arrow);
    }

    // ── Exit check ─────────────────────────────────────────────────────
    if (should_exit)
      return;

    // ── Render ────────────────────────────────────────────────────────
    if (needs_redraw) {
      render();
      gfx::swap();
      needs_redraw = false;
    }

    // Yield to avoid spinning the CPU at full speed
    asm volatile("yield");
  }
}

i32 get_window_count() { return window_count; }

const char *get_window_title(i32 index) {
  if (index < 0 || index >= window_count)
    return nullptr;
  return windows[index].title;
}

bool is_window_active(i32 index) {
  if (index < 0 || index >= window_count)
    return false;
  return windows[index].active;
}

i32 get_window_type(i32 index) {
  if (index < 0 || index >= window_count)
    return -1;
  return static_cast<i32>(windows[index].type);
}

const char *get_window_app_id(i32 index) {
  if (index < 0 || index >= window_count)
    return nullptr;
  if (windows[index].type == WIN_APP && windows[index].app)
    return windows[index].app->id;
  return nullptr;
}

} // namespace gui
