#include "app.h"
#include "registry.h"
#ifdef USERSPACE
#include "userapi.h"
#else
#include "fs.h"
#include "graphics.h"
#include "string.h"
#include "syslog.h"
#endif

/*
 * notepad.ogz — OguzOS Notepad
 *
 * Features:
 *   - Type to insert, arrow keys to move cursor
 *   - Backspace / Delete to remove characters
 *   - Ctrl+S = Save, Ctrl+W = Save As
 *   - Ctrl+A = Select All
 *   - Home / End = start/end of line
 *   - Text selection with Shift+Arrow and click-to-position
 *   - Status bar with line/col, filename, modified indicator
 *   - Save As dialog for new files or saving to a new name
 */

namespace {

// ── State ───────────────────────────────────────────────────────────────────
constexpr i32 TEXT_MAX = 3800;

struct NotepadState {
  char text[TEXT_MAX];
  i32 len;
  i32 cursor;
  i32 scroll_y;       // line offset for vertical scrolling
  char filepath[128]; // full path e.g. "/home/Desktop/notes.txt"
  char filename[64];  // just the name e.g. "notes.txt"
  bool dirty;         // unsaved changes
  i32 sel_start;      // selection anchor (-1 = no selection)
  // Save-As dialog
  bool saveas_open;
  char saveas_buf[64];
  i32 saveas_cursor;
};

static_assert(sizeof(NotepadState) <= 4096, "NotepadState exceeds app_state");

// ── Colours ─────────────────────────────────────────────────────────────────
constexpr u32 COL_BG = 0x00FFFFFF;
constexpr u32 COL_TEXT = 0x00202020;
constexpr u32 COL_CURSOR = 0x00000000;
constexpr u32 COL_LINENUM_BG = 0x00E8E8E8;
constexpr u32 COL_LINENUM = 0x00999999;
constexpr u32 COL_STATUS = 0x00E0E0E0;
constexpr u32 COL_STATUS_TEXT = 0x00444444;
constexpr u32 COL_SEL = 0x003399FF;
constexpr u32 COL_SEL_TEXT = 0x00FFFFFF;
constexpr u32 COL_DIALOG_BG = 0x00F0F0F0;
constexpr u32 COL_DIALOG_TITLE = 0x004488CC;

// ── Helpers ─────────────────────────────────────────────────────────────────
i32 count_lines(const char *text, i32 pos) {
  i32 lines = 0;
  for (i32 i = 0; i < pos; i++)
    if (text[i] == '\n') lines++;
  return lines;
}

i32 cursor_col(const char *text, i32 pos) {
  i32 col = 0;
  for (i32 i = pos - 1; i >= 0; i--) {
    if (text[i] == '\n') break;
    col++;
  }
  return col;
}

i32 line_start(const char *text, i32 len, i32 line) {
  if (line == 0) return 0;
  i32 l = 0;
  for (i32 i = 0; i < len; i++) {
    if (text[i] == '\n') {
      l++;
      if (l == line) return i + 1;
    }
  }
  return len;
}

i32 line_end(const char *text, i32 len, i32 line) {
  i32 start = line_start(text, len, line);
  for (i32 i = start; i < len; i++) {
    if (text[i] == '\n') return i;
  }
  return len;
}

i32 total_lines(const char *text, i32 len) {
  return count_lines(text, len) + 1;
}

// Selection range (ordered): returns false if no selection
bool sel_range(NotepadState *s, i32 &lo, i32 &hi) {
  if (s->sel_start < 0 || s->sel_start == s->cursor) return false;
  lo = s->sel_start < s->cursor ? s->sel_start : s->cursor;
  hi = s->sel_start > s->cursor ? s->sel_start : s->cursor;
  return true;
}

void delete_range(NotepadState *s, i32 lo, i32 hi) {
  i32 del = hi - lo;
  if (del <= 0) return;
  for (i32 i = lo; i < s->len - del; i++)
    s->text[i] = s->text[i + del];
  s->len -= del;
  s->text[s->len] = '\0';
  s->cursor = lo;
  s->sel_start = -1;
  s->dirty = true;
}

void clear_selection(NotepadState *s) {
  s->sel_start = -1;
}

void append_int(char *dst, i32 val) {
  char tmp[12];
  i32 i = 0;
  if (val == 0) { tmp[i++] = '0'; }
  else { while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; } }
  usize len = str::len(dst);
  for (i32 j = i - 1; j >= 0; j--)
    dst[len++] = tmp[j];
  dst[len] = '\0';
}

