#include "app.h"
#include "csharp.h"
#include "fs.h"
#include "graphics.h"
#include "gui.h"
#include "registry.h"
#include "string.h"
#include "syslog.h"

/*
 * csharp.ogz — OguzOS C# IDE
 *
 * Split-pane editor: code on top, output on bottom.
 * F5 / Ctrl+R to run. Ctrl+S to save.
 */

namespace {

constexpr i32 SRC_MAX = 2400;
constexpr i32 OUT_MAX = 900;

struct CSharpState {
  char src[SRC_MAX];
  i32 src_len;
  i32 cursor;
  i32 scroll_y;
  i32 sel_start;
  char output[OUT_MAX];
  i32 out_scroll;
  char filepath[128];
  char filename[64];
  bool dirty;
  bool ran_ok;       // last run succeeded
  bool has_output;   // has run at least once
  // Save As
  bool saveas_open;
  char saveas_buf[64];
  i32 saveas_cursor;
};

static_assert(sizeof(CSharpState) <= 4096, "CSharpState too large");

// ── Colours ─────────────────────────────────────────────────────────────────
constexpr u32 COL_BG = 0x001E1E2E;       // dark editor bg
constexpr u32 COL_TEXT = 0x00CDD6F4;     // light text
constexpr u32 COL_KW = 0x0089B4FA;       // keyword blue
constexpr u32 COL_TYPE = 0x0094E2D5;     // type teal
constexpr u32 COL_STR = 0x00A6E3A1;      // string green
constexpr u32 COL_NUM = 0x00FAB387;      // number orange
constexpr u32 COL_COMMENT = 0x006C7086;  // comment grey
constexpr u32 COL_CURSOR = 0x00F5E0DC;
constexpr u32 COL_SEL = 0x00585B70;
constexpr u32 COL_LINENUM_BG = 0x00181825;
constexpr u32 COL_LINENUM = 0x006C7086;
constexpr u32 COL_OUT_BG = 0x00181825;
constexpr u32 COL_OUT_TEXT = 0x00BAC2DE;
constexpr u32 COL_OUT_ERR = 0x00F38BA8;
constexpr u32 COL_SPLITTER = 0x00313244;
constexpr u32 COL_STATUS = 0x00313244;
constexpr u32 COL_STATUS_TEXT = 0x00A6ADC8;
constexpr u32 COL_RUN_BTN = 0x00A6E3A1;

// ── Helpers (same as notepad) ───────────────────────────────────────────────
i32 count_lines(const char *t, i32 pos) {
  i32 n = 0; for (i32 i = 0; i < pos; i++) if (t[i] == '\n') n++; return n;
}
i32 cursor_col(const char *t, i32 pos) {
  i32 c = 0; for (i32 i = pos - 1; i >= 0; i--) { if (t[i] == '\n') break; c++; } return c;
}
i32 line_start(const char *t, i32 len, i32 line) {
  if (line == 0) return 0;
  i32 l = 0; for (i32 i = 0; i < len; i++) { if (t[i] == '\n') { l++; if (l == line) return i + 1; } }
  return len;
}
i32 line_end(const char *t, i32 len, i32 line) {
  i32 s = line_start(t, len, line);
  for (i32 i = s; i < len; i++) if (t[i] == '\n') return i;
  return len;
}
i32 total_lines(const char *t, i32 len) { return count_lines(t, len) + 1; }

bool sel_range(CSharpState *s, i32 &lo, i32 &hi) {
  if (s->sel_start < 0 || s->sel_start == s->cursor) return false;
  lo = s->sel_start < s->cursor ? s->sel_start : s->cursor;
  hi = s->sel_start > s->cursor ? s->sel_start : s->cursor;
  return true;
}

void delete_range(CSharpState *s, i32 lo, i32 hi) {
  i32 del = hi - lo; if (del <= 0) return;
  for (i32 i = lo; i < s->src_len - del; i++) s->src[i] = s->src[i + del];
  s->src_len -= del; s->src[s->src_len] = '\0';
  s->cursor = lo; s->sel_start = -1; s->dirty = true;
}

void append_int(char *dst, i32 val) {
  char tmp[12]; i32 i = 0;
  if (val == 0) { tmp[i++] = '0'; }
  else { while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; } }
  usize len = str::len(dst);
  for (i32 j = i - 1; j >= 0; j--) dst[len++] = tmp[j];
  dst[len] = '\0';
}

