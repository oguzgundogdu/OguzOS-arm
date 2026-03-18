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
 * Solution explorer panel on the left for .sln projects.
 * F5 / Ctrl+R to run. Ctrl+S to save. Ctrl+E toggle explorer.
 */

namespace {

constexpr i32 SRC_MAX = 2400;
constexpr i32 OUT_MAX = 900;
constexpr i32 SLN_MAX_FILES = 8;
constexpr i32 SLN_FNAME_MAX = 64;
constexpr i32 SLN_DIR_MAX = 128;
constexpr i32 SLN_NAME_MAX = 64;
constexpr i32 SLN_PANEL_W = 170;

struct SlnFileEntry {
  char name[SLN_FNAME_MAX];
  bool dirty;
};

struct SolutionState {
  bool active;
  char name[SLN_NAME_MAX];
  char dir[SLN_DIR_MAX];
  char sln_filename[SLN_FNAME_MAX];
  i32 type;  // 0=Console, 1=Window
  SlnFileEntry files[SLN_MAX_FILES];
  i32 file_count;
  i32 active_file;
  i32 explorer_scroll;
};

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
  bool ran_ok;
  bool has_output;
  // Save As
  bool saveas_open;
  char saveas_buf[64];
  i32 saveas_cursor;
  // Template chooser
  bool show_template;
  i32 template_sel;  // 0 = Console, 1 = Window, 2 = Solution
  // Solution
  SolutionState sln;
  bool sln_panel_open;
  // Add file dialog
  bool addfile_open;
  char addfile_buf[64];
  i32 addfile_cursor;
  // Solution name dialog (for creating new solution)
  bool slnname_open;
  char slnname_buf[64];
  i32 slnname_cursor;
  i32 slnname_type; // which template type for the solution
};

static_assert(sizeof(CSharpState) <= 8192, "CSharpState too large");

// ── Colours ─────────────────────────────────────────────────────────────────
constexpr u32 COL_BG = 0x001E1E2E;
constexpr u32 COL_TEXT = 0x00CDD6F4;
constexpr u32 COL_KW = 0x0089B4FA;
constexpr u32 COL_TYPE = 0x0094E2D5;
constexpr u32 COL_STR = 0x00A6E3A1;
constexpr u32 COL_NUM = 0x00FAB387;
constexpr u32 COL_COMMENT = 0x006C7086;
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
constexpr u32 COL_SLN_BG = 0x00181825;
constexpr u32 COL_SLN_HEADER = 0x00252538;
constexpr u32 COL_SLN_ACTIVE = 0x002A2A3E;
constexpr u32 COL_SLN_HOVER = 0x00222235;
constexpr u32 COL_SLN_SEP = 0x00313244;
constexpr u32 COL_SLN_FOLDER = 0x00FFCC44;
constexpr u32 COL_SLN_FILE = 0x0089B4FA;
constexpr u32 COL_SLN_TEXT = 0x00A6ADC8;
constexpr u32 COL_SLN_ADD = 0x00A6E3A1;
constexpr u32 COL_TOOLBAR = 0x00252535;
constexpr u32 COL_TB_BTN = 0x00333348;
constexpr u32 COL_TB_BTN_HOV = 0x00444460;
constexpr u32 COL_TB_TEXT = 0x00CDD6F4;
constexpr u32 COL_TB_RUN = 0x00A6E3A1;
constexpr u32 COL_TB_SEP = 0x00444455;
constexpr i32 TOOLBAR_H = 22;

// ── Helpers ─────────────────────────────────────────────────────────────────
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

// Check if a string ends with a suffix
bool ends_with(const char *s, const char *suffix) {
  usize sl = str::len(s), xl = str::len(suffix);
  if (xl > sl) return false;
  return str::cmp(s + sl - xl, suffix) == 0;
}

// ── Solution helpers ────────────────────────────────────────────────────────

void parse_sln(const char *content, SolutionState *sln) {
  sln->active = false;
  sln->file_count = 0;
  sln->active_file = -1;
  sln->explorer_scroll = 0;
  sln->type = 0;
  sln->name[0] = '\0';

  // Verify magic header
  if (str::ncmp(content, "#OguzSln v1", 11) != 0) return;

  const char *p = content;
  while (*p) {
    // Skip to start of line
    while (*p == '\n' || *p == '\r') p++;
    if (!*p) break;

    // Find end of line
    const char *eol = p;
    while (*eol && *eol != '\n' && *eol != '\r') eol++;
    i32 ll = static_cast<i32>(eol - p);

    // Parse line
    if (ll > 5 && str::ncmp(p, "Name=", 5) == 0) {
      i32 vl = ll - 5;
      if (vl > SLN_NAME_MAX - 1) vl = SLN_NAME_MAX - 1;
      str::memcpy(sln->name, p + 5, static_cast<usize>(vl));
      sln->name[vl] = '\0';
    } else if (ll > 5 && str::ncmp(p, "Type=", 5) == 0) {
      if (str::ncmp(p + 5, "Window", 6) == 0) sln->type = 1;
      else sln->type = 0;
    } else if (ll > 5 && str::ncmp(p, "File=", 5) == 0) {
      if (sln->file_count < SLN_MAX_FILES) {
        i32 vl = ll - 5;
        if (vl > SLN_FNAME_MAX - 1) vl = SLN_FNAME_MAX - 1;
        str::memcpy(sln->files[sln->file_count].name, p + 5, static_cast<usize>(vl));
        sln->files[sln->file_count].name[vl] = '\0';
        sln->files[sln->file_count].dirty = false;
        sln->file_count++;
      }
    }

    p = eol;
  }

  if (sln->name[0] && sln->file_count > 0) {
    sln->active = true;
    sln->active_file = 0;
  }
}

void serialize_sln(const SolutionState *sln, char *buf, i32 max) {
  buf[0] = '\0';
  str::cat(buf, "#OguzSln v1\n");
  str::cat(buf, "Name=");
  str::cat(buf, sln->name);
  str::cat(buf, "\nType=");
  str::cat(buf, sln->type == 1 ? "Window" : "Console");
  str::cat(buf, "\n");
  for (i32 i = 0; i < sln->file_count; i++) {
    if (static_cast<i32>(str::len(buf)) + 6 + static_cast<i32>(str::len(sln->files[i].name)) >= max - 2)
      break;
    str::cat(buf, "File=");
    str::cat(buf, sln->files[i].name);
    str::cat(buf, "\n");
  }
}