// Extract directory and filename from a full path
void split_path(const char *path, char *dir, usize dir_size, char *name, usize name_size) {
  const char *last_slash = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/') last_slash = p;
  }
  if (last_slash == path) {
    // root directory
    str::ncpy(dir, "/", dir_size - 1);
    str::ncpy(name, path + 1, name_size - 1);
  } else {
    usize dlen = static_cast<usize>(last_slash - path);
    if (dlen >= dir_size) dlen = dir_size - 1;
    str::memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    str::ncpy(name, last_slash + 1, name_size - 1);
  }
}

// Save file to the filesystem
bool save_file(NotepadState *s) {
  if (!s->filepath[0]) return false;

  char dir[128], name[64];
  split_path(s->filepath, dir, sizeof(dir), name, sizeof(name));

  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));
  fs::cd(dir);
  fs::touch(name); // ensure file exists
  bool ok = fs::write(name, s->text);
  if (ok) {
    fs::sync_to_disk();
    s->dirty = false;
    syslog::info("notepad", "saved: %s", s->filepath);
  }
  fs::cd(old_cwd);
  return ok;
}

// ── Callbacks ───────────────────────────────────────────────────────────────
void notepad_open(u8 *state) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  s->text[0] = '\0';
  s->len = 0;
  s->cursor = 0;
  s->scroll_y = 0;
  s->filepath[0] = '\0';
  s->filename[0] = '\0';
  s->dirty = false;
  s->sel_start = -1;
  s->saveas_open = false;
  s->saveas_buf[0] = '\0';
  s->saveas_cursor = 0;
}