void split_path(const char *path, char *dir, usize dsz, char *name, usize nsz) {
  const char *sl = path;
  for (const char *p = path; *p; p++) if (*p == '/') sl = p;
  if (sl == path) { str::ncpy(dir, "/", dsz - 1); str::ncpy(name, path + 1, nsz - 1); }
  else {
    usize d = static_cast<usize>(sl - path);
    if (d >= dsz) d = dsz - 1;
    str::memcpy(dir, path, d); dir[d] = '\0';
    str::ncpy(name, sl + 1, nsz - 1);
  }
}

bool save_file(CSharpState *s) {
  if (!s->filepath[0]) return false;
  char dir[128], name[64];
  split_path(s->filepath, dir, sizeof(dir), name, sizeof(name));
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(dir); fs::touch(name);
  bool ok = fs::write(name, s->src);
  if (ok) { fs::sync_to_disk(); s->dirty = false; }
  fs::cd(old);
  return ok;
}

i32 pos_from_xy(CSharpState *s, i32 rx, i32 ry, i32 /*ch*/, i32 split_y) {
  i32 fw = gfx::font_w(), fh = gfx::font_h(), LH = fh + 2, LNW = fw * 4;
  if (ry < 0) ry = 0;
  if (ry >= split_y) ry = split_y - 1;
  i32 line = s->scroll_y + ry / LH;
  i32 col = (rx - LNW - 4) / fw;
  if (col < 0) col = 0;
  i32 tl = total_lines(s->src, s->src_len);
  if (line >= tl) line = tl - 1;
  if (line < 0) line = 0;
  i32 ls = line_start(s->src, s->src_len, line);
  i32 le = line_end(s->src, s->src_len, line);
  i32 ll = le - ls;
  if (col > ll) col = ll;
  return ls + col;
}

// ── Syntax highlighting ─────────────────────────────────────────────────────
bool is_kw(const char *word) {
  const char *kws[] = {"using","class","static","if","else","while","for",
    "return","new","null","namespace","public","private","protected",
    "this","base","override","virtual","abstract","readonly","const",nullptr};
  for (i32 i = 0; kws[i]; i++) if (str::cmp(word, kws[i]) == 0) return true;
  return false;
}
bool is_type(const char *word) {
  const char *ts[] = {"void","int","string","bool","char","float","double",
    "var","object","Console",nullptr};
  for (i32 i = 0; ts[i]; i++) if (str::cmp(word, ts[i]) == 0) return true;
  return false;
}
bool is_bool_lit(const char *word) {
  return str::cmp(word, "true") == 0 || str::cmp(word, "false") == 0;
}

u32 get_char_color(const char *text, i32 len, i32 pos) {
  char c = text[pos];

  // Check if inside a comment
  for (i32 i = pos; i >= 0; i--) {
    if (i > 0 && text[i - 1] == '/' && text[i] == '/') {
      // Check this is on the same line
      bool same_line = true;
      for (i32 j = i; j <= pos; j++) if (text[j] == '\n') { same_line = false; break; }
      if (same_line) return COL_COMMENT;
    }
  }
  // Simpler: scan backward on current line for //
  i32 line_s = pos;
  while (line_s > 0 && text[line_s - 1] != '\n') line_s--;
  for (i32 i = line_s; i < pos; i++) {
    if (text[i] == '/' && i + 1 < len && text[i + 1] == '/')
      return COL_COMMENT;
  }

  // String literal
  // Count quotes from line start
  i32 quotes = 0;
  for (i32 i = line_s; i < pos; i++) if (text[i] == '"') quotes++;
  if (quotes % 2 == 1) return COL_STR;
  if (c == '"') return COL_STR;

  // Number
  if (c >= '0' && c <= '9') return COL_NUM;

  // Word-based coloring
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
    // Extract the word
    i32 ws = pos;
    while (ws > 0 && ((text[ws-1] >= 'a' && text[ws-1] <= 'z') ||
           (text[ws-1] >= 'A' && text[ws-1] <= 'Z') ||
           (text[ws-1] >= '0' && text[ws-1] <= '9') || text[ws-1] == '_'))
      ws--;
    i32 we = pos;
    while (we < len && ((text[we] >= 'a' && text[we] <= 'z') ||
           (text[we] >= 'A' && text[we] <= 'Z') ||
           (text[we] >= '0' && text[we] <= '9') || text[we] == '_'))
      we++;
    char word[32];
    i32 wlen = we - ws;
    if (wlen > 31) wlen = 31;
    for (i32 i = 0; i < wlen; i++) word[i] = text[ws + i];
    word[wlen] = '\0';

    if (is_kw(word)) return COL_KW;
    if (is_type(word)) return COL_TYPE;
    if (is_bool_lit(word)) return COL_NUM;
  }

  return COL_TEXT;
}