void save_sln_file(CSharpState *s) {
  if (!s->sln.active) return;
  char buf[1024];
  serialize_sln(&s->sln, buf, sizeof(buf));
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(s->sln.dir);
  fs::touch(s->sln.sln_filename);
  fs::write(s->sln.sln_filename, buf);
  fs::sync_to_disk();
  fs::cd(old);
}

// Save current file content to filesystem (used before switching files)
void flush_current_file(CSharpState *s) {
  if (!s->sln.active || s->sln.active_file < 0) return;
  if (!s->dirty) return;
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(s->sln.dir);
  const char *fname = s->sln.files[s->sln.active_file].name;
  fs::touch(fname);
  fs::write(fname, s->src);
  fs::sync_to_disk();
  fs::cd(old);
  s->dirty = false;
  s->sln.files[s->sln.active_file].dirty = false;
}

// Load a file from the solution into src buffer
void load_sln_file(CSharpState *s, i32 idx) {
  if (idx < 0 || idx >= s->sln.file_count) return;
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(s->sln.dir);
  const char *content = fs::cat(s->sln.files[idx].name);
  if (content) {
    usize cl = str::len(content);
    if (cl > static_cast<usize>(SRC_MAX - 1)) cl = static_cast<usize>(SRC_MAX - 1);
    str::memcpy(s->src, content, cl);
    s->src[cl] = '\0';
    s->src_len = static_cast<i32>(cl);
  } else {
    s->src[0] = '\0';
    s->src_len = 0;
  }
  fs::cd(old);

  s->sln.active_file = idx;
  s->cursor = 0;
  s->scroll_y = 0;
  s->sel_start = -1;
  s->dirty = false;

  // Update filepath/filename
  str::cpy(s->filepath, s->sln.dir);
  str::cat(s->filepath, "/");
  str::cat(s->filepath, s->sln.files[idx].name);
  str::ncpy(s->filename, s->sln.files[idx].name, 63);
}

void switch_file(CSharpState *s, i32 idx) {
  if (idx == s->sln.active_file) return;
  if (s->dirty) {
    s->sln.files[s->sln.active_file].dirty = true;
    flush_current_file(s);
  }
  load_sln_file(s, idx);
}

void load_solution(CSharpState *s, const char *sln_path, const char *content) {
  parse_sln(content, &s->sln);
  if (!s->sln.active) return;

  // Extract directory and filename from sln_path
  split_path(sln_path, s->sln.dir, sizeof(s->sln.dir),
             s->sln.sln_filename, sizeof(s->sln.sln_filename));

  s->sln_panel_open = true;
  s->show_template = false;

  // Load first file
  load_sln_file(s, 0);
}

void add_file_to_sln(CSharpState *s, const char *filename) {
  if (!s->sln.active || s->sln.file_count >= SLN_MAX_FILES) return;

  // Add to file list
  str::ncpy(s->sln.files[s->sln.file_count].name, filename, SLN_FNAME_MAX - 1);
  s->sln.files[s->sln.file_count].dirty = false;
  s->sln.file_count++;

  // Create the file on disk
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(s->sln.dir);
  fs::touch(filename);
  // Write a basic template
  if (ends_with(filename, ".csg")) {
    fs::write(filename, "using System;\n\nclass NewApp {\n    static void Main() {\n    }\n\n"
              "    static void OnDraw(int w, int h) {\n        Gfx.Clear(0xF0F0F0);\n    }\n}\n");
  } else {
    fs::write(filename, "using System;\n\nclass NewClass {\n    static void Main() {\n"
              "        Console.WriteLine(\"Hello!\");\n    }\n}\n");
  }
  fs::sync_to_disk();
  fs::cd(old);

  // Save updated .sln
  save_sln_file(s);

  // Switch to the new file
  switch_file(s, s->sln.file_count - 1);
}

void remove_file_from_sln(CSharpState *s, i32 idx) {
  if (!s->sln.active || idx < 0 || idx >= s->sln.file_count) return;
  if (s->sln.file_count <= 1) return; // keep at least one file

  // Remove the file from disk
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd(s->sln.dir);
  fs::rm(s->sln.files[idx].name);
  fs::sync_to_disk();
  fs::cd(old);

  // Shift entries
  for (i32 i = idx; i < s->sln.file_count - 1; i++)
    s->sln.files[i] = s->sln.files[i + 1];
  s->sln.file_count--;

  // Adjust active file
  if (s->sln.active_file >= s->sln.file_count)
    s->sln.active_file = s->sln.file_count - 1;
  if (s->sln.active_file == idx || idx <= s->sln.active_file)
    load_sln_file(s, s->sln.active_file >= s->sln.file_count ? 0 : s->sln.active_file);

  save_sln_file(s);
}