void notepad_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 LINE_H = fh + 2;
  i32 LINENUM_W = fw * 4;
  i32 STATUS_H = fh + 6;

  // ── Status bar ──
  i32 status_y = cy + ch - STATUS_H;
  gfx::fill_rect(cx, status_y, cw, STATUS_H, COL_STATUS);

  char status[128];
  status[0] = '\0';

  // Filename or "Untitled"
  if (s->filename[0]) {
    str::cat(status, s->filename);
  } else {
    str::cat(status, "Untitled");
  }
  if (s->dirty)
    str::cat(status, " *");

  str::cat(status, "  |  Ln ");
  append_int(status, count_lines(s->text, s->cursor) + 1);
  str::cat(status, ", Col ");
  append_int(status, cursor_col(s->text, s->cursor) + 1);
  str::cat(status, "  |  ");
  append_int(status, s->len);
  str::cat(status, " chars");

  // Keyboard shortcuts hint on the right side
  const char *hint = "Ctrl+S Save  Ctrl+W Save As";

  gfx::draw_text(cx + 4, status_y + 3, status, COL_STATUS_TEXT, COL_STATUS);
  i32 hint_w = gfx::text_width(hint);
  if (cx + cw - hint_w - 8 > cx + gfx::text_width(status) + 16)
    gfx::draw_text(cx + cw - hint_w - 4, status_y + 3, hint, COL_LINENUM, COL_STATUS);

  // ── Text area ──
  i32 text_h = ch - STATUS_H;
  i32 visible_lines = text_h / LINE_H;

  gfx::fill_rect(cx, cy, LINENUM_W, text_h, COL_LINENUM_BG);
  gfx::fill_rect(cx + LINENUM_W, cy, cw - LINENUM_W, text_h, COL_BG);

  // Ensure cursor is visible
  i32 cursor_line = count_lines(s->text, s->cursor);
  if (cursor_line < s->scroll_y)
    s->scroll_y = cursor_line;
  if (cursor_line >= s->scroll_y + visible_lines)
    s->scroll_y = cursor_line - visible_lines + 1;

  // Selection range
  i32 sel_lo = -1, sel_hi = -1;
  sel_range(s, sel_lo, sel_hi);

  // Draw text
  i32 line = 0;
  i32 col = 0;
  i32 text_x = cx + LINENUM_W + 4;

  auto draw_linenum = [&](i32 ln, i32 y) {
    char lnbuf[8];
    i32 v = ln + 1, i = 0;
    char tmp[8];
    if (v == 0) tmp[i++] = '0';
    else while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    i32 j = 0;
    while (i > 0) lnbuf[j++] = tmp[--i];
    lnbuf[j] = '\0';
    i32 xoff = LINENUM_W - 4 - j * fw;
    if (xoff < 2) xoff = 2;
    gfx::draw_text(cx + xoff, y, lnbuf, COL_LINENUM, COL_LINENUM_BG);
  };

  for (i32 i = 0; i <= s->len; i++) {
    bool at_end = (i == s->len);
    char ch_c = at_end ? '\0' : s->text[i];

    if (line >= s->scroll_y && line < s->scroll_y + visible_lines) {
      i32 py = cy + (line - s->scroll_y) * LINE_H;

      if (col == 0)
        draw_linenum(line, py + 1);

      // Check if this character is selected
      bool in_sel = (sel_lo >= 0 && i >= sel_lo && i < sel_hi);

      // Draw selection background
      if (in_sel && !at_end && ch_c != '\n') {
        gfx::fill_rect(text_x + col * fw, py, fw, LINE_H, COL_SEL);
      }

      // Draw cursor
      if (i == s->cursor) {
        i32 cursor_x = text_x + col * fw;
        gfx::fill_rect(cursor_x, py, 2, LINE_H, COL_CURSOR);
      }

      // Draw character
      if (!at_end && ch_c != '\n' && text_x + col * fw + fw <= cx + cw) {
        u32 fg = in_sel ? COL_SEL_TEXT : COL_TEXT;
        u32 bg = in_sel ? COL_SEL : COL_BG;
        gfx::draw_char(text_x + col * fw, py + 1, ch_c, fg, bg);
      }
    }

    if (ch_c == '\n') {
      line++;
      col = 0;
    } else if (!at_end) {
      col++;
    }
  }

  // ── Save As dialog ──
  if (s->saveas_open) {
    i32 dw = 340, dh = 100;
    i32 dx = cx + (cw - dw) / 2;
    i32 dy = cy + (ch - dh) / 2;

    gfx::fill_rect(dx + 3, dy + 3, dw, dh, 0x00333333);
    gfx::fill_rect(dx, dy, dw, dh, COL_DIALOG_BG);
    gfx::rect(dx, dy, dw, dh, 0x00666666);
    gfx::fill_rect(dx, dy, dw, 24, COL_DIALOG_TITLE);
    gfx::draw_text(dx + 8, dy + (24 - fh) / 2, "Save As",
                    0x00FFFFFF, COL_DIALOG_TITLE);

    gfx::draw_text(dx + 10, dy + 32, "Path:", COL_TEXT, COL_DIALOG_BG);

    i32 fx = dx + 10;
    i32 fy = dy + 50;
    i32 field_w = dw - 20;
    gfx::fill_rect(fx, fy, field_w, fh + 6, 0x00FFFFFF);
    gfx::rect(fx, fy, field_w, fh + 6, 0x00AAAAAA);
    gfx::draw_text(fx + 4, fy + 3, s->saveas_buf, COL_TEXT, 0x00FFFFFF);

    i32 cur_x = fx + 4 + s->saveas_cursor * fw;
    if (cur_x < fx + field_w - 2)
      gfx::fill_rect(cur_x, fy + 2, 1, fh + 2, COL_TEXT);

    gfx::draw_text(dx + 10, dy + dh - fh - 6, "Enter=Save  Esc=Cancel",
                    0x00888888, COL_DIALOG_BG);
  }
}