// ── Callbacks ───────────────────────────────────────────────────────────────
const char *DEFAULT_SRC =
  "using System;\n\nclass Program {\n    static void Main() {\n"
  "        Console.WriteLine(\"Hello, OguzOS!\");\n\n"
  "        for (int i = 1; i <= 5; i++) {\n"
  "            Console.WriteLine(\"Count: \" + i);\n"
  "        }\n    }\n}\n";

void csharp_open(u8 *state) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  str::ncpy(s->src, DEFAULT_SRC, SRC_MAX - 1);
  s->src_len = static_cast<i32>(str::len(s->src));
  s->cursor = 0;
  s->scroll_y = 0;
  s->sel_start = -1;
  s->output[0] = '\0';
  s->out_scroll = 0;
  s->filepath[0] = '\0';
  s->filename[0] = '\0';
  s->dirty = false;
  s->ran_ok = false;
  s->has_output = false;
  s->saveas_open = false;
  s->saveas_buf[0] = '\0';
  s->saveas_cursor = 0;
}

void csharp_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  i32 fw = gfx::font_w(), fh = gfx::font_h(), LH = fh + 2;
  i32 LNW = fw * 4;
  i32 STATUS_H = fh + 6;
  i32 SPLIT_H = 4;

  // Layout: editor top 60%, output bottom 40%
  i32 editor_h = (ch - STATUS_H - SPLIT_H) * 6 / 10;
  i32 output_h = ch - STATUS_H - SPLIT_H - editor_h;
  i32 split_y = cy + editor_h;
  i32 out_y = split_y + SPLIT_H;
  i32 status_y = cy + ch - STATUS_H;

  // ── Status bar ──
  gfx::fill_rect(cx, status_y, cw, STATUS_H, COL_STATUS);
  char status[128]; status[0] = '\0';
  if (s->filename[0]) str::cat(status, s->filename);
  else str::cat(status, "Untitled.cs");
  if (s->dirty) str::cat(status, " *");
  str::cat(status, "  |  Ln ");
  append_int(status, count_lines(s->src, s->cursor) + 1);
  str::cat(status, ", Col ");
  append_int(status, cursor_col(s->src, s->cursor) + 1);
  gfx::draw_text(cx + 4, status_y + 3, status, COL_STATUS_TEXT, COL_STATUS);

  // Run button hint
  const char *hint = "F5 Run  F6 GUI  Ctrl+S Save";
  i32 hw = gfx::text_width(hint);
  gfx::draw_text(cx + cw - hw - 4, status_y + 3, hint, COL_RUN_BTN, COL_STATUS);

  // ── Splitter ──
  gfx::fill_rect(cx, split_y, cw, SPLIT_H, COL_SPLITTER);
  gfx::draw_text(cx + 4, split_y, "Output", COL_STATUS_TEXT, COL_SPLITTER);

  // ── Output panel ──
  gfx::fill_rect(cx, out_y, cw, output_h, COL_OUT_BG);
  if (s->has_output) {
    u32 ocol = s->ran_ok ? COL_OUT_TEXT : COL_OUT_ERR;
    i32 oline = 0, ox = cx + 4, oy = out_y + 2;
    for (i32 i = 0; s->output[i] && oy + fh <= out_y + output_h; i++) {
      if (s->output[i] == '\n') {
        oline++; ox = cx + 4; oy += LH;
      } else {
        if (ox + fw <= cx + cw)
          gfx::draw_char(ox, oy, s->output[i], ocol, COL_OUT_BG);
        ox += fw;
      }
    }
  } else {
    gfx::draw_text(cx + 4, out_y + 2, "F5=Console  F6=GUI App", COL_COMMENT, COL_OUT_BG);
  }

  // ── Editor ──
  i32 vis_lines = editor_h / LH;
  gfx::fill_rect(cx, cy, LNW, editor_h, COL_LINENUM_BG);
  gfx::fill_rect(cx + LNW, cy, cw - LNW, editor_h, COL_BG);

  // Ensure cursor visible
  i32 cl = count_lines(s->src, s->cursor);
  if (cl < s->scroll_y) s->scroll_y = cl;
  if (cl >= s->scroll_y + vis_lines) s->scroll_y = cl - vis_lines + 1;

  // Selection
  i32 slo = -1, shi = -1;
  sel_range(s, slo, shi);

  // Draw text with syntax highlighting
  i32 line = 0, col = 0;
  i32 text_x = cx + LNW + 4;

  for (i32 i = 0; i <= s->src_len; i++) {
    bool at_end = (i == s->src_len);
    char cc = at_end ? '\0' : s->src[i];

    if (line >= s->scroll_y && line < s->scroll_y + vis_lines) {
      i32 py = cy + (line - s->scroll_y) * LH;

      // Line number
      if (col == 0) {
        char lnb[8]; i32 v = line + 1, li = 0;
        char tmp[8];
        if (v == 0) tmp[li++] = '0';
        else while (v > 0) { tmp[li++] = '0' + (v % 10); v /= 10; }
        i32 j = 0;
        while (li > 0) lnb[j++] = tmp[--li];
        lnb[j] = '\0';
        i32 xoff = LNW - 4 - j * fw;
        if (xoff < 2) xoff = 2;
        gfx::draw_text(cx + xoff, py + 1, lnb, COL_LINENUM, COL_LINENUM_BG);
      }

      bool in_sel = (slo >= 0 && i >= slo && i < shi);
      if (in_sel && !at_end && cc != '\n')
        gfx::fill_rect(text_x + col * fw, py, fw, LH, COL_SEL);

      // Cursor
      if (i == s->cursor)
        gfx::fill_rect(text_x + col * fw, py, 2, LH, COL_CURSOR);

      // Character with syntax color
      if (!at_end && cc != '\n' && text_x + col * fw + fw <= cx + cw) {
        u32 fg = in_sel ? 0x00FFFFFF : get_char_color(s->src, s->src_len, i);
        u32 bg = in_sel ? COL_SEL : COL_BG;
        gfx::draw_char(text_x + col * fw, py + 1, cc, fg, bg);
      }
    }

    if (cc == '\n') { line++; col = 0; }
    else if (!at_end) col++;
  }

  // ── Save As dialog ──
  if (s->saveas_open) {
    i32 dw = 340, dh = 100;
    i32 dx = cx + (cw - dw) / 2, dy = cy + (ch - dh) / 2;
    gfx::fill_rect(dx + 3, dy + 3, dw, dh, 0x00111111);
    gfx::fill_rect(dx, dy, dw, dh, 0x002D2D3D);
    gfx::rect(dx, dy, dw, dh, 0x00666666);
    gfx::fill_rect(dx, dy, dw, 24, COL_KW);
    gfx::draw_text(dx + 8, dy + (24 - fh) / 2, "Save As", 0x00FFFFFF, COL_KW);
    gfx::draw_text(dx + 10, dy + 32, "Path:", COL_TEXT, 0x002D2D3D);
    i32 fx = dx + 10, fy = dy + 50, field_w = dw - 20;
    gfx::fill_rect(fx, fy, field_w, fh + 6, COL_BG);
    gfx::rect(fx, fy, field_w, fh + 6, 0x00666666);
    gfx::draw_text(fx + 4, fy + 3, s->saveas_buf, COL_TEXT, COL_BG);
    i32 cur_x = fx + 4 + s->saveas_cursor * fw;
    if (cur_x < fx + field_w - 2)
      gfx::fill_rect(cur_x, fy + 2, 1, fh + 2, COL_CURSOR);
    gfx::draw_text(dx + 10, dy + dh - fh - 6, "Enter=Save  Esc=Cancel",
                    COL_COMMENT, 0x002D2D3D);
  }
}

