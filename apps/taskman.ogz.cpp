#include "app.h"
#include "registry.h"

#ifdef USERSPACE
#include "userapi.h"
#else
#include "disk.h"
#include "fb.h"
#include "fs.h"
#include "graphics.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "netdev.h"
#include "string.h"
#include "syslog.h"
#endif

/*
 * taskman.ogz — OguzOS Task Manager
 *
 * System monitor displaying uptime, hardware status,
 * filesystem usage, network info, and open windows.
 */

namespace {

struct TaskManState {
  i32 scroll;
  u64 last_refresh; // timer counter at last refresh
};

static_assert(sizeof(TaskManState) <= 4096, "TaskManState exceeds app_state");

// ── Colour palette ──────────────────────────────────────────────────────────
constexpr u32 COL_BG = 0x001A1B26;
constexpr u32 COL_HEADER_BG = 0x00292E42;
constexpr u32 COL_SECTION = 0x007AA2F7;
constexpr u32 COL_LABEL = 0x00A9B1D6;
constexpr u32 COL_VALUE = 0x00C0CAF5;
constexpr u32 COL_GOOD = 0x009ECE6A;
constexpr u32 COL_WARN = 0x00E0AF68;
constexpr u32 COL_BAD = 0x00F7768E;
constexpr u32 COL_BAR_BG = 0x00363B54;
constexpr u32 COL_BAR_FG = 0x007AA2F7;
constexpr u32 COL_ACTIVE = 0x009ECE6A;
constexpr u32 COL_DIM = 0x00565F89;

constexpr i32 PAD = 6;

// Resolved at draw time via gfx::font_w()/font_h()
i32 CHAR_W = 10;
i32 LINE_H = 24;

// ── Helpers ─────────────────────────────────────────────────────────────────

void int_to_str(u64 val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  char tmp[20];
  i32 i = 0;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  i32 j = 0;
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

void get_uptime(u64 &secs, u64 &ms) {
  u64 cnt, freq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (freq == 0)
    freq = 1;
  secs = cnt / freq;
  ms = (cnt % freq) * 1000 / freq;
}

// Draw a label:value pair, returns next y
i32 draw_kv(i32 x, i32 y, i32 max_y, const char *label, const char *value,
            u32 val_color = COL_VALUE) {
  if (y + LINE_H > max_y)
    return y;
  gfx::draw_text(x, y, label, COL_LABEL, COL_BG);
  i32 vx = x + static_cast<i32>(str::len(label)) * CHAR_W;
  gfx::draw_text(vx, y, value, val_color, COL_BG);
  return y + LINE_H;
}

// Draw section header
i32 draw_section(i32 x, i32 y, i32 w, i32 max_y, const char *title) {
  if (y + LINE_H + 2 > max_y)
    return y;
  y += 2;
  gfx::fill_rect(x, y, w, LINE_H, COL_HEADER_BG);
  gfx::draw_text(x + 4, y + 1, title, COL_SECTION, COL_HEADER_BG);
  return y + LINE_H + 2;
}

// Draw a progress bar
i32 draw_bar(i32 x, i32 y, i32 w, i32 max_y, const char *label, u64 used,
             u64 total) {
  if (y + LINE_H > max_y)
    return y;

  gfx::draw_text(x, y, label, COL_LABEL, COL_BG);
  i32 label_w = static_cast<i32>(str::len(label)) * CHAR_W;

  i32 bar_x = x + label_w + 4;
  i32 bar_w = w - label_w - 80;
  if (bar_w < 20)
    bar_w = 20;
  i32 bar_h = 8;
  i32 bar_y = y + 1;

  gfx::fill_rect(bar_x, bar_y, bar_w, bar_h, COL_BAR_BG);
  if (total > 0) {
    i32 fill = static_cast<i32>(used * static_cast<u64>(bar_w) / total);
    if (fill > bar_w)
      fill = bar_w;
    u32 fill_col = COL_BAR_FG;
    u64 pct = used * 100 / total;
    if (pct > 80)
      fill_col = COL_BAD;
    else if (pct > 60)
      fill_col = COL_WARN;
    gfx::fill_rect(bar_x, bar_y, fill, bar_h, fill_col);
  }

  // Percentage text
  char pct_buf[8];
  if (total > 0) {
    int_to_str(used * 100 / total, pct_buf);
    str::cat(pct_buf, "%");
  } else {
    str::ncpy(pct_buf, "N/A", 7);
  }
  gfx::draw_text(bar_x + bar_w + 6, y, pct_buf, COL_VALUE, COL_BG);

  return y + LINE_H;
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void taskman_open(u8 *state) {
  auto *s = reinterpret_cast<TaskManState *>(state);
  s->scroll = 0;
  s->last_refresh = 0;
  syslog::info("taskman", "task manager opened");
}

void taskman_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<TaskManState *>(state);

  // Background
  CHAR_W = gfx::font_w();
  LINE_H = gfx::font_h() + 2;

  gfx::fill_rect(cx, cy, cw, ch, COL_BG);

  i32 x = cx + PAD;
  i32 y = cy + PAD;
  i32 w = cw - PAD * 2;
  i32 max_y = cy + ch - PAD;
  char buf[64];

  // ── System ────────────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " SYSTEM");

  // Uptime
  u64 secs, ms;
  get_uptime(secs, ms);
  u64 hr = secs / 3600;
  u64 min = (secs / 60) % 60;
  u64 sec = secs % 60;
  buf[0] = '0' + static_cast<char>(hr / 10);
  buf[1] = '0' + static_cast<char>(hr % 10);
  buf[2] = ':';
  buf[3] = '0' + static_cast<char>(min / 10);
  buf[4] = '0' + static_cast<char>(min % 10);
  buf[5] = ':';
  buf[6] = '0' + static_cast<char>(sec / 10);
  buf[7] = '0' + static_cast<char>(sec % 10);
  buf[8] = '\0';
  y = draw_kv(x + 4, y, max_y, "Uptime:    ", buf, COL_GOOD);

  // CPU
  u64 freq;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  int_to_str(freq / 1000000, buf);
  str::cat(buf, " MHz timer");
  y = draw_kv(x + 4, y, max_y, "CPU:       ", "ARM Cortex-A72");
  y = draw_kv(x + 4, y, max_y, "Timer:     ", buf, COL_DIM);

  // RAM (fixed for QEMU virt)
  y = draw_kv(x + 4, y, max_y, "RAM:       ", "512 MB");

  // ── Display ───────────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " DISPLAY");

  if (fb::is_available()) {
    int_to_str(fb::width(), buf);
    str::cat(buf, "x");
    char h[8];
    int_to_str(fb::height(), h);
    str::cat(buf, h);
    str::cat(buf, " XRGB8888");
    y = draw_kv(x + 4, y, max_y, "Resolution: ", buf, COL_GOOD);
  } else {
    y = draw_kv(x + 4, y, max_y, "Framebuffer:", " not available", COL_BAD);
  }

  // ── Storage ───────────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " STORAGE");

  if (disk::is_available()) {
    u64 cap_kb = disk::get_capacity() / 2;
    int_to_str(cap_kb, buf);
    str::cat(buf, " KB");
    y = draw_kv(x + 4, y, max_y, "Disk:      ", buf);
  } else {
    y = draw_kv(x + 4, y, max_y, "Disk:      ", "not available", COL_BAD);
  }

  // Filesystem usage
  i32 used_nodes = 0;
  i32 file_count = 0;
  i32 dir_count = 0;
  u64 total_bytes = 0;
  for (i32 i = 0; i < static_cast<i32>(fs::MAX_NODES); i++) {
    const fs::Node *n = fs::get_node(i);
    if (n) {
      used_nodes++;
      if (n->type == fs::NodeType::File) {
        file_count++;
        total_bytes += n->content_len;
      } else {
        dir_count++;
      }
    }
  }

  int_to_str(static_cast<u64>(file_count), buf);
  str::cat(buf, " files, ");
  char d[8];
  int_to_str(static_cast<u64>(dir_count), d);
  str::cat(buf, d);
  str::cat(buf, " dirs");
  y = draw_kv(x + 4, y, max_y, "FS:        ", buf);

  y = draw_bar(x + 4, y, w - 8, max_y, "Nodes:  ",
               static_cast<u64>(used_nodes), fs::MAX_NODES);

  int_to_str(total_bytes, buf);
  str::cat(buf, " / ");
  char cap[16];
  int_to_str(static_cast<u64>(file_count) * fs::MAX_CONTENT, cap);
  str::cat(buf, cap);
  str::cat(buf, " B");
  y = draw_kv(x + 4, y, max_y, "Data:      ", buf, COL_DIM);

  // ── Network ───────────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " NETWORK");

  if (netdev::is_available()) {
    u8 mac[6];
    netdev::get_mac(mac);
    // Format MAC
    const char hex[] = "0123456789abcdef";
    char mac_str[18];
    for (i32 i = 0; i < 6; i++) {
      mac_str[i * 3] = hex[mac[i] >> 4];
      mac_str[i * 3 + 1] = hex[mac[i] & 0xF];
      mac_str[i * 3 + 2] = (i < 5) ? ':' : '\0';
    }
    y = draw_kv(x + 4, y, max_y, "MAC:       ", mac_str);

    if (net::is_available()) {
      y = draw_kv(x + 4, y, max_y, "IPv4:      ", "DHCP configured",
                  COL_GOOD);
    } else {
      y = draw_kv(x + 4, y, max_y, "IPv4:      ", "not configured", COL_WARN);
    }
  } else {
    y = draw_kv(x + 4, y, max_y, "Network:   ", "not available", COL_BAD);
  }

  // ── Input Devices ─────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " INPUT");

  y = draw_kv(x + 4, y, max_y, "Keyboard:  ",
              keyboard::is_available() ? "virtio-keyboard" : "not found",
              keyboard::is_available() ? COL_GOOD : COL_BAD);
  y = draw_kv(x + 4, y, max_y, "Mouse:     ",
              mouse::is_available() ? "virtio-tablet" : "not found",
              mouse::is_available() ? COL_GOOD : COL_BAD);

  // ── Running Apps ────────────────────────────────────────────────────
  y = draw_section(x, y, w, max_y, " RUNNING APPS");

  i32 wcount = gui::get_window_count();
  i32 app_count = 0;

  // Count and list running .ogz apps
  for (i32 i = 0; i < wcount; i++) {
    if (gui::get_window_type(i) == gui::WTYPE_APP)
      app_count++;
  }

  int_to_str(static_cast<u64>(app_count), buf);
  str::cat(buf, " running");
  y = draw_kv(x + 4, y, max_y, "Apps:      ", buf);

  // Table header
  if (y + LINE_H <= max_y) {
    gfx::fill_rect(x + 4, y, w - 8, LINE_H, COL_HEADER_BG);
    i32 col_pid = x + 8;
    i32 col_name = x + CHAR_W * 5;
    i32 col_type = x + CHAR_W * 22;
    i32 col_status = x + CHAR_W * 36;
    gfx::draw_text(col_pid, y + 1, "PID", COL_DIM, COL_HEADER_BG);
    gfx::draw_text(col_name, y + 1, "NAME", COL_DIM, COL_HEADER_BG);
    gfx::draw_text(col_type, y + 1, "TYPE", COL_DIM, COL_HEADER_BG);
    gfx::draw_text(col_status, y + 1, "STATUS", COL_DIM, COL_HEADER_BG);
    y += LINE_H;
  }

  for (i32 i = 0; i < wcount && y + LINE_H <= max_y; i++) {
    const char *title = gui::get_window_title(i);
    if (!title)
      continue;

    // Copy title immediately — further syscalls overwrite the transfer buffer
    char name[20];
    str::ncpy(name, title, 19);

    bool active = gui::is_window_active(i);
    i32 wtype = gui::get_window_type(i);
    const char *app_id = gui::get_window_app_id(i);

    u32 row_bg = (i % 2 == 0) ? COL_BG : 0x001F2030;
    gfx::fill_rect(x + 4, y, w - 8, LINE_H, row_bg);

    i32 col_pid = x + 8;
    i32 col_name = x + CHAR_W * 5;
    i32 col_type = x + CHAR_W * 22;
    i32 col_status = x + CHAR_W * 36;

    // PID column
    char pid[4];
    int_to_str(static_cast<u64>(i), pid);
    gfx::draw_text(col_pid, y + 1, pid, COL_VALUE, row_bg);

    // Name column
    gfx::draw_text(col_name, y + 1, name, COL_VALUE, row_bg);

    // Type column
    const char *type_str = "system";
    u32 type_col = COL_DIM;
    if (wtype == gui::WTYPE_APP) {
      type_str = app_id ? app_id : "app";
      type_col = COL_SECTION;
    } else if (wtype == gui::WTYPE_EXPLORER) {
      type_str = "explorer";
      type_col = COL_WARN;
    } else if (wtype == gui::WTYPE_TEXTVIEW) {
      type_str = "viewer";
      type_col = COL_LABEL;
    }
    gfx::draw_text(col_type, y + 1, type_str, type_col, row_bg);

    // Status column
    const char *status = active ? "active" : "idle";
    u32 status_col = active ? COL_ACTIVE : COL_DIM;
    gfx::draw_text(col_status, y + 1, status, status_col, row_bg);

    y += LINE_H;
  }

  (void)s;
}

bool taskman_key(u8 *, char) {
  // No interactive keys for now — display-only
  return false;
}

void taskman_arrow(u8 *, char) {}

void taskman_close(u8 *) {
  syslog::info("taskman", "task manager closed");
}

const OgzApp taskman_app = {
    "Task Manager",    // name
    "taskman.ogz",     // id
    560,               // default_w
    640,               // default_h
    taskman_open,      // on_open
    taskman_draw,      // on_draw
    taskman_key,       // on_key
    taskman_arrow,     // on_arrow
    taskman_close,     // on_close
    nullptr,           // on_click
    nullptr,           // on_scroll
    nullptr,           // on_mouse_down
    nullptr,           // on_mouse_move
    nullptr,           // on_open_file
};

} // anonymous namespace

namespace apps {
void register_taskman() { register_app(&taskman_app); }
} // namespace apps