bool notepad_key(u8 *state, char key) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  // ── Save As dialog input ──
  if (s->saveas_open) {
    if (key == 0x1B) { // Escape
      s->saveas_open = false;
      return true;
    }
    if (key == '\r' || key == '\n') {
      // Confirm Save As
      if (s->saveas_buf[0]) {
        str::ncpy(s->filepath, s->saveas_buf, 127);
        // Extract filename
        const char *name = s->saveas_buf;
        for (const char *p = s->saveas_buf; *p; p++)
          if (*p == '/') name = p + 1;
        str::ncpy(s->filename, name, 63);
        save_file(s);
      }
      s->saveas_open = false;
      return true;
    }
    if (key == 0x7F || key == 0x08) {
      if (s->saveas_cursor > 0) {
        s->saveas_cursor--;
        s->saveas_buf[s->saveas_cursor] = '\0';
      }
      return true;
    }
    if (key >= 32 && key <= 126 && s->saveas_cursor < 62) {
      s->saveas_buf[s->saveas_cursor++] = key;
      s->saveas_buf[s->saveas_cursor] = '\0';
      return true;
    }
    return true;
  }

  // ── Ctrl+S: Save ──
  if (key == 0x13) {
    if (s->filepath[0]) {
      save_file(s);
    } else {
      // No path yet — open Save As
      s->saveas_open = true;
      str::cpy(s->saveas_buf, "/home/Desktop/");
      s->saveas_cursor = static_cast<i32>(str::len(s->saveas_buf));
    }
    return true;
  }

  // ── Ctrl+W: Save As ──
  if (key == 0x17) {
    s->saveas_open = true;
    if (s->filepath[0]) {
      str::ncpy(s->saveas_buf, s->filepath, 63);
    } else {
      str::cpy(s->saveas_buf, "/home/Desktop/");
    }
    s->saveas_cursor = static_cast<i32>(str::len(s->saveas_buf));
    return true;
  }

  // ── Ctrl+A: Select All ──
  if (key == 0x01) {
    s->sel_start = 0;
    s->cursor = s->len;
    return true;
  }

  // ── Delete key (sent as 0x04 by some terminals, or we handle via arrow) ──
  // We'll handle DEL (forward delete) as Ctrl+D for now
  if (key == 0x04) {
    // Delete selected text or char at cursor
    i32 lo, hi;
    if (sel_range(s, lo, hi)) {
      delete_range(s, lo, hi);
    } else if (s->cursor < s->len) {
      for (i32 i = s->cursor; i < s->len - 1; i++)
        s->text[i] = s->text[i + 1];
      s->len--;
      s->text[s->len] = '\0';
      s->dirty = true;
    }
    return true;
  }

  // ── Backspace ──
  if (key == 0x7F || key == 0x08) {
    i32 lo, hi;
    if (sel_range(s, lo, hi)) {
      delete_range(s, lo, hi);
    } else if (s->cursor > 0 && s->len > 0) {
      for (i32 i = s->cursor - 1; i < s->len - 1; i++)
        s->text[i] = s->text[i + 1];
      s->len--;
      s->cursor--;
      s->text[s->len] = '\0';
      s->dirty = true;
    }
    return true;
  }

  // ── Home: go to start of line ──
  if (key == 0x01) { // already handled Ctrl+A above
    return false;
  }

  // ── Enter ──
  if (key == '\r')
    key = '\n';

  // ── Printable or newline ──
  if ((key >= 32 && key <= 126) || key == '\n') {
    // Delete selection first if present
    i32 lo, hi;
    if (sel_range(s, lo, hi))
      delete_range(s, lo, hi);

    if (s->len < TEXT_MAX - 1) {
      for (i32 i = s->len; i > s->cursor; i--)
        s->text[i] = s->text[i - 1];
      s->text[s->cursor] = key;
      s->cursor++;
      s->len++;
      s->text[s->len] = '\0';
      s->dirty = true;
    }
    clear_selection(s);
    return true;
  }

  return false;
}