// Create a new solution project on disk
void create_solution(CSharpState *s, const char *name, i32 type) {
  // Create directory /home/Desktop/<name>/
  char old[256]; fs::get_cwd(old, sizeof(old));
  fs::cd("/home/Desktop");
  fs::mkdir(name);
  fs::cd(name);

  // Create main file
  const char *main_name = (type == 1) ? "Program.csg" : "Program.cs";
  fs::touch(main_name);
  if (type == 1) {
    fs::write(main_name,
      "using System;\n\nclass MyApp {\n"
      "    static Button btn;\n    static Label lbl;\n"
      "    static TextBox txt;\n    static CheckBox chk;\n\n"
      "    static void Main() {\n"
      "        lbl = new Label(20, 15, \"My First App\");\n"
      "        txt = new TextBox(20, 40, 200, 24);\n"
      "        btn = new Button(20, 80, 120, 30, \"Click Me\");\n"
      "        chk = new CheckBox(20, 125, \"Dark Mode\");\n"
      "    }\n\n"
      "    static void OnDraw(int w, int h) {\n"
      "        Gfx.Clear(0xF0F0F0);\n"
      "        Gfx.Rect(0, 0, w, h, 0x999999);\n"
      "        lbl.Draw();\n        txt.Draw();\n"
      "        btn.Draw();\n        chk.Draw();\n"
      "    }\n\n"
      "    static void OnClick(int x, int y) {\n"
      "        if (btn.HitTest(x, y)) {\n"
      "            lbl.SetText(\"Hello, \" + txt.GetText() + \"!\");\n"
      "        }\n"
      "        if (chk.HitTest(x, y)) {\n"
      "            chk.Toggle();\n"
      "        }\n"
      "        txt.Click(x, y);\n"
      "    }\n\n"
      "    static void OnKey(int key) {\n"
      "        txt.Key(key);\n"
      "    }\n}\n");
  } else {
    fs::write(main_name,
      "using System;\n\nclass Program {\n    static void Main() {\n"
      "        Console.WriteLine(\"Hello, OguzOS!\");\n\n"
      "        for (int i = 1; i <= 5; i++) {\n"
      "            Console.WriteLine(\"Count: \" + i);\n"
      "        }\n    }\n}\n");
  }

  // Create .sln file
  char sln_name[128];
  str::cpy(sln_name, name);
  str::cat(sln_name, ".sln");

  // Build sln content
  s->sln.active = true;
  str::ncpy(s->sln.name, name, SLN_NAME_MAX - 1);
  s->sln.type = type;
  s->sln.file_count = 1;
  str::ncpy(s->sln.files[0].name, main_name, SLN_FNAME_MAX - 1);
  s->sln.files[0].dirty = false;
  s->sln.active_file = 0;
  s->sln.explorer_scroll = 0;

  char sln_content[512];
  serialize_sln(&s->sln, sln_content, sizeof(sln_content));
  fs::touch(sln_name);
  fs::write(sln_name, sln_content);
  fs::sync_to_disk();

  // Set sln paths
  str::cpy(s->sln.dir, "/home/Desktop/");
  str::cat(s->sln.dir, name);
  str::ncpy(s->sln.sln_filename, sln_name, SLN_FNAME_MAX - 1);

  fs::cd(old);

  // Load the first file
  s->sln_panel_open = true;
  s->show_template = false;
  load_sln_file(s, 0);
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

  // Check if inside a comment — scan backward on current line for //
  i32 line_s = pos;
  while (line_s > 0 && text[line_s - 1] != '\n') line_s--;
  for (i32 i = line_s; i < pos; i++) {
    if (text[i] == '/' && i + 1 < len && text[i + 1] == '/')
      return COL_COMMENT;
  }

  // String literal
  i32 quotes = 0;
  for (i32 i = line_s; i < pos; i++) if (text[i] == '"') quotes++;
  if (quotes % 2 == 1) return COL_STR;
  if (c == '"') return COL_STR;

  // Number
  if (c >= '0' && c <= '9') return COL_NUM;

  // Word-based coloring
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
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

// ── Templates ───────────────────────────────────────────────────────────────
const char *TPL_CONSOLE =
  "using System;\n\nclass Program {\n    static void Main() {\n"
  "        Console.WriteLine(\"Hello, OguzOS!\");\n\n"
  "        for (int i = 1; i <= 5; i++) {\n"
  "            Console.WriteLine(\"Count: \" + i);\n"
  "        }\n    }\n}\n";

const char *TPL_WINDOW =
  "using System;\n\nclass MyApp {\n"
  "    static Button btn;\n    static Label lbl;\n"
  "    static TextBox txt;\n    static CheckBox chk;\n\n"
  "    static void Main() {\n"
  "        lbl = new Label(20, 15, \"My First App\");\n"
  "        txt = new TextBox(20, 40, 200, 24);\n"
  "        btn = new Button(20, 80, 120, 30, \"Click Me\");\n"
  "        chk = new CheckBox(20, 125, \"Dark Mode\");\n"
  "    }\n\n"
  "    static void OnDraw(int w, int h) {\n"
  "        Gfx.Clear(0xF0F0F0);\n"
  "        Gfx.Rect(0, 0, w, h, 0x999999);\n"
  "        lbl.Draw();\n        txt.Draw();\n"
  "        btn.Draw();\n        chk.Draw();\n"
  "    }\n\n"
  "    static void OnClick(int x, int y) {\n"
  "        if (btn.HitTest(x, y)) {\n"
  "            lbl.SetText(\"Hello, \" + txt.GetText() + \"!\");\n"
  "        }\n"
  "        if (chk.HitTest(x, y)) {\n"
  "            chk.Toggle();\n"
  "        }\n"
  "        txt.Click(x, y);\n"
  "    }\n\n"
  "    static void OnKey(int key) {\n"
  "        txt.Key(key);\n"
  "    }\n}\n";

constexpr i32 TPL_COUNT = 3;
const char *TPL_NAMES[] = {"Console App (.cs)", "Window App (.csg)", "Solution (.sln)"};
const char *TPL_DESCS[] = {
  "Command-line program with\nConsole.WriteLine output.\nRun with F5.",
  "GUI application with windows,\nbuttons, labels, and text input.\nRun with F6.",
  "Multi-file project with\nsolution explorer panel.\nOrganize your code."
};

// ── Solution explorer panel drawing ─────────────────────────────────────────
void draw_sln_panel(CSharpState *s, i32 px, i32 py, i32 pw, i32 ph) {
  i32 fw = gfx::font_w(), fh = gfx::font_h(), LH = fh + 4;

  // Background
  gfx::fill_rect(px, py, pw, ph, COL_SLN_BG);

  // Header
  i32 hdr_h = fh + 8;
  gfx::fill_rect(px, py, pw, hdr_h, COL_SLN_HEADER);
  gfx::draw_text(px + 4, py + 4, "EXPLORER", COL_SLN_TEXT, COL_SLN_HEADER);

  // Solution name with folder icon
  i32 y = py + hdr_h + 4;
  gfx::fill_rect(px + 6, y + 1, 10, 8, COL_SLN_FOLDER);
  gfx::fill_rect(px + 6, y - 1, 5, 3, COL_SLN_FOLDER);
  char sln_label[80]; sln_label[0] = '\0';
  str::cat(sln_label, s->sln.name);
  i32 max_chars = (pw - 28) / fw;
  if (static_cast<i32>(str::len(sln_label)) > max_chars && max_chars > 3) {
    sln_label[max_chars - 2] = '.';
    sln_label[max_chars - 1] = '.';
    sln_label[max_chars] = '\0';
  }
  gfx::draw_text(px + 20, y, sln_label, COL_TEXT, COL_SLN_BG);
  y += LH + 2;

  // Separator
  gfx::hline(px + 4, y, pw - 8, COL_SLN_SEP);
  y += 4;

  // File list
  for (i32 i = 0; i < s->sln.file_count && y + LH < py + ph - LH; i++) {
    bool active = (i == s->sln.active_file);
    u32 bg = active ? COL_SLN_ACTIVE : COL_SLN_BG;

    if (active)
      gfx::fill_rect(px + 2, y - 1, pw - 4, LH, bg);

    // File icon (small C# icon)
    u32 icon_col = COL_SLN_FILE;
    if (ends_with(s->sln.files[i].name, ".csg")) icon_col = COL_SLN_ADD;
    gfx::fill_rect(px + 14, y + 2, 8, 10, icon_col);
    gfx::draw_char(px + 15, y + 1, 'C', 0x00FFFFFF, icon_col);

    // File name
    char flabel[48];
    str::ncpy(flabel, s->sln.files[i].name, 40);
    if (s->sln.files[i].dirty) str::cat(flabel, " *");
    u32 txt_col = active ? COL_TEXT : COL_SLN_TEXT;
    gfx::draw_text(px + 26, y + 1, flabel, txt_col, bg);

    y += LH;
  }

  // Buttons at bottom
  i32 btn_y = py + ph - LH * 2 - 8;
  gfx::hline(px + 4, btn_y - 2, pw - 8, COL_SLN_SEP);
  gfx::draw_text(px + 8, btn_y + 1, "+ Add File", COL_SLN_ADD, COL_SLN_BG);
  i32 rm_y = btn_y + LH + 2;
  u32 rm_col = (s->sln.file_count > 1) ? COL_OUT_ERR : COL_COMMENT;
  gfx::draw_text(px + 8, rm_y + 1, "- Remove File", rm_col, COL_SLN_BG);

  // Vertical separator on the right edge
  gfx::fill_rect(px + pw - 1, py, 1, ph, COL_SLN_SEP);
}

// ── Callbacks ───────────────────────────────────────────────────────────────
void csharp_open(u8 *state) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  s->src[0] = '\0';
  s->src_len = 0;
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
  s->show_template = true;
  s->template_sel = 0;
  // Solution
  s->sln.active = false;
  s->sln.file_count = 0;
  s->sln.active_file = -1;
  s->sln.explorer_scroll = 0;
  s->sln.name[0] = '\0';
  s->sln.dir[0] = '\0';
  s->sln.sln_filename[0] = '\0';
  s->sln_panel_open = false;
  s->addfile_open = false;
  s->addfile_buf[0] = '\0';
  s->addfile_cursor = 0;
  s->slnname_open = false;
  s->slnname_buf[0] = '\0';
  s->slnname_cursor = 0;
  s->slnname_type = 0;
}