bool csharp_key(u8 *state, char key) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  // Save As dialog
  if (s->saveas_open) {
    if (key == 0x1B) { s->saveas_open = false; return true; }
    if (key == '\r' || key == '\n') {
      if (s->saveas_buf[0]) {
        str::ncpy(s->filepath, s->saveas_buf, 127);
        const char *n = s->saveas_buf;
        for (const char *p = s->saveas_buf; *p; p++) if (*p == '/') n = p + 1;
        str::ncpy(s->filename, n, 63);
        save_file(s);
      }
      s->saveas_open = false;
      return true;
    }
    if (key == 0x7F || key == 0x08) {
      if (s->saveas_cursor > 0) { s->saveas_cursor--; s->saveas_buf[s->saveas_cursor] = '\0'; }
      return true;
    }
    if (key >= 32 && key <= 126 && s->saveas_cursor < 62) {
      s->saveas_buf[s->saveas_cursor++] = key;
      s->saveas_buf[s->saveas_cursor] = '\0';
      return true;
    }
    return true;
  }

  // F5 / Ctrl+R = Run
  if (key == 0x12) {
    // If file has a path, save first then run in a terminal window
    if (s->filepath[0]) {
      save_file(s);
      // Build "csrun /path/file.cs" command and launch terminal
      char cmd[200];
      str::cpy(cmd, "csrun ");
      str::cat(cmd, s->filepath);
      gui::open_file(s->filepath, cmd);
    }
    // Always show output in the IDE panel too
    s->ran_ok = csharp::run(s->src, s->output, OUT_MAX);
    s->has_output = true;
    s->out_scroll = 0;
    return true;
  }

  // F6 / Ctrl+G = Run as GUI App
  if (key == 0x07) {
    // Save to a .csg path so file association opens csgui.ogz
    char gui_path[128];
    if (s->filepath[0]) {
      str::ncpy(gui_path, s->filepath, 127);
      // Replace .cs with .csg if needed
      usize pl = str::len(gui_path);
      if (pl > 3 && str::cmp(gui_path + pl - 3, ".cs") == 0) {
        gui_path[pl] = 'g';
        gui_path[pl + 1] = '\0';
      }
    } else {
      str::cpy(gui_path, "/tmp/app.csg");
    }

    // Save source to the .csg file
    char dir[128], name[64];
    split_path(gui_path, dir, sizeof(dir), name, sizeof(name));
    char old_cwd[256]; fs::get_cwd(old_cwd, sizeof(old_cwd));
    fs::cd(dir); fs::touch(name); fs::write(name, s->src);
    fs::sync_to_disk(); fs::cd(old_cwd);

    // Launch via file association (.csg → csgui.ogz)
    gui::open_file(gui_path, s->src);
    s->has_output = true;
    s->ran_ok = true;
    str::cpy(s->output, "GUI app launched.\n");
    return true;
  }

  // Ctrl+S
  if (key == 0x13) {
    if (s->filepath[0]) { save_file(s); }
    else {
      s->saveas_open = true;
      str::cpy(s->saveas_buf, "/home/Desktop/");
      s->saveas_cursor = static_cast<i32>(str::len(s->saveas_buf));
    }
    return true;
  }

  // Ctrl+W: Save As
  if (key == 0x17) {
    s->saveas_open = true;
    if (s->filepath[0]) str::ncpy(s->saveas_buf, s->filepath, 63);
    else str::cpy(s->saveas_buf, "/home/Desktop/program.cs");
    s->saveas_cursor = static_cast<i32>(str::len(s->saveas_buf));
    return true;
  }

  // Ctrl+A
  if (key == 0x01) { s->sel_start = 0; s->cursor = s->src_len; return true; }

  // Escape
  if (key == 0x1B) { s->sel_start = -1; return true; }

  // Delete
  if (key == 0x04) {
    i32 lo, hi;
    if (sel_range(s, lo, hi)) { delete_range(s, lo, hi); }
    else if (s->cursor < s->src_len) {
      for (i32 i = s->cursor; i < s->src_len - 1; i++) s->src[i] = s->src[i + 1];
      s->src_len--; s->src[s->src_len] = '\0'; s->dirty = true;
    }
    return true;
  }

  // Backspace
  if (key == 0x7F || key == 0x08) {
    i32 lo, hi;
    if (sel_range(s, lo, hi)) { delete_range(s, lo, hi); }
    else if (s->cursor > 0) {
      for (i32 i = s->cursor - 1; i < s->src_len - 1; i++) s->src[i] = s->src[i + 1];
      s->src_len--; s->cursor--; s->src[s->src_len] = '\0'; s->dirty = true;
    }
    return true;
  }

  if (key == '\r') key = '\n';

  // Printable / newline
  if ((key >= 32 && key <= 126) || key == '\n') {
    i32 lo, hi;
    if (sel_range(s, lo, hi)) delete_range(s, lo, hi);
    if (s->src_len < SRC_MAX - 1) {
      for (i32 i = s->src_len; i > s->cursor; i--) s->src[i] = s->src[i - 1];
      s->src[s->cursor] = key; s->cursor++; s->src_len++;
      s->src[s->src_len] = '\0'; s->dirty = true;
    }
    s->sel_start = -1;
    return true;
  }

  return false;
}

