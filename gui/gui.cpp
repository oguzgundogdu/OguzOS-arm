#include "gui.h"
#include "app.h"
#include "exception.h"
#include "fb.h"
#include "fs.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "registry.h"
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

constexpr i32 TASKBAR_H = 28;
constexpr i32 TITLEBAR_H = 20;

// ── Window types ────────────────────────────────────────────────────────────
enum WinType { WIN_EXPLORER, WIN_TEXTVIEW, WIN_APP };

struct Window {
  i32 x, y, w, h;
  char title[64];
  bool visible;
  bool active;
  WinType type;

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

// Start menu state
bool start_menu_open = false;
i32 start_menu_hover = -1;

constexpr i32 MENU_W = 180;
constexpr i32 MENU_ITEM_H = 24;
constexpr i32 MENU_HEADER_H = 28;

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
  i32 idx = create_window("File Explorer", 60, 40, 400, 320, WIN_EXPLORER);
  if (idx >= 0)
    str::ncpy(windows[idx].path, path, 255);
}

void open_text_viewer(const char *title, const char *text) {
  i32 idx = create_window(title, 100, 60, 380, 280, WIN_TEXTVIEW);
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
void draw_desktop() { gfx::clear(COL_DESKTOP); }

void draw_taskbar() {
  i32 sh = static_cast<i32>(fb::height());

  i32 sw = static_cast<i32>(fb::width());
  gfx::fill_rect(0, sh - TASKBAR_H, sw, TASKBAR_H, COL_TASKBAR);
  gfx::hline(0, sh - TASKBAR_H, sw, 0x00444444);

  // "OguzOS" button
  gfx::fill_rect(2, sh - TASKBAR_H + 3, 60, TASKBAR_H - 6, 0x00445577);
  gfx::draw_text(10, sh - TASKBAR_H + 9, "OguzOS", COL_TASK_TEXT, 0x00445577);

  // Window list on taskbar
  i32 tx = 68;
  for (i32 i = 0; i < window_count; i++) {
    u32 bg = (i == active_window) ? 0x00556688 : 0x00444444;
    gfx::fill_rect(tx, sh - TASKBAR_H + 3, 80, TASKBAR_H - 6, bg);
    // Truncate title to ~9 chars
    char short_title[12];
    str::ncpy(short_title, windows[i].title, 10);
    short_title[10] = '\0';
    gfx::draw_text(tx + 4, sh - TASKBAR_H + 9, short_title, COL_TASK_TEXT, bg);
    tx += 84;
  }

  // Clock (uptime-based HH:MM)
  u64 cnt, freq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (freq == 0)
    freq = 1;
  u64 sec = cnt / freq;
  u64 min = (sec / 60) % 60;
  u64 hr = (sec / 3600) % 24;

  char ts[6];
  ts[0] = '0' + static_cast<char>(hr / 10);
  ts[1] = '0' + static_cast<char>(hr % 10);
  ts[2] = ':';
  ts[3] = '0' + static_cast<char>(min / 10);
  ts[4] = '0' + static_cast<char>(min % 10);
  ts[5] = '\0';
  gfx::draw_text(sw - 48, sh - TASKBAR_H + 9, ts, COL_TASK_TEXT, COL_TASKBAR);
}

void draw_window_frame(Window &win) {
  u32 title_col = win.active ? COL_WIN_TITLE : COL_WIN_INACTIVE;

  // Shadow
  gfx::fill_rect(win.x + 3, win.y + 3, win.w, win.h, 0x00222222);

  // Border
  gfx::rect(win.x - 1, win.y - 1, win.w + 2, win.h + 2, COL_WIN_BORDER);

  // Title bar
  gfx::fill_rect(win.x, win.y, win.w, TITLEBAR_H, title_col);
  gfx::draw_text(win.x + 6, win.y + 6, win.title, COL_TEXT_LIGHT, title_col);

  // Close button
  i32 cbx = win.x + win.w - 18;
  i32 cby = win.y + 3;
  gfx::fill_rect(cbx, cby, 14, 14, COL_CLOSE_BTN);
  gfx::draw_char(cbx + 3, cby + 3, 'X', COL_TEXT_LIGHT, COL_CLOSE_BTN);

  // Window body
  gfx::fill_rect(win.x, win.y + TITLEBAR_H, win.w, win.h - TITLEBAR_H,
                  COL_WIN_BODY);
}

void draw_explorer(Window &win) {
  draw_window_frame(win);

  i32 cx = win.x + 4;
  i32 cy = win.y + TITLEBAR_H + 4;
  i32 cw = win.w - 8;
  i32 ch = win.h - TITLEBAR_H - 8;

  // Path bar
  gfx::fill_rect(cx, cy, cw, 14, 0x00FFFFFF);
  gfx::rect(cx, cy, cw, 14, 0x00999999);
  gfx::draw_text(cx + 4, cy + 3, win.path, COL_TEXT_DARK, 0x00FFFFFF);
  cy += 18;
  ch -= 18;

  // Directory listing
  i32 dir_idx = fs::resolve(win.path);
  if (dir_idx < 0)
    return;
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir || dir->type != fs::NodeType::Directory)
    return;

  constexpr i32 ITEM_H = 16;
  i32 item_y = cy;
  i32 item_idx = 0;
  bool has_dotdot = (dir_idx != 0);

  // ".." entry
  if (has_dotdot) {
    bool sel = (win.selected == item_idx);
    u32 bg = sel ? COL_SELECTION : COL_WIN_BODY;
    u32 fg = sel ? COL_TEXT_LIGHT : COL_TEXT_DARK;
    if (sel)
      gfx::fill_rect(cx, item_y, cw, ITEM_H, COL_SELECTION);
    gfx::fill_rect(cx + 4, item_y + 3, 10, 10, COL_FOLDER);
    gfx::draw_text(cx + 18, item_y + 4, "..", fg, bg);
    item_y += ITEM_H;
    item_idx++;
  }

  // Children
  for (usize i = 0; i < dir->child_count; i++) {
    if (item_y + ITEM_H > cy + ch)
      break;

    i32 ci = dir->children[i];
    if (ci < 0)
      continue;
    const fs::Node *child = fs::get_node(ci);
    if (!child || !child->used)
      continue;

    bool sel = (win.selected == item_idx);
    u32 bg = sel ? COL_SELECTION : COL_WIN_BODY;
    u32 fg = sel ? COL_TEXT_LIGHT : COL_TEXT_DARK;

    if (sel)
      gfx::fill_rect(cx, item_y, cw, ITEM_H, COL_SELECTION);

    // Icon
    u32 icon_col =
        (child->type == fs::NodeType::Directory) ? COL_FOLDER : COL_FILE_ICON;
    gfx::fill_rect(cx + 4, item_y + 3, 10, 10, icon_col);

    // Name
    gfx::draw_text(cx + 18, item_y + 4, child->name, fg, bg);

    // Size for files
    if (child->type == fs::NodeType::File) {
      char sz[16];
      int_to_str(static_cast<i64>(child->content_len), sz);
      str::cat(sz, "B");
      gfx::draw_text(cx + cw - 56, item_y + 4, sz, fg, bg);
    }

    item_y += ITEM_H;
    item_idx++;
  }
}