void draw_dialog(i32 cx, i32 cy, i32 cw, i32 ch,
                 const char *title, const char *label,
                 const char *buf, i32 buf_cursor,
                 const char *hint) {
  i32 fw = gfx::font_w(), fh = gfx::font_h();
  i32 dw = 340, dh = 100;
  i32 dx = cx + (cw - dw) / 2, dy = cy + (ch - dh) / 2;
  gfx::fill_rect(dx + 3, dy + 3, dw, dh, 0x00111111);
  gfx::fill_rect(dx, dy, dw, dh, 0x002D2D3D);
  gfx::rect(dx, dy, dw, dh, 0x00666666);
  gfx::fill_rect(dx, dy, dw, 24, COL_KW);
  gfx::draw_text(dx + 8, dy + (24 - fh) / 2, title, 0x00FFFFFF, COL_KW);
  gfx::draw_text(dx + 10, dy + 32, label, COL_TEXT, 0x002D2D3D);
  i32 fx = dx + 10, fy = dy + 50, field_w = dw - 20;
  gfx::fill_rect(fx, fy, field_w, fh + 6, COL_BG);
  gfx::rect(fx, fy, field_w, fh + 6, 0x00666666);
  gfx::draw_text(fx + 4, fy + 3, buf, COL_TEXT, COL_BG);
  i32 cur_x = fx + 4 + buf_cursor * fw;
  if (cur_x < fx + field_w - 2)
    gfx::fill_rect(cur_x, fy + 2, 1, fh + 2, COL_CURSOR);
  gfx::draw_text(dx + 10, dy + dh - fh - 6, hint, COL_COMMENT, 0x002D2D3D);
}