void csharp_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  s->sel_start = -1;
  if (dir == 'C' && s->cursor < s->src_len) s->cursor++;
  else if (dir == 'D' && s->cursor > 0) s->cursor--;
  else if (dir == 'A') {
    i32 c = cursor_col(s->src, s->cursor), l = count_lines(s->src, s->cursor);
    if (l > 0) {
      i32 ps = line_start(s->src, s->src_len, l - 1);
      i32 pe = line_start(s->src, s->src_len, l) - 1;
      i32 pl = pe - ps;
      s->cursor = ps + (c < pl ? c : pl);
    }
  } else if (dir == 'B') {
    i32 c = cursor_col(s->src, s->cursor), l = count_lines(s->src, s->cursor);
    i32 tl = total_lines(s->src, s->src_len);
    if (l < tl - 1) {
      i32 ns = line_start(s->src, s->src_len, l + 1);
      i32 nn = line_start(s->src, s->src_len, l + 2);
      i32 nl = nn - ns;
      if (nl > 0 && s->src[nn - 1] == '\n') nl--;
      s->cursor = ns + (c < nl ? c : nl);
    }
  } else if (dir == 'H') {
    i32 l = count_lines(s->src, s->cursor);
    s->cursor = line_start(s->src, s->src_len, l);
  } else if (dir == 'F') {
    i32 l = count_lines(s->src, s->cursor);
    s->cursor = line_end(s->src, s->src_len, l);
  }
}