void draw_text_viewer(Window &win) {
  draw_window_frame(win);

  i32 cx = win.x + 8;
  i32 cy = win.y + TITLEBAR_H + 8;
  i32 cw = win.w - 16;
  i32 max_y = win.y + win.h - 8;

  const char *p = win.view_text;
  i32 tx = cx;
  i32 ty = cy;

  while (*p && ty + 8 <= max_y) {
    if (*p == '\n') {
      ty += 10;
      tx = cx;
    } else {
      if (tx + 8 <= cx + cw) {
        gfx::draw_char(tx, ty, *p, COL_TEXT_DARK, COL_WIN_BODY);
        tx += 8;
      }
    }
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
  gfx::fill_rect(mx + 1, my + 1, MENU_W - 2, MENU_HEADER_H - 1, 0x00445577);
  gfx::draw_text(mx + 12, my + 10, "OguzOS", COL_TEXT_LIGHT, 0x00445577);
  gfx::draw_text(mx + 68, my + 10, "v1.0", 0x00AABBCC, 0x00445577);

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
    gfx::draw_text(mx + 12, iy + 8, menu_labels[i], COL_TEXT_LIGHT, bg);
    iy += MENU_ITEM_H;
  }
}

void render() {
  draw_desktop();
  for (i32 i = 0; i < window_count; i++) {
    if (!windows[i].visible)
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

void handle_click(i32 x, i32 y) {
  i32 sh = static_cast<i32>(fb::height());

  // Start menu click handling
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
    // Clicked outside menu — close it
    start_menu_open = false;
    start_menu_hover = -1;
  }

  // Taskbar
  if (y >= sh - TASKBAR_H) {
    if (x >= 2 && x <= 62) {
      start_menu_open = !start_menu_open;
      start_menu_hover = -1;
    } else {
      start_menu_open = false;
      // Click on taskbar window buttons
      i32 tx = 68;
      for (i32 i = 0; i < window_count; i++) {
        if (x >= tx && x < tx + 80) {
          bring_to_front(i);
          break;
        }
        tx += 84;
      }
    }
    return;
  }

  // Windows (back to front)
  for (i32 i = window_count - 1; i >= 0; i--) {
    Window &win = windows[i];
    if (!win.visible)
      continue;

    if (x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + win.h) {
      // Close button
      i32 cbx = win.x + win.w - 18;
      i32 cby = win.y + 3;
      if (x >= cbx && x < cbx + 14 && y >= cby && y < cby + 14) {
        close_window(i);
        return;
      }

      // Title bar → drag
      if (y < win.y + TITLEBAR_H) {
        bring_to_front(i);
        dragging = true;
        drag_win = window_count - 1; // window moved to front
        drag_ox = x - windows[drag_win].x;
        drag_oy = y - windows[drag_win].y;
        return;
      }

      // Content area
      bring_to_front(i);
      Window &w = windows[window_count - 1];

      if (w.type == WIN_EXPLORER) {
        i32 local_y = y - w.y - TITLEBAR_H - 22; // after path bar
        if (local_y >= 0) {
          i32 clicked_item = local_y / 16;
          i32 total = explorer_item_count(w);
          if (clicked_item >= 0 && clicked_item < total)
            w.selected = clicked_item;
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
  if (win.type != WIN_EXPLORER)
    return;
  if (x >= win.x && x < win.x + win.w &&
      y >= win.y + TITLEBAR_H + 22 && y < win.y + win.h) {
    explorer_activate(win);
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
    i32 mx, my;
    bool ml, mr;
    if (mouse::poll(mx, my, ml, mr)) {
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

      // Drag
      if (dragging && ml) {
        if (drag_win >= 0 && drag_win < window_count) {
          windows[drag_win].x = mouse_x - drag_ox;
          windows[drag_win].y = mouse_y - drag_oy;
          needs_redraw = true;
        }
      } else {
        dragging = false;
      }

      // Click on press edge
      if (ml && !prev_mouse_left) {
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
        else
          handle_click(mouse_x, mouse_y);

        last_click_time = cnt;
        last_click_x = mouse_x;
        last_click_y = mouse_y;
        needs_redraw = true;
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