void csharp_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  // ── Template chooser screen ──
  if (s->show_template) {
    i32 fw = gfx::font_w(), fh = gfx::font_h();
    gfx::fill_rect(cx, cy, cw, ch, 0x001E1E2E);

    const char *title = "Create New Project";
    i32 tw = gfx::text_width(title);
    gfx::draw_text(cx + (cw - tw) / 2, cy + 20, title, 0x0089B4FA, 0x001E1E2E);

    i32 card_w = 200, card_h = 100, card_gap = 16;
    i32 total_w = TPL_COUNT * card_w + (TPL_COUNT - 1) * card_gap;
    i32 start_x = cx + (cw - total_w) / 2;
    i32 card_y = cy + 60;

    for (i32 i = 0; i < TPL_COUNT; i++) {
      i32 card_x = start_x + i * (card_w + card_gap);
      bool sel = (s->template_sel == i);
      u32 bg = sel ? 0x00313244 : 0x00232336;
      u32 border = sel ? 0x0089B4FA : 0x00585B70;

      gfx::fill_rect(card_x, card_y, card_w, card_h, bg);
      gfx::rect(card_x, card_y, card_w, card_h, border);
      if (sel) gfx::rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, border);

      // Icons
      if (i == 0) {
        gfx::fill_rect(card_x + 12, card_y + 12, 36, 28, 0x00181825);
        gfx::draw_text(card_x + 15, card_y + 17, ">_", 0x00A6E3A1, 0x00181825);
      } else if (i == 1) {
        gfx::fill_rect(card_x + 12, card_y + 12, 36, 28, 0x00585B70);
        gfx::fill_rect(card_x + 12, card_y + 12, 36, 8, 0x0089B4FA);
        gfx::fill_rect(card_x + 40, card_y + 13, 6, 6, 0x00F38BA8);
      } else {
        // Solution icon: folder with files
        gfx::fill_rect(card_x + 12, card_y + 14, 20, 14, COL_SLN_FOLDER);
        gfx::fill_rect(card_x + 12, card_y + 12, 10, 4, COL_SLN_FOLDER);
        gfx::fill_rect(card_x + 28, card_y + 18, 16, 16, COL_SLN_FILE);
        gfx::draw_char(card_x + 30, card_y + 19, 'C', 0x00FFFFFF, COL_SLN_FILE);
      }

      gfx::draw_text(card_x + 56, card_y + 14, TPL_NAMES[i],
                      sel ? 0x00CDD6F4 : 0x00A6ADC8, bg);

      const char *desc = TPL_DESCS[i];
      i32 dy = card_y + 36;
      i32 dx = card_x + 14;
      while (*desc && dy + fh < card_y + card_h - 4) {
        if (*desc == '\n') { dy += fh + 2; dx = card_x + 14; desc++; continue; }
        if (dx + fw <= card_x + card_w - 8) {
          gfx::draw_char(dx, dy, *desc, 0x006C7086, bg);
          dx += fw;
        }
        desc++;
      }
    }

    const char *hint = "Left/Right to select, Enter to create";
    i32 hw = gfx::text_width(hint);
    gfx::draw_text(cx + (cw - hw) / 2, card_y + card_h + 20,
                    hint, 0x006C7086, 0x001E1E2E);

    // Solution creation dialog overlay
    if (s->slnname_open) {
      i32 fw2 = gfx::font_w(), fh2 = gfx::font_h();
      i32 dw = 360, dh = 150;
      i32 dx = cx + (cw - dw) / 2, dy = cy + (ch - dh) / 2;
      gfx::fill_rect(dx + 3, dy + 3, dw, dh, 0x00111111);
      gfx::fill_rect(dx, dy, dw, dh, 0x002D2D3D);
      gfx::rect(dx, dy, dw, dh, 0x00666666);
      gfx::fill_rect(dx, dy, dw, 24, COL_KW);
      gfx::draw_text(dx + 8, dy + (24 - fh2) / 2, "New Solution", 0x00FFFFFF, COL_KW);
      // Name field
      gfx::draw_text(dx + 10, dy + 32, "Project name:", COL_TEXT, 0x002D2D3D);
      i32 fx = dx + 10, fy = dy + 50, field_w = dw - 20;
      gfx::fill_rect(fx, fy, field_w, fh2 + 6, COL_BG);
      gfx::rect(fx, fy, field_w, fh2 + 6, 0x00666666);
      gfx::draw_text(fx + 4, fy + 3, s->slnname_buf, COL_TEXT, COL_BG);
      i32 cur_x = fx + 4 + s->slnname_cursor * fw2;
      if (cur_x < fx + field_w - 2)
        gfx::fill_rect(cur_x, fy + 2, 1, fh2 + 2, COL_CURSOR);
      // Type selector
      i32 ty = fy + fh2 + 14;
      gfx::draw_text(dx + 10, ty, "Type:", COL_TEXT, 0x002D2D3D);
      i32 opt_x = dx + 10 + 6 * fw2;
      // Console option
      bool is_con = (s->slnname_type == 0);
      u32 con_bg = is_con ? COL_KW : 0x00444455;
      u32 con_fg = is_con ? 0x00FFFFFF : COL_SLN_TEXT;
      i32 con_w = gfx::text_width(" Console ") + 4;
      gfx::fill_rect(opt_x, ty - 2, con_w, fh2 + 4, con_bg);
      gfx::draw_text(opt_x + 2, ty, " Console ", con_fg, con_bg);
      // Window option
      i32 win_x = opt_x + con_w + 8;
      bool is_win = (s->slnname_type == 1);
      u32 win_bg = is_win ? COL_KW : 0x00444455;
      u32 win_fg = is_win ? 0x00FFFFFF : COL_SLN_TEXT;
      i32 win_w = gfx::text_width(" Window ") + 4;
      gfx::fill_rect(win_x, ty - 2, win_w, fh2 + 4, win_bg);
      gfx::draw_text(win_x + 2, ty, " Window ", win_fg, win_bg);
      // Hint
      gfx::draw_text(dx + 10, dy + dh - fh2 - 6,
                      "Tab=Switch Type  Enter=Create  Esc=Cancel",
                      COL_COMMENT, 0x002D2D3D);
    }
    return;
  }

  i32 fw = gfx::font_w(), fh = gfx::font_h(), LH = fh + 2;
  i32 LNW = fw * 4;
  i32 STATUS_H = fh + 6;
  i32 SPLIT_H = 4;

  // Solution explorer panel offset
  i32 panel_w = (s->sln_panel_open && s->sln.active) ? SLN_PANEL_W : 0;
  i32 ex = cx + panel_w; // editor x origin
  i32 ew = cw - panel_w; // editor width

  // Draw solution explorer panel
  if (s->sln_panel_open && s->sln.active) {
    draw_sln_panel(s, cx, cy, panel_w, ch);
  }

  // ── Toolbar ──
  i32 tb_y = cy;
  gfx::fill_rect(ex, tb_y, ew, TOOLBAR_H, COL_TOOLBAR);

  // Toolbar buttons
  i32 bx = ex + 4, by = tb_y + 2, bh = TOOLBAR_H - 4;
  auto draw_tb_btn = [&](const char *label, u32 bg, u32 fg) -> i32 {
    i32 bw = gfx::text_width(label) + 12;
    gfx::fill_rect(bx, by, bw, bh, bg);
    gfx::draw_text(bx + 6, by + 1, label, fg, bg);
    i32 old_bx = bx;
    bx += bw + 4;
    (void)old_bx;
    return bw;
  };

  // Run button (green accent)
  draw_tb_btn(s->sln.active ? (s->sln.type == 1 ? "> Run GUI" : "> Run") : "> Run",
              COL_TB_BTN, COL_TB_RUN);

  // Save button
  draw_tb_btn("Save", COL_TB_BTN, COL_TB_TEXT);

  // Save As button
  draw_tb_btn("Save As", COL_TB_BTN, COL_TB_TEXT);

  // Separator
  gfx::fill_rect(bx, by + 2, 1, bh - 4, COL_TB_SEP);
  bx += 6;

  // Solution-specific buttons
  if (s->sln.active) {
    draw_tb_btn(s->sln_panel_open ? "Hide Explorer" : "Show Explorer",
                COL_TB_BTN, COL_TB_TEXT);
    draw_tb_btn("+ File", COL_TB_BTN, COL_SLN_ADD);
  }

  // Bottom separator line for toolbar
  gfx::hline(ex, tb_y + TOOLBAR_H - 1, ew, COL_TB_SEP);

  // Layout: editor top 60%, output bottom 40% (below toolbar)
  i32 area_y = cy + TOOLBAR_H;
  i32 area_h = ch - TOOLBAR_H;
  i32 editor_h = (area_h - STATUS_H - SPLIT_H) * 6 / 10;
  i32 output_h = area_h - STATUS_H - SPLIT_H - editor_h;
  i32 split_y = area_y + editor_h;
  i32 out_y = split_y + SPLIT_H;
  i32 status_y = cy + ch - STATUS_H;

  // ── Status bar ──
  gfx::fill_rect(ex, status_y, ew, STATUS_H, COL_STATUS);
  char status[128]; status[0] = '\0';
  if (s->sln.active) {
    str::cat(status, s->sln.name);
    str::cat(status, " > ");
  }
  if (s->filename[0]) str::cat(status, s->filename);
  else str::cat(status, "Untitled.cs");
  if (s->dirty) str::cat(status, " *");
  str::cat(status, "  |  Ln ");
  append_int(status, count_lines(s->src, s->cursor) + 1);
  str::cat(status, ", Col ");
  append_int(status, cursor_col(s->src, s->cursor) + 1);
  gfx::draw_text(ex + 4, status_y + 3, status, COL_STATUS_TEXT, COL_STATUS);

  // ── Splitter ──
  gfx::fill_rect(ex, split_y, ew, SPLIT_H, COL_SPLITTER);
  gfx::draw_text(ex + 4, split_y, "Output", COL_STATUS_TEXT, COL_SPLITTER);

  // ── Output panel ──
  gfx::fill_rect(ex, out_y, ew, output_h, COL_OUT_BG);
  if (s->has_output) {
    u32 ocol = s->ran_ok ? COL_OUT_TEXT : COL_OUT_ERR;
    i32 ox = ex + 4, oy = out_y + 2;
    for (i32 i = 0; s->output[i] && oy + fh <= out_y + output_h; i++) {
      if (s->output[i] == '\n') {
        ox = ex + 4; oy += LH;
      } else {
        if (ox + fw <= ex + ew)
          gfx::draw_char(ox, oy, s->output[i], ocol, COL_OUT_BG);
        ox += fw;
      }
    }
  } else {
    gfx::draw_text(ex + 4, out_y + 2, "Press F5 to run", COL_COMMENT, COL_OUT_BG);
  }

  // ── Editor ──
  i32 vis_lines = editor_h / LH;
  gfx::fill_rect(ex, area_y, LNW, editor_h, COL_LINENUM_BG);
  gfx::fill_rect(ex + LNW, area_y, ew - LNW, editor_h, COL_BG);

  i32 cl = count_lines(s->src, s->cursor);
  if (cl < s->scroll_y) s->scroll_y = cl;
  if (cl >= s->scroll_y + vis_lines) s->scroll_y = cl - vis_lines + 1;

  i32 slo = -1, shi = -1;
  sel_range(s, slo, shi);

  i32 line = 0, col = 0;
  i32 text_x = ex + LNW + 4;

  for (i32 i = 0; i <= s->src_len; i++) {
    bool at_end = (i == s->src_len);
    char cc = at_end ? '\0' : s->src[i];

    if (line >= s->scroll_y && line < s->scroll_y + vis_lines) {
      i32 py = area_y + (line - s->scroll_y) * LH;

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
        gfx::draw_text(ex + xoff, py + 1, lnb, COL_LINENUM, COL_LINENUM_BG);
      }

      bool in_sel = (slo >= 0 && i >= slo && i < shi);
      if (in_sel && !at_end && cc != '\n')
        gfx::fill_rect(text_x + col * fw, py, fw, LH, COL_SEL);

      if (i == s->cursor)
        gfx::fill_rect(text_x + col * fw, py, 2, LH, COL_CURSOR);

      if (!at_end && cc != '\n' && text_x + col * fw + fw <= ex + ew) {
        u32 fg = in_sel ? 0x00FFFFFF : get_char_color(s->src, s->src_len, i);
        u32 bg = in_sel ? COL_SEL : COL_BG;
        gfx::draw_char(text_x + col * fw, py + 1, cc, fg, bg);
      }
    }

    if (cc == '\n') { line++; col = 0; }
    else if (!at_end) col++;
  }

  // ── Dialogs (drawn on top) ──
  if (s->saveas_open) {
    draw_dialog(cx, cy, cw, ch, "Save As", "Path:",
                s->saveas_buf, s->saveas_cursor, "Enter=Save  Esc=Cancel");
  }
  if (s->addfile_open) {
    draw_dialog(cx, cy, cw, ch, "Add File", "Filename:",
                s->addfile_buf, s->addfile_cursor, "Enter=Add  Esc=Cancel");
  }
}