void csharp_close(u8 *) {}

void csharp_click(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  if (s->saveas_open) return;
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 editor_h = (ch - STATUS_H - SPLIT_H) * 6 / 10;
  if (ry < editor_h) {
    s->cursor = pos_from_xy(s, rx, ry, ch, editor_h);
    if (s->sel_start == s->cursor) s->sel_start = -1;
  }
}

void csharp_mouse_down(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  if (s->saveas_open) return;
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 editor_h = (ch - STATUS_H - SPLIT_H) * 6 / 10;
  if (ry < editor_h) {
    s->cursor = pos_from_xy(s, rx, ry, ch, editor_h);
    s->sel_start = s->cursor;
  }
}

void csharp_mouse_move(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  if (s->saveas_open) return;
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 editor_h = (ch - STATUS_H - SPLIT_H) * 6 / 10;
  s->cursor = pos_from_xy(s, rx, ry, ch, editor_h);
}

void csharp_scroll(u8 *state, i32 delta) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  s->scroll_y -= delta * 3;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

void csharp_open_file(u8 *state, const char *path, const char *content) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  str::ncpy(s->filepath, path, 127);
  const char *n = path;
  for (const char *p = path; *p; p++) if (*p == '/') n = p + 1;
  str::ncpy(s->filename, n, 63);
  usize cl = str::len(content);
  if (cl > static_cast<usize>(SRC_MAX - 1)) cl = static_cast<usize>(SRC_MAX - 1);
  str::memcpy(s->src, content, cl);
  s->src[cl] = '\0';
  s->src_len = static_cast<i32>(cl);
  s->cursor = 0; s->scroll_y = 0; s->dirty = false;
  s->sel_start = -1;
  s->output[0] = '\0'; s->has_output = false;
}

const OgzApp csharp_app = {
    "C# IDE",        // name
    "csharp.ogz",    // id
    760,             // default_w
    600,             // default_h
    csharp_open,
    csharp_draw,
    csharp_key,
    csharp_arrow,
    csharp_close,
    csharp_click,
    csharp_scroll,
    csharp_mouse_down,
    csharp_mouse_move,
    csharp_open_file,
};

} // anonymous namespace

namespace apps {
void register_csharp() { register_app(&csharp_app); }
} // namespace apps
