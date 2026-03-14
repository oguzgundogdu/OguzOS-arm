#include "app.h"
#include "fs.h"
#include "graphics.h"
#include "registry.h"
#include "string.h"

/*
 * notepad.ogz — OguzOS Notepad
 *
 * A simple text editor. Type to insert, backspace to delete,
 * arrow keys to move cursor, Ctrl+S to save.
 */

namespace {

struct NotepadState {
  char text[4000];
  i32 len;
  i32 cursor;
  i32 scroll_y; // line offset for scrolling
  char filename[64];
  bool dirty; // unsaved changes
};

static_assert(sizeof(NotepadState) <= 4096, "NotepadState exceeds app_state");

constexpr u32 COL_BG = 0x00FFFFFF;
constexpr u32 COL_TEXT = 0x00202020;
constexpr u32 COL_CURSOR = 0x00000000;
constexpr u32 COL_LINENUM_BG = 0x00E8E8E8;
constexpr u32 COL_LINENUM = 0x00999999;
constexpr u32 COL_STATUS = 0x00E0E0E0;
constexpr u32 COL_STATUS_TEXT = 0x00444444;

// Count lines up to position
i32 count_lines(const char *text, i32 pos) {
  i32 lines = 0;
  for (i32 i = 0; i < pos; i++) {
    if (text[i] == '\n')
      lines++;
  }
  return lines;
}

// Get column of cursor on its current line
i32 cursor_col(const char *text, i32 pos) {
  i32 col = 0;
  for (i32 i = pos - 1; i >= 0; i--) {
    if (text[i] == '\n')
      break;
    col++;
  }
  return col;
}

// Find start of line N (0-indexed)
i32 line_start(const char *text, i32 len, i32 line) {
  if (line == 0)
    return 0;
  i32 l = 0;
  for (i32 i = 0; i < len; i++) {
    if (text[i] == '\n') {
      l++;
      if (l == line)
        return i + 1;
    }
  }
  return len;
}

void notepad_open(u8 *state) {
  auto *s = reinterpret_cast<NotepadState *>(state);
  s->text[0] = '\0';
  s->len = 0;
  s->cursor = 0;
  s->scroll_y = 0;
  s->filename[0] = '\0';
  s->dirty = false;
}

void notepad_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 LINE_H = fh + 2;
  i32 LINENUM_W = fw * 4; // 4 digits
  i32 STATUS_H = fh + 6;

  // Status bar at bottom
  i32 status_y = cy + ch - STATUS_H;
  gfx::fill_rect(cx, status_y, cw, STATUS_H, COL_STATUS);
  char status[80];
  i32 cur_line = count_lines(s->text, s->cursor) + 1;
  i32 cur_col = cursor_col(s->text, s->cursor) + 1;
  str::ncpy(status, "Ln ", 79);
  auto append_int = [](char *dst, i32 val) {
    char tmp[12];
    i32 i = 0;
    if (val == 0) {
      tmp[i++] = '0';
    } else {
      while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    }
    usize len = str::len(dst);
    for (i32 j = i - 1; j >= 0; j--)
      dst[len++] = tmp[j];
    dst[len] = '\0';
  };
  append_int(status, cur_line);
  str::cat(status, ", Col ");
  append_int(status, cur_col);
  if (s->dirty)
    str::cat(status, " [modified]");
  gfx::draw_text(cx + 4, status_y + 3, status, COL_STATUS_TEXT, COL_STATUS);

  // Text area
  i32 text_h = ch - STATUS_H;
  i32 visible_lines = text_h / LINE_H;

  // Line numbers background
  gfx::fill_rect(cx, cy, LINENUM_W, text_h, COL_LINENUM_BG);

  // Text background
  gfx::fill_rect(cx + LINENUM_W, cy, cw - LINENUM_W, text_h, COL_BG);

  // Ensure cursor is visible
  i32 cursor_line = count_lines(s->text, s->cursor);
  if (cursor_line < s->scroll_y)
    s->scroll_y = cursor_line;
  if (cursor_line >= s->scroll_y + visible_lines)
    s->scroll_y = cursor_line - visible_lines + 1;

  // Draw text line by line
  i32 line = 0;
  i32 col = 0;
  i32 text_x = cx + LINENUM_W + 4;
  i32 draw_line = 0;

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
      draw_line = line - s->scroll_y;
      i32 py = cy + draw_line * LINE_H;

      if (col == 0)
        draw_linenum(line, py + 1);

      // Draw cursor
      if (i == s->cursor) {
        i32 cursor_x = text_x + col * fw;
        gfx::fill_rect(cursor_x, py, 2, LINE_H, COL_CURSOR);
      }

      // Draw character
      if (!at_end && ch_c != '\n' && text_x + col * fw + fw <= cx + cw) {
        gfx::draw_char(text_x + col * fw, py + 1, ch_c, COL_TEXT, COL_BG);
      }
    }

    if (ch_c == '\n') {
      line++;
      col = 0;
    } else if (!at_end) {
      col++;
    }
  }
}

bool notepad_key(u8 *state, char key) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  if (key == 0x13) { // Ctrl+S
    if (s->filename[0]) {
      fs::write(s->filename, s->text);
      s->dirty = false;
    }
    return true;
  }

  if (key == 0x7F || key == 0x08) { // Backspace
    if (s->cursor > 0 && s->len > 0) {
      for (i32 i = s->cursor - 1; i < s->len - 1; i++)
        s->text[i] = s->text[i + 1];
      s->len--;
      s->cursor--;
      s->text[s->len] = '\0';
      s->dirty = true;
    }
    return true;
  }

  if (key == '\r')
    key = '\n';

  // Printable or newline
  if ((key >= 32 && key <= 126) || key == '\n') {
    if (s->len < 3999) {
      for (i32 i = s->len; i > s->cursor; i--)
        s->text[i] = s->text[i - 1];
      s->text[s->cursor] = key;
      s->cursor++;
      s->len++;
      s->text[s->len] = '\0';
      s->dirty = true;
    }
    return true;
  }

  return false;
}

void notepad_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<NotepadState *>(state);

  if (dir == 'C' && s->cursor < s->len) // Right
    s->cursor++;
  else if (dir == 'D' && s->cursor > 0) // Left
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
    i32 total_lines = count_lines(s->text, s->len);
    if (ln < total_lines) {
      i32 next_start = line_start(s->text, s->len, ln + 1);
      i32 next_next = line_start(s->text, s->len, ln + 2);
      i32 next_len = next_next - next_start;
      if (s->text[next_next - 1] == '\n')
        next_len--;
      s->cursor = next_start + (col < next_len ? col : next_len);
    }
  }
}

void notepad_close(u8 *) {}

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
    nullptr,         // on_click
};

// Auto-register
struct NotepadRegistrar {
  NotepadRegistrar() { apps::register_app(&notepad_app); }
};

// This won't work with -fno-use-cxa-atexit in freestanding...
// We'll register manually from kernel instead.

} // anonymous namespace

// Explicit registration function called from kernel
namespace apps {
void register_notepad() { register_app(&notepad_app); }
} // namespace apps