void apply_template(CSharpState *s, i32 tpl) {
  if (tpl == 2) {
    // Solution project — show name dialog
    s->slnname_open = true;
    str::cpy(s->slnname_buf, "MyProject");
    s->slnname_cursor = static_cast<i32>(str::len(s->slnname_buf));
    s->slnname_type = 0; // default Console solution
    return;
  }
  const char *code = (tpl == 0) ? TPL_CONSOLE : TPL_WINDOW;
  str::ncpy(s->src, code, SRC_MAX - 1);
  s->src_len = static_cast<i32>(str::len(s->src));
  s->cursor = 0;
  s->scroll_y = 0;
  s->sel_start = -1;
  s->dirty = false;
  s->show_template = false;
}

// Handle text input for a dialog field
bool dialog_key(char key, char *buf, i32 &cursor, i32 max_len) {
  if (key == 0x7F || key == 0x08) {
    if (cursor > 0) { cursor--; buf[cursor] = '\0'; }
    return true;
  }
  if (key >= 32 && key <= 126 && cursor < max_len - 2) {
    buf[cursor++] = key;
    buf[cursor] = '\0';
    return true;
  }
  return true;
}

bool csharp_key(u8 *state, char key) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  // Solution name dialog (from template chooser)
  if (s->slnname_open) {
    if (key == 0x1B) { s->slnname_open = false; return true; }
    if (key == '\t') {
      s->slnname_type = (s->slnname_type == 0) ? 1 : 0;
      return true;
    }
    if (key == '\r' || key == '\n') {
      if (s->slnname_buf[0]) {
        create_solution(s, s->slnname_buf, s->slnname_type);
        s->slnname_open = false;
      }
      return true;
    }
    dialog_key(key, s->slnname_buf, s->slnname_cursor, 62);
    return true;
  }

  // Template chooser
  if (s->show_template) {
    if (key == '\r' || key == '\n') {
      apply_template(s, s->template_sel);
      return true;
    }
    return true;
  }

  // Add file dialog
  if (s->addfile_open) {
    if (key == 0x1B) { s->addfile_open = false; return true; }
    if (key == '\r' || key == '\n') {
      if (s->addfile_buf[0]) {
        add_file_to_sln(s, s->addfile_buf);
        s->addfile_open = false;
      }
      return true;
    }
    dialog_key(key, s->addfile_buf, s->addfile_cursor, 62);
    return true;
  }

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

  // Ctrl+E: Toggle solution explorer
  if (key == 0x05) {
    if (s->sln.active)
      s->sln_panel_open = !s->sln_panel_open;
    return true;
  }

  // Ctrl+N: Add new file to solution
  if (key == 0x0E) {
    if (s->sln.active && s->sln.file_count < SLN_MAX_FILES) {
      s->addfile_open = true;
      str::cpy(s->addfile_buf, "NewFile.cs");
      s->addfile_cursor = static_cast<i32>(str::len(s->addfile_buf));
    }
    return true;
  }

  // F5 / Ctrl+R = Run (auto-detects console vs GUI mode)
  if (key == 0x12) {
    // Save current file first
    if (s->sln.active) flush_current_file(s);
    else if (s->filepath[0]) save_file(s);

    // Determine run mode and build the merged source
    bool gui_mode = false;
    // Merged source buffer: concatenate all solution files
    // (entry file first so Main() is found in the right class)
    constexpr i32 MERGED_MAX = 4096;
    static char merged_src[MERGED_MAX];
    const char *run_src = s->src; // default: current buffer

    if (s->sln.active) {
      gui_mode = (s->sln.type == 1);

      // Find the entry file index
      const char *ext = gui_mode ? ".csg" : ".cs";
      i32 entry_idx = -1;
      for (i32 i = 0; i < s->sln.file_count; i++) {
        if (ends_with(s->sln.files[i].name, ext)) { entry_idx = i; break; }
      }
      if (entry_idx < 0) {
        s->has_output = true;
        s->ran_ok = false;
        str::cpy(s->output, "Error: no ");
        str::cat(s->output, ext);
        str::cat(s->output, " entry file in solution.\n");
        s->out_scroll = 0;
        return true;
      }

      // Concatenate all files: entry file first, then the rest
      merged_src[0] = '\0';
      i32 mlen = 0;
      char old[256]; fs::get_cwd(old, sizeof(old));
      fs::cd(s->sln.dir);

      // Helper: append a file's content to merged_src
      auto append_file = [&](i32 idx) {
        const char *content;
        if (idx == s->sln.active_file)
          content = s->src; // use in-memory version (may have unsaved edits)
        else
          content = fs::cat(s->sln.files[idx].name);
        if (!content) return;
        usize cl = str::len(content);
        if (mlen + static_cast<i32>(cl) + 2 >= MERGED_MAX) return; // skip if no room
        if (mlen > 0) { merged_src[mlen++] = '\n'; }
        str::memcpy(merged_src + mlen, content, cl);
        mlen += static_cast<i32>(cl);
        merged_src[mlen] = '\0';
      };

      // Entry file first
      append_file(entry_idx);
      // Then all other files
      for (i32 i = 0; i < s->sln.file_count; i++) {
        if (i != entry_idx) append_file(i);
      }

      fs::cd(old);
      run_src = merged_src;
    } else if (s->filepath[0]) {
      gui_mode = ends_with(s->filepath, ".csg");
    }

    if (gui_mode) {
      // GUI mode: validate first using init(), then launch if OK
      bool valid = csharp::init(run_src);
      csharp::gui_cleanup();
      s->has_output = true;
      if (!valid) {
        s->ran_ok = false;
        csharp::run(run_src, s->output, OUT_MAX);
        s->out_scroll = 0;
      } else {
        // Build the .csg path for launching
        char gui_path[128];
        if (s->sln.active) {
          str::cpy(gui_path, s->sln.dir);
          str::cat(gui_path, "/");
          // Find the .csg entry filename
          for (i32 i = 0; i < s->sln.file_count; i++) {
            if (ends_with(s->sln.files[i].name, ".csg")) {
              str::cat(gui_path, s->sln.files[i].name);
              break;
            }
          }
        } else if (s->filepath[0]) {
          str::ncpy(gui_path, s->filepath, 127);
          usize pl = str::len(gui_path);
          if (pl > 3 && str::cmp(gui_path + pl - 3, ".cs") == 0) {
            gui_path[pl] = 'g';
            gui_path[pl + 1] = '\0';
          }
        } else {
          str::cpy(gui_path, "/tmp/app.csg");
        }
        // Save merged source to the .csg file so csgui host sees everything
        char gdir[128], gname[64];
        split_path(gui_path, gdir, sizeof(gdir), gname, sizeof(gname));
        char old2[256]; fs::get_cwd(old2, sizeof(old2));
        fs::cd(gdir); fs::touch(gname); fs::write(gname, run_src);
        fs::sync_to_disk(); fs::cd(old2);
        gui::open_file(gui_path, run_src);
        s->ran_ok = true;
        str::cpy(s->output, "Build succeeded. GUI app launched.\n");
      }
    } else {
      // Console mode: run the entry file source
      s->ran_ok = csharp::run(run_src, s->output, OUT_MAX);
      s->has_output = true;
      s->out_scroll = 0;
    }
    return true;
  }

  // Ctrl+S
  if (key == 0x13) {
    if (s->sln.active) {
      flush_current_file(s);
      save_sln_file(s);
    } else if (s->filepath[0]) {
      save_file(s);
    } else {
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

  // Template chooser: left/right to switch
  if (s->show_template) {
    if (dir == 'C' && s->template_sel < TPL_COUNT - 1) s->template_sel++;
    else if (dir == 'D' && s->template_sel > 0) s->template_sel--;
    return;
  }

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

void csharp_close(u8 *state) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  // Auto-save on close if in a solution
  if (s->sln.active && s->dirty) {
    flush_current_file(s);
    save_sln_file(s);
  }
}

void csharp_click(u8 *state, i32 rx, i32 ry, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  // Template chooser: click on a card to select + create
  if (s->show_template) {
    // If solution name dialog is open, handle clicks on type buttons
    if (s->slnname_open) {
      i32 fw2 = gfx::font_w(), fh2 = gfx::font_h();
      i32 dw = 360, dh = 150;
      i32 dx = (cw - dw) / 2, dy = (ch - dh) / 2;
      i32 fy = dy + 50;
      i32 ty = fy + fh2 + 14;
      i32 opt_x = dx + 10 + 6 * fw2;
      i32 con_w = gfx::text_width(" Console ") + 4;
      i32 win_x = opt_x + con_w + 8;
      i32 win_w = gfx::text_width(" Window ") + 4;
      if (ry >= ty - 2 && ry < ty + fh2 + 4) {
        if (rx >= opt_x && rx < opt_x + con_w) { s->slnname_type = 0; return; }
        if (rx >= win_x && rx < win_x + win_w) { s->slnname_type = 1; return; }
      }
      return;
    }
    i32 card_w = 200, card_h = 100, card_gap = 16;
    i32 total_w = TPL_COUNT * card_w + (TPL_COUNT - 1) * card_gap;
    i32 start_x = (cw - total_w) / 2;
    i32 card_y = 60;
    for (i32 i = 0; i < TPL_COUNT; i++) {
      i32 card_x = start_x + i * (card_w + card_gap);
      if (rx >= card_x && rx < card_x + card_w &&
          ry >= card_y && ry < card_y + card_h) {
        s->template_sel = i;
        apply_template(s, i);
        return;
      }
    }
    return;
  }

  if (s->saveas_open || s->addfile_open) return;

  i32 panel_w = (s->sln_panel_open && s->sln.active) ? SLN_PANEL_W : 0;

  // ── Toolbar click handling ──
  if (ry < TOOLBAR_H && rx >= panel_w) {
    // Replay the button layout to find which was clicked
    i32 bx2 = panel_w + 4;
    auto btn_w = [&](const char *label) -> i32 { return gfx::text_width(label) + 12; };

    // Run button
    const char *run_label = s->sln.active ? (s->sln.type == 1 ? "> Run GUI" : "> Run") : "> Run";
    i32 rw = btn_w(run_label);
    if (rx >= bx2 && rx < bx2 + rw) {
      csharp_key(state, 0x12); // trigger F5/Run
      return;
    }
    bx2 += rw + 4;

    // Save button
    i32 sw = btn_w("Save");
    if (rx >= bx2 && rx < bx2 + sw) {
      csharp_key(state, 0x13); // trigger Ctrl+S
      return;
    }
    bx2 += sw + 4;

    // Save As button
    i32 saw = btn_w("Save As");
    if (rx >= bx2 && rx < bx2 + saw) {
      csharp_key(state, 0x17); // trigger Ctrl+W
      return;
    }
    bx2 += saw + 4 + 1 + 6; // + separator

    // Solution buttons
    if (s->sln.active) {
      const char *exp_label = s->sln_panel_open ? "Hide Explorer" : "Show Explorer";
      i32 ew2 = btn_w(exp_label);
      if (rx >= bx2 && rx < bx2 + ew2) {
        s->sln_panel_open = !s->sln_panel_open;
        return;
      }
      bx2 += ew2 + 4;

      i32 afw = btn_w("+ File");
      if (rx >= bx2 && rx < bx2 + afw) {
        if (s->sln.file_count < SLN_MAX_FILES) {
          s->addfile_open = true;
          str::cpy(s->addfile_buf, "NewFile.cs");
          s->addfile_cursor = static_cast<i32>(str::len(s->addfile_buf));
        }
        return;
      }
    }
    return;
  }

  // Click in solution explorer panel
  if (s->sln_panel_open && s->sln.active && rx < panel_w) {
    i32 fh = gfx::font_h(), LH = fh + 4;
    i32 hdr_h = fh + 8;
    i32 file_start_y = hdr_h + 4 + LH + 6;

    i32 btn_y = ch - LH * 2 - 8;
    i32 rm_y = btn_y + LH + 2;
    if (ry >= btn_y - 2 && ry < btn_y + LH) {
      if (s->sln.file_count < SLN_MAX_FILES) {
        s->addfile_open = true;
        str::cpy(s->addfile_buf, "NewFile.cs");
        s->addfile_cursor = static_cast<i32>(str::len(s->addfile_buf));
      }
      return;
    }
    if (ry >= rm_y - 2 && ry < rm_y + LH) {
      if (s->sln.file_count > 1) {
        remove_file_from_sln(s, s->sln.active_file);
      }
      return;
    }

    i32 file_idx = (ry - file_start_y) / LH;
    if (file_idx >= 0 && file_idx < s->sln.file_count) {
      switch_file(s, file_idx);
    }
    return;
  }

  // Click in editor area (adjust for panel and toolbar offset)
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 area_h = ch - TOOLBAR_H;
  i32 editor_h = (area_h - STATUS_H - SPLIT_H) * 6 / 10;
  i32 erx = rx - panel_w;
  i32 ery = ry - TOOLBAR_H;
  if (erx >= 0 && ery >= 0 && ery < editor_h) {
    s->cursor = pos_from_xy(s, erx, ery, ch, editor_h);
    if (s->sel_start == s->cursor) s->sel_start = -1;
  }
}

void csharp_mouse_down(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  if (s->saveas_open || s->addfile_open) return;
  i32 panel_w = (s->sln_panel_open && s->sln.active) ? SLN_PANEL_W : 0;
  if (rx < panel_w || ry < TOOLBAR_H) return;
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 area_h = ch - TOOLBAR_H;
  i32 editor_h = (area_h - STATUS_H - SPLIT_H) * 6 / 10;
  i32 erx = rx - panel_w;
  i32 ery = ry - TOOLBAR_H;
  if (ery < editor_h) {
    s->cursor = pos_from_xy(s, erx, ery, ch, editor_h);
    s->sel_start = s->cursor;
  }
}

void csharp_mouse_move(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 ch) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  if (s->saveas_open || s->addfile_open) return;
  i32 panel_w = (s->sln_panel_open && s->sln.active) ? SLN_PANEL_W : 0;
  i32 fh = gfx::font_h(), STATUS_H = fh + 6, SPLIT_H = 4;
  i32 area_h = ch - TOOLBAR_H;
  i32 editor_h = (area_h - STATUS_H - SPLIT_H) * 6 / 10;
  i32 erx = rx - panel_w;
  i32 ery = ry - TOOLBAR_H;
  s->cursor = pos_from_xy(s, erx, ery, ch, editor_h);
}

void csharp_scroll(u8 *state, i32 delta) {
  auto *s = reinterpret_cast<CSharpState *>(state);
  s->scroll_y -= delta * 3;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

void csharp_open_file(u8 *state, const char *path, const char *content) {
  auto *s = reinterpret_cast<CSharpState *>(state);

  // Check if this is a .sln file
  if (ends_with(path, ".sln")) {
    load_solution(s, path, content);
    return;
  }

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
  s->show_template = false;
}

const OgzApp csharp_app = {
    "C# IDE",        // name
    "csharp.ogz",    // id
    860,             // default_w (wider for explorer panel)
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