void notepad_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  // Clear selection on plain arrow (no shift)
  clear_selection(s);

  if (dir == 'C' && s->cursor < s->len)
    s->cursor++;
  else if (dir == 'D' && s->cursor > 0)
    s->cursor--;
  else if (dir == 'A') { // Up
    i32 col = cursor_col(s->text, s->cursor);
    i32 ln = count_lines(s->text, s->cursor);
    if (ln > 0) {
      i32 prev_start = line_start(s->text, s->len, ln - 1);
      i32 prev_end = line_start(s->text, s->len, ln) - 1;
      i32 prev_len = prev_end - prev_start;
      s->cursor = prev_start + (col < prev_len ? col : prev_len);
    }
  } else if (dir == 'B') { // Down
    i32 col = cursor_col(s->text, s->cursor);
    i32 ln = count_lines(s->text, s->cursor);
    i32 tl = total_lines(s->text, s->len);
    if (ln < tl - 1) {
      i32 next_start = line_start(s->text, s->len, ln + 1);
      i32 next_next = line_start(s->text, s->len, ln + 2);
      i32 next_len = next_next - next_start;
      if (next_len > 0 && s->text[next_next - 1] == '\n')
        next_len--;
      s->cursor = next_start + (col < next_len ? col : next_len);
    }
  } else if (dir == 'H') { // Home
    i32 ln = count_lines(s->text, s->cursor);
    s->cursor = line_start(s->text, s->len, ln);
  } else if (dir == 'F') { // End
    i32 ln = count_lines(s->text, s->cursor);
    s->cursor = line_end(s->text, s->len, ln);
  }
}

void notepad_close(u8 *) {}

// Convert mouse coordinates (relative to content area) to a text position
i32 pos_from_xy(NotepadState *s, i32 rx, i32 ry, i32 ch) {
  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 LINE_H = fh + 2;
  i32 LINENUM_W = fw * 4;
  i32 STATUS_H = fh + 6;
  i32 text_h = ch - STATUS_H;

  if (ry < 0) ry = 0;
  if (ry >= text_h) ry = text_h - 1;

  i32 clicked_line = s->scroll_y + ry / LINE_H;
  i32 clicked_col = (rx - LINENUM_W - 4) / fw;
  if (clicked_col < 0) clicked_col = 0;

  i32 tl = total_lines(s->text, s->len);
  if (clicked_line >= tl) clicked_line = tl - 1;
  if (clicked_line < 0) clicked_line = 0;

  i32 ls = line_start(s->text, s->len, clicked_line);
  i32 le = line_end(s->text, s->len, clicked_line);
  i32 ll = le - ls;

  if (clicked_col > ll) clicked_col = ll;
  return ls + clicked_col;
}

void notepad_click(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  if (s->saveas_open) return;

  // On release: if selection is empty (click without drag), clear it
  s->cursor = pos_from_xy(s, rx, ry, ch);
  if (s->sel_start == s->cursor)
    clear_selection(s);
}

void notepad_mouse_down(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  if (s->saveas_open) return;

  s->cursor = pos_from_xy(s, rx, ry, ch);
  s->sel_start = s->cursor; // anchor selection
}

void notepad_mouse_move(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  if (s->saveas_open) return;

  // Extend selection by moving cursor while anchor stays
  s->cursor = pos_from_xy(s, rx, ry, ch);
}

void notepad_open_file(u8 *state, const char *path, const char *content) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  // Store full path
  str::ncpy(s->filepath, path, 127);

  // Extract filename
  const char *name = path;
  for (const char *p = path; *p; p++)
    if (*p == '/') name = p + 1;
  str::ncpy(s->filename, name, 63);

  usize clen = str::len(content);
  if (clen > static_cast<usize>(TEXT_MAX - 1))
    clen = static_cast<usize>(TEXT_MAX - 1);
  str::memcpy(s->text, content, clen);
  s->text[clen] = '\0';
  s->len = static_cast<i32>(clen);
  s->cursor = 0;
  s->scroll_y = 0;
  s->dirty = false;
  s->sel_start = -1;
}

void notepad_scroll(u8 *state, i32 delta) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  s->scroll_y -= delta * 3;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

const OgzApp notepad_app = {
    "Notepad",       // name
    "notepad.ogz",   // id
    680,             // default_w
    520,             // default_h
    notepad_open,    // on_open
    notepad_draw,    // on_draw
    notepad_key,     // on_key
    notepad_arrow,   // on_arrow
    notepad_close,   // on_close
    notepad_click,      // on_click
    notepad_scroll,     // on_scroll
    notepad_mouse_down, // on_mouse_down
    notepad_mouse_move, // on_mouse_move
    notepad_open_file, // on_open_file
};

} // anonymous namespace

// Explicit registration function called from kernel
namespace apps {
void register_notepad() { register_app(&notepad_app); }
} // namespace apps
