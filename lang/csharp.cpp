#include "csharp.h"
#include "graphics.h"
#include "string.h"
#include "syslog.h"
#include "uart.h"

namespace {

// ── Limits ──────────────────────────────────────────────────────────────────
constexpr i32 MAX_TOKENS = 2048;
constexpr i32 MAX_VARS = 64;
constexpr i32 MAX_FUNCS = 16;
constexpr i32 MAX_CALL = 16;
constexpr i32 MAX_SLEN = 96;

// ── Token types ─────────────────────────────────────────────────────────────
enum Tok : u8 {
  T_EOF, T_INT_LIT, T_STR_LIT, T_IDENT,
  // Keywords
  T_USING, T_CLASS, T_STATIC, T_VOID, T_INT, T_STRING, T_BOOL,
  T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN, T_TRUE, T_FALSE, T_NULL, T_NEW,
  T_PUBLIC, T_PRIVATE, T_PROTECTED, T_OVERRIDE, T_VIRTUAL, T_ABSTRACT, T_READONLY, T_CONST, T_NAMESPACE,
  T_VAR, T_CHAR, T_FLOAT, T_DOUBLE, T_OBJECT,
  // Operators
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
  T_ASSIGN, T_EQ, T_NEQ, T_LT, T_GT, T_LTE, T_GTE,
  T_AND, T_OR, T_NOT,
  T_PLUSPLUS, T_MINUSMINUS, T_PLUSEQ, T_MINUSEQ,
  // Punctuation
  T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
  T_LBRACKET, T_RBRACKET,
  T_SEMI, T_COMMA, T_DOT,
};

struct Token {
  u8 type;
  i32 pos; // position in source
  i32 len; // length in source
  i32 ival; // integer value (for T_INT_LIT)
};

// ── Value types ─────────────────────────────────────────────────────────────
enum VType : u8 { V_VOID, V_INT, V_STRING, V_BOOL, V_WIDGET };

struct Value {
  VType type;
  i32 ival;
  char sval[MAX_SLEN];
  bool bval;
};

Value make_void() { Value v; v.type = V_VOID; v.ival = 0; v.sval[0] = '\0'; v.bval = false; return v; }
Value make_int(i32 i) { Value v; v.type = V_INT; v.ival = i; v.sval[0] = '\0'; v.bval = false; return v; }
Value make_bool(bool b) { Value v; v.type = V_BOOL; v.ival = 0; v.sval[0] = '\0'; v.bval = b; return v; }
Value make_str(const char *s) {
  Value v; v.type = V_STRING; v.ival = 0; v.bval = false;
  str::ncpy(v.sval, s, MAX_SLEN - 1);
  return v;
}

// ── Variables ───────────────────────────────────────────────────────────────
struct Var {
  char name[32];
  Value val;
  i32 scope;
};

// ── Functions ───────────────────────────────────────────────────────────────
enum Access : u8 { ACC_PUBLIC = 0, ACC_PRIVATE = 1, ACC_PROTECTED = 2 };

struct Func {
  char name[32];
  i32 tok_start;    // token index of first '{' of body
  i32 param_count;
  char params[6][32];
  VType param_types[6];
  VType ret_type;
  Access access;
  char owner_class[32]; // class this method belongs to
};

// ── Interpreter state (file-static, reset each run) ─────────────────────────
const char *src;
Token tokens[MAX_TOKENS];
i32 tok_count;
i32 tp; // current token pointer

Var vars[MAX_VARS];
i32 var_count;
i32 scope_depth;

Func funcs[MAX_FUNCS];
i32 func_count;

// ── Class name registry (populated by pre-scan) ────────────────────────────
constexpr i32 MAX_CLASSES = 8;
char class_names[MAX_CLASSES][32];
i32 class_count;

char *out;
i32 out_cap;
i32 out_len;

bool had_error;
bool had_return;
Value return_val;

i32 call_depth;

// ── GUI state ───────────────────────────────────────────────────────────────
bool gui_mode = false;
bool close_requested = false;
i32 draw_cx, draw_cy, draw_cw, draw_ch; // current draw context

void gfx_line(i32 x1, i32 y1, i32 x2, i32 y2, u32 color) {
  i32 dx = x2 - x1; if (dx < 0) dx = -dx;
  i32 dy = y2 - y1; if (dy < 0) dy = -dy;
  i32 sx = x1 < x2 ? 1 : -1;
  i32 sy = y1 < y2 ? 1 : -1;
  i32 err = dx - dy;
  while (true) {
    gfx::pixel(x1, y1, color);
    if (x1 == x2 && y1 == y2) break;
    i32 e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x1 += sx; }
    if (e2 < dx) { err += dx; y1 += sy; }
  }
}

// ── Widget system ───────────────────────────────────────────────────────────
constexpr i32 MAX_WIDGETS = 16;
enum WType : u8 { W_NONE, W_LABEL, W_BUTTON, W_TEXTBOX, W_CHECKBOX, W_PANEL };

struct Widget {
  WType wtype;
  i32 x, y, w, h;
  char text[56];
  bool checked;   // CheckBox
  bool focused;   // TextBox
  i32 cursor;     // TextBox cursor
  u32 bg_color;   // Panel background
  u32 fg_color;   // text/foreground color
};

Widget widgets[MAX_WIDGETS];
i32 widget_count;

Value make_widget(i32 handle) {
  Value v; v.type = V_WIDGET; v.ival = handle; v.sval[0] = '\0'; v.bval = false;
  return v;
}

i32 alloc_widget(WType wt) {
  if (widget_count >= MAX_WIDGETS) return -1;
  i32 idx = widget_count++;
  str::memset(&widgets[idx], 0, sizeof(Widget));
  widgets[idx].wtype = wt;
  widgets[idx].fg_color = 0x00202020;
  widgets[idx].bg_color = 0x00FFFFFF;
  return idx;
}

// Widget rendering
constexpr u32 W_COL_BTN = 0x00E0E0E0;
constexpr u32 W_COL_BTN_BORDER = 0x00999999;
constexpr u32 W_COL_BTN_TEXT = 0x00202020;
constexpr u32 W_COL_TB_BG = 0x00FFFFFF;
constexpr u32 W_COL_TB_BORDER = 0x00AAAAAA;
constexpr u32 W_COL_TB_FOCUS = 0x003399FF;
constexpr u32 W_COL_CB_CHECK = 0x003399FF;

void draw_widget(Widget &w) {
  i32 ax = draw_cx + w.x, ay = draw_cy + w.y;
  i32 fh = gfx::font_h(), fw = gfx::font_w();

  switch (w.wtype) {
  case W_LABEL:
    gfx::draw_text_nobg(ax, ay, w.text, w.fg_color);
    break;

  case W_BUTTON: {
    gfx::fill_rect(ax, ay, w.w, w.h, W_COL_BTN);
    gfx::rect(ax, ay, w.w, w.h, W_COL_BTN_BORDER);
    // Highlight top-left edges
    gfx::hline(ax + 1, ay + 1, w.w - 2, 0x00F8F8F8);
    gfx::fill_rect(ax + 1, ay + 1, 1, w.h - 2, 0x00F8F8F8);
    // Center text
    i32 tw = gfx::text_width(w.text);
    i32 tx = ax + (w.w - tw) / 2;
    i32 ty = ay + (w.h - fh) / 2;
    gfx::draw_text(tx, ty, w.text, W_COL_BTN_TEXT, W_COL_BTN);
    break;
  }

  case W_TEXTBOX: {
    u32 border = w.focused ? W_COL_TB_FOCUS : W_COL_TB_BORDER;
    gfx::fill_rect(ax, ay, w.w, w.h, W_COL_TB_BG);
    gfx::rect(ax, ay, w.w, w.h, border);
    if (w.focused) gfx::rect(ax - 1, ay - 1, w.w + 2, w.h + 2, border);
    i32 text_y = ay + (w.h - fh) / 2;
    gfx::draw_text(ax + 4, text_y, w.text, 0x00202020, W_COL_TB_BG);
    // Cursor
    if (w.focused) {
      i32 cx = ax + 4 + w.cursor * fw;
      if (cx < ax + w.w - 2)
        gfx::fill_rect(cx, ay + 3, 1, w.h - 6, 0x00000000);
    }
    break;
  }

  case W_CHECKBOX: {
    i32 box_sz = fh;
    gfx::fill_rect(ax, ay, box_sz, box_sz, 0x00FFFFFF);
    gfx::rect(ax, ay, box_sz, box_sz, 0x00888888);
    if (w.checked) {
      // Draw check mark
      gfx::fill_rect(ax + 3, ay + 3, box_sz - 6, box_sz - 6, W_COL_CB_CHECK);
    }
    gfx::draw_text_nobg(ax + box_sz + 6, ay, w.text, w.fg_color);
    break;
  }

  case W_PANEL:
    gfx::fill_rect(ax, ay, w.w, w.h, w.bg_color);
    if (w.text[0])
      gfx::draw_text_nobg(ax + 4, ay + 4, w.text, w.fg_color);
    break;

  case W_NONE: break;
  }
}

bool widget_hit(Widget &w, i32 mx, i32 my) {
  if (w.wtype == W_LABEL) {
    i32 tw = gfx::text_width(w.text);
    return mx >= w.x && mx < w.x + tw && my >= w.y && my < w.y + gfx::font_h();
  }
  if (w.wtype == W_CHECKBOX) {
    i32 box_sz = gfx::font_h();
    i32 total_w = box_sz + 6 + gfx::text_width(w.text);
    return mx >= w.x && mx < w.x + total_w && my >= w.y && my < w.y + box_sz;
  }
  return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h;
}

void textbox_key(Widget &w, i32 key) {
  if (key == 0x7F || key == 0x08 || key == 8) {
    // Backspace
    if (w.cursor > 0) {
      i32 len = static_cast<i32>(str::len(w.text));
      for (i32 i = w.cursor - 1; i < len - 1; i++) w.text[i] = w.text[i + 1];
      w.text[len - 1] = '\0';
      w.cursor--;
    }
  } else if (key >= 32 && key <= 126) {
    i32 len = static_cast<i32>(str::len(w.text));
    if (len < 54) {
      for (i32 i = len; i > w.cursor; i--) w.text[i] = w.text[i - 1];
      w.text[w.cursor] = static_cast<char>(key);
      w.cursor++;
      w.text[len + 1] = '\0';
    }
  }
}

void textbox_click(Widget &w, i32 mx) {
  i32 fw = gfx::font_w();
  i32 col = (mx - w.x - 4) / fw;
  if (col < 0) col = 0;
  i32 len = static_cast<i32>(str::len(w.text));
  if (col > len) col = len;
  w.cursor = col;
  // Unfocus all other textboxes, focus this one
  for (i32 i = 0; i < widget_count; i++)
    widgets[i].focused = false;
  w.focused = true;
}

// Check if a name is a widget type
bool is_widget_type(const char *name) {
  return str::cmp(name, "Button") == 0 || str::cmp(name, "Label") == 0 ||
         str::cmp(name, "TextBox") == 0 || str::cmp(name, "CheckBox") == 0 ||
         str::cmp(name, "Panel") == 0;
}

// Check if a name is a declared class in the current source
bool is_declared_class(const char *name) {
  for (i32 i = 0; i < class_count; i++)
    if (str::cmp(class_names[i], name) == 0) return true;
  return false;
}

// Check if identifier is a valid type (widget, declared class, or known API class)
bool is_valid_type_ident(const char *name) {
  return is_widget_type(name) || is_declared_class(name) ||
         str::cmp(name, "Console") == 0 || str::cmp(name, "Gfx") == 0 ||
         str::cmp(name, "App") == 0;
}

// Pre-scan tokens to find all class declarations
void prescan_classes() {
  class_count = 0;
  for (i32 i = 0; i < tok_count - 1 && class_count < MAX_CLASSES; i++) {
    if (tokens[i].type == T_CLASS && tokens[i + 1].type == T_IDENT) {
      Token &t = tokens[i + 1];
      i32 len = t.len;
      if (len > 31) len = 31;
      for (i32 j = 0; j < len; j++) class_names[class_count][j] = src[t.pos + j];
      class_names[class_count][len] = '\0';
      class_count++;
    }
  }
}

WType widget_type_from_name(const char *name) {
  if (str::cmp(name, "Label") == 0) return W_LABEL;
  if (str::cmp(name, "Button") == 0) return W_BUTTON;
  if (str::cmp(name, "TextBox") == 0) return W_TEXTBOX;
  if (str::cmp(name, "CheckBox") == 0) return W_CHECKBOX;
  if (str::cmp(name, "Panel") == 0) return W_PANEL;
  return W_NONE;
}

// ── Output helpers ──────────────────────────────────────────────────────────
void emit(const char *s) {
  if (gui_mode) uart::puts(s); // echo to serial console in GUI mode
  while (*s && out_len < out_cap - 1)
    out[out_len++] = *s++;
  out[out_len] = '\0';
}

void emit_int(i32 v) {
  char buf[16];
  if (v == 0) { emit("0"); return; }
  bool neg = v < 0;
  if (neg) v = -v;
  i32 i = 0;
  while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
  if (neg) buf[i++] = '-';
  char rev[16];
  for (i32 j = 0; j < i; j++) rev[j] = buf[i - 1 - j];
  rev[i] = '\0';
  emit(rev);
}

void error(const char *msg) {
  if (had_error) return;
  had_error = true;
  emit("Error: ");
  emit(msg);
  emit("\n");
}

// ── Tokenizer ───────────────────────────────────────────────────────────────
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

// Compare source substring to a keyword
bool src_eq(i32 pos, i32 len, const char *kw) {
  for (i32 i = 0; i < len; i++) {
    if (kw[i] == '\0' || src[pos + i] != kw[i]) return false;
  }
  return kw[len] == '\0';
}

void add_tok(u8 type, i32 pos, i32 len, i32 ival = 0) {
  if (tok_count >= MAX_TOKENS) return;
  tokens[tok_count++] = {type, pos, len, ival};
}

bool tokenize() {
  tok_count = 0;
  i32 i = 0;
  i32 slen = static_cast<i32>(str::len(src));

  while (i < slen) {
    char c = src[i];

    // Skip whitespace
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }

    // Skip // comment
    if (c == '/' && i + 1 < slen && src[i + 1] == '/') {
      while (i < slen && src[i] != '\n') i++;
      continue;
    }

    // Skip /* */ comment
    if (c == '/' && i + 1 < slen && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < slen && !(src[i] == '*' && src[i + 1] == '/')) i++;
      if (i + 1 < slen) i += 2;
      continue;
    }

    // String literal
    if (c == '"') {
      i32 start = i + 1;
      i++;
      while (i < slen && src[i] != '"') i++;
      add_tok(T_STR_LIT, start, i - start);
      if (i < slen) i++; // skip closing "
      continue;
    }

    // Integer literal (decimal or 0x hex)
    if (is_digit(c)) {
      i32 start = i;
      i32 val = 0;
      if (c == '0' && i + 1 < slen && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
        // Hex literal: 0xFF00FF
        i += 2;
        while (i < slen) {
          char h = src[i];
          if (h >= '0' && h <= '9') { val = val * 16 + (h - '0'); }
          else if (h >= 'a' && h <= 'f') { val = val * 16 + (h - 'a' + 10); }
          else if (h >= 'A' && h <= 'F') { val = val * 16 + (h - 'A' + 10); }
          else break;
          i++;
        }
      } else {
        while (i < slen && is_digit(src[i])) {
          val = val * 10 + (src[i] - '0');
          i++;
        }
      }
      add_tok(T_INT_LIT, start, i - start, val);
      continue;
    }

    // Identifier or keyword
    if (is_alpha(c)) {
      i32 start = i;
      while (i < slen && is_alnum(src[i])) i++;
      i32 len = i - start;

      // Check keywords
      u8 type = T_IDENT;
      if (src_eq(start, len, "using")) type = T_USING;
      else if (src_eq(start, len, "class")) type = T_CLASS;
      else if (src_eq(start, len, "static")) type = T_STATIC;
      else if (src_eq(start, len, "void")) type = T_VOID;
      else if (src_eq(start, len, "int")) type = T_INT;
      else if (src_eq(start, len, "string")) type = T_STRING;
      else if (src_eq(start, len, "bool")) type = T_BOOL;
      else if (src_eq(start, len, "if")) type = T_IF;
      else if (src_eq(start, len, "else")) type = T_ELSE;
      else if (src_eq(start, len, "while")) type = T_WHILE;
      else if (src_eq(start, len, "for")) type = T_FOR;
      else if (src_eq(start, len, "return")) type = T_RETURN;
      else if (src_eq(start, len, "true")) type = T_TRUE;
      else if (src_eq(start, len, "false")) type = T_FALSE;
      else if (src_eq(start, len, "null")) type = T_NULL;
      else if (src_eq(start, len, "new")) type = T_NEW;
      else if (src_eq(start, len, "public")) type = T_PUBLIC;
      else if (src_eq(start, len, "private")) type = T_PRIVATE;
      else if (src_eq(start, len, "protected")) type = T_PROTECTED;
      else if (src_eq(start, len, "override")) type = T_OVERRIDE;
      else if (src_eq(start, len, "virtual")) type = T_VIRTUAL;
      else if (src_eq(start, len, "abstract")) type = T_ABSTRACT;
      else if (src_eq(start, len, "readonly")) type = T_READONLY;
      else if (src_eq(start, len, "const")) type = T_CONST;
      else if (src_eq(start, len, "namespace")) type = T_NAMESPACE;
      else if (src_eq(start, len, "var")) type = T_VAR;
      else if (src_eq(start, len, "char")) type = T_CHAR;
      else if (src_eq(start, len, "float")) type = T_FLOAT;
      else if (src_eq(start, len, "double")) type = T_DOUBLE;
      else if (src_eq(start, len, "object")) type = T_OBJECT;

      add_tok(type, start, len);
      continue;
    }

    // Two-char operators
    if (i + 1 < slen) {
      char c2 = src[i + 1];
      if (c == '=' && c2 == '=') { add_tok(T_EQ, i, 2); i += 2; continue; }
      if (c == '!' && c2 == '=') { add_tok(T_NEQ, i, 2); i += 2; continue; }
      if (c == '<' && c2 == '=') { add_tok(T_LTE, i, 2); i += 2; continue; }
      if (c == '>' && c2 == '=') { add_tok(T_GTE, i, 2); i += 2; continue; }
      if (c == '&' && c2 == '&') { add_tok(T_AND, i, 2); i += 2; continue; }
      if (c == '|' && c2 == '|') { add_tok(T_OR, i, 2); i += 2; continue; }
      if (c == '+' && c2 == '+') { add_tok(T_PLUSPLUS, i, 2); i += 2; continue; }
      if (c == '-' && c2 == '-') { add_tok(T_MINUSMINUS, i, 2); i += 2; continue; }
      if (c == '+' && c2 == '=') { add_tok(T_PLUSEQ, i, 2); i += 2; continue; }
      if (c == '-' && c2 == '=') { add_tok(T_MINUSEQ, i, 2); i += 2; continue; }
    }

    // Single-char tokens
    switch (c) {
    case '+': add_tok(T_PLUS, i, 1); break;
    case '-': add_tok(T_MINUS, i, 1); break;
    case '*': add_tok(T_STAR, i, 1); break;
    case '/': add_tok(T_SLASH, i, 1); break;
    case '%': add_tok(T_PERCENT, i, 1); break;
    case '=': add_tok(T_ASSIGN, i, 1); break;
    case '<': add_tok(T_LT, i, 1); break;
    case '>': add_tok(T_GT, i, 1); break;
    case '!': add_tok(T_NOT, i, 1); break;
    case '(': add_tok(T_LPAREN, i, 1); break;
    case ')': add_tok(T_RPAREN, i, 1); break;
    case '{': add_tok(T_LBRACE, i, 1); break;
    case '}': add_tok(T_RBRACE, i, 1); break;
    case '[': add_tok(T_LBRACKET, i, 1); break;
    case ']': add_tok(T_RBRACKET, i, 1); break;
    case ';': add_tok(T_SEMI, i, 1); break;
    case ',': add_tok(T_COMMA, i, 1); break;
    case '.': add_tok(T_DOT, i, 1); break;
    default:
      error("unexpected character");
      return false;
    }
    i++;
  }

  add_tok(T_EOF, slen, 0);
  return true;
}

// ── Token helpers ───────────────────────────────────────────────────────────
Token &cur() { return tokens[tp < tok_count ? tp : tok_count - 1]; }
Token &peek(i32 ahead = 0) {
  i32 idx = tp + ahead;
  return tokens[idx < tok_count ? idx : tok_count - 1];
}
bool at(u8 type) { return cur().type == type; }
bool match(u8 type) { if (at(type)) { tp++; return true; } return false; }
void expect(u8 type) {
  if (!match(type)) {
    char ebuf[80];
    str::cpy(ebuf, "expected tok ");
    char tmp[4]; tmp[0] = '0' + (type / 10); tmp[1] = '0' + (type % 10); tmp[2] = '\0';
    str::cat(ebuf, tmp);
    str::cat(ebuf, " got ");
    u8 gt = cur().type;
    tmp[0] = '0' + (gt / 10); tmp[1] = '0' + (gt % 10); tmp[2] = '\0';
    str::cat(ebuf, tmp);
    str::cat(ebuf, " at ");
    i32 pos = cur().pos;
    char pb[8]; pb[0] = '\0';
    i32 pi = 0; char pt[8];
    if (pos == 0) { pb[0] = '0'; pb[1] = '\0'; }
    else { while (pos > 0) { pt[pi++] = '0' + (pos % 10); pos /= 10; }
           i32 pj = 0; while (pi > 0) pb[pj++] = pt[--pi]; pb[pj] = '\0'; }
    str::cat(ebuf, pb);
    error(ebuf);
  }
}

// Get identifier text from a token
void tok_text(Token &t, char *buf, i32 buf_size) {
  i32 len = t.len;
  if (len >= buf_size) len = buf_size - 1;
  for (i32 i = 0; i < len; i++) buf[i] = src[t.pos + i];
  buf[len] = '\0';
}

// ── Variable management ─────────────────────────────────────────────────────
Var *find_var(const char *name) {
  // Search backwards (inner scope first)
  for (i32 i = var_count - 1; i >= 0; i--) {
    if (str::cmp(vars[i].name, name) == 0)
      return &vars[i];
  }
  return nullptr;
}

Var *add_var(const char *name, Value val) {
  if (var_count >= MAX_VARS) { error("too many variables"); return nullptr; }
  Var &v = vars[var_count++];
  str::ncpy(v.name, name, 31);
  v.val = val;
  v.scope = scope_depth;
  return &v;
}

void pop_scope() {
  while (var_count > 0 && vars[var_count - 1].scope >= scope_depth)
    var_count--;
  scope_depth--;
}

// ── Function management ─────────────────────────────────────────────────────
Func *find_func(const char *name) {
  for (i32 i = 0; i < func_count; i++) {
    if (str::cmp(funcs[i].name, name) == 0)
      return &funcs[i];
  }
  return nullptr;
}

VType parse_type_kw() {
  if (match(T_VOID)) return V_VOID;
  if (match(T_INT) || match(T_CHAR) || match(T_FLOAT) || match(T_DOUBLE)) return V_INT;
  if (match(T_STRING)) return V_STRING;
  if (match(T_BOOL)) return V_BOOL;
  if (match(T_VAR) || match(T_OBJECT)) return V_INT; // var/object → infer from assignment
  return V_VOID;
}

// Check if token is a type keyword
bool is_type_keyword(u8 t) {
  return t == T_VOID || t == T_INT || t == T_STRING || t == T_BOOL ||
         t == T_VAR || t == T_CHAR || t == T_FLOAT || t == T_DOUBLE || t == T_OBJECT;
}

// ── Forward declarations ────────────────────────────────────────────────────
Value parse_expr();
void exec_block();
void exec_stmt();

// ── Expression parser (recursive descent, proper precedence) ────────────────
Value parse_primary() {
  if (had_error) return make_void();

  if (at(T_INT_LIT)) {
    Value v = make_int(cur().ival);
    tp++;
    return v;
  }

  if (at(T_STR_LIT)) {
    char buf[MAX_SLEN];
    tok_text(cur(), buf, MAX_SLEN);
    tp++;
    return make_str(buf);
  }

  if (match(T_TRUE)) return make_bool(true);
  if (match(T_FALSE)) return make_bool(false);

  if (match(T_LPAREN)) {
    Value v = parse_expr();
    expect(T_RPAREN);
    return v;
  }

  if (match(T_MINUS)) {
    Value v = parse_primary();
    return make_int(-v.ival);
  }

  if (match(T_NOT)) {
    Value v = parse_primary();
    if (v.type == V_BOOL) return make_bool(!v.bval);
    return make_bool(v.ival == 0);
  }

  // new WidgetType(args...)
  if (match(T_NEW)) {
    char tname[32];
    tok_text(cur(), tname, 32);
    expect(T_IDENT);
    expect(T_LPAREN);

    WType wt = widget_type_from_name(tname);
    if (wt == W_NONE) {
      // Not a widget — check if it's a declared class (user-defined type)
      if (is_declared_class(tname)) {
        // Skip constructor arguments and return a placeholder value
        i32 depth = 1;
        while (depth > 0 && !at(T_EOF)) {
          if (match(T_LPAREN)) depth++;
          else if (at(T_RPAREN)) { depth--; if (depth > 0) tp++; }
          else tp++;
        }
        expect(T_RPAREN);
        return make_int(1); // placeholder object reference
      }
      error("unknown type for new");
      expect(T_RPAREN);
      return make_void();
    }

    i32 idx = alloc_widget(wt);
    if (idx < 0) { error("too many widgets"); expect(T_RPAREN); return make_void(); }
    Widget &w = widgets[idx];

    if (wt == W_LABEL) {
      // new Label(x, y, "text")
      w.x = parse_expr().ival; expect(T_COMMA);
      w.y = parse_expr().ival; expect(T_COMMA);
      Value t = parse_expr();
      str::ncpy(w.text, t.sval, 55);
    } else if (wt == W_BUTTON) {
      // new Button(x, y, w, h, "text")
      w.x = parse_expr().ival; expect(T_COMMA);
      w.y = parse_expr().ival; expect(T_COMMA);
      w.w = parse_expr().ival; expect(T_COMMA);
      w.h = parse_expr().ival; expect(T_COMMA);
      Value t = parse_expr();
      str::ncpy(w.text, t.sval, 55);
    } else if (wt == W_TEXTBOX) {
      // new TextBox(x, y, w, h)
      w.x = parse_expr().ival; expect(T_COMMA);
      w.y = parse_expr().ival; expect(T_COMMA);
      w.w = parse_expr().ival; expect(T_COMMA);
      w.h = parse_expr().ival;
    } else if (wt == W_CHECKBOX) {
      // new CheckBox(x, y, "text")
      w.x = parse_expr().ival; expect(T_COMMA);
      w.y = parse_expr().ival; expect(T_COMMA);
      Value t = parse_expr();
      str::ncpy(w.text, t.sval, 55);
      w.h = gfx::font_h();
    } else if (wt == W_PANEL) {
      // new Panel(x, y, w, h, color)
      w.x = parse_expr().ival; expect(T_COMMA);
      w.y = parse_expr().ival; expect(T_COMMA);
      w.w = parse_expr().ival; expect(T_COMMA);
      w.h = parse_expr().ival; expect(T_COMMA);
      w.bg_color = static_cast<u32>(parse_expr().ival);
    }

    expect(T_RPAREN);
    return make_widget(idx);
  }

  // int.Parse(string) → convert string to int
  if (at(T_INT) && tp + 1 < tok_count && tokens[tp + 1].type == T_DOT) {
    tp++; // skip 'int'
    tp++; // skip '.'
    char method[32]; tok_text(cur(), method, 32); tp++;
    if (str::cmp(method, "Parse") == 0) {
      expect(T_LPAREN);
      Value arg = parse_expr();
      expect(T_RPAREN);
      if (arg.type != V_STRING) { error("int.Parse expects string"); return make_int(0); }
      // Parse string to integer
      const char *s = arg.sval;
      i32 result = 0, sign = 1, i = 0;
      if (s[0] == '-') { sign = -1; i = 1; }
      while (s[i] >= '0' && s[i] <= '9') { result = result * 10 + (s[i] - '0'); i++; }
      return make_int(result * sign);
    }
    error("unknown int method"); return make_int(0);
  }

  if (at(T_IDENT)) {
    char name[32];
    tok_text(cur(), name, 32);
    tp++;

    // Console.WriteLine / Console.Write
    if (str::cmp(name, "Console") == 0 && match(T_DOT)) {
      char method[32];
      tok_text(cur(), method, 32);
      tp++;
      expect(T_LPAREN);

      if (str::cmp(method, "WriteLine") == 0) {
        if (!at(T_RPAREN)) {
          Value arg = parse_expr();
          if (arg.type == V_INT) emit_int(arg.ival);
          else if (arg.type == V_STRING) emit(arg.sval);
          else if (arg.type == V_BOOL) emit(arg.bval ? "True" : "False");
        }
        emit("\n");
      } else if (str::cmp(method, "Write") == 0) {
        if (!at(T_RPAREN)) {
          Value arg = parse_expr();
          if (arg.type == V_INT) emit_int(arg.ival);
          else if (arg.type == V_STRING) emit(arg.sval);
          else if (arg.type == V_BOOL) emit(arg.bval ? "True" : "False");
        }
      }
      expect(T_RPAREN);
      return make_void();
    }

    // Gfx.* drawing API
    if (str::cmp(name, "Gfx") == 0 && match(T_DOT)) {
      char method[32];
      tok_text(cur(), method, 32);
      tp++;
      expect(T_LPAREN);

      if (str::cmp(method, "Clear") == 0) {
        Value c = parse_expr();
        if (gui_mode)
          gfx::fill_rect(draw_cx, draw_cy, draw_cw, draw_ch, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "FillRect") == 0) {
        Value x = parse_expr(); expect(T_COMMA);
        Value y = parse_expr(); expect(T_COMMA);
        Value w = parse_expr(); expect(T_COMMA);
        Value h = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx::fill_rect(draw_cx + x.ival, draw_cy + y.ival, w.ival, h.ival, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "Rect") == 0) {
        Value x = parse_expr(); expect(T_COMMA);
        Value y = parse_expr(); expect(T_COMMA);
        Value w = parse_expr(); expect(T_COMMA);
        Value h = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx::rect(draw_cx + x.ival, draw_cy + y.ival, w.ival, h.ival, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "DrawText") == 0) {
        Value x = parse_expr(); expect(T_COMMA);
        Value y = parse_expr(); expect(T_COMMA);
        Value t = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx::draw_text_nobg(draw_cx + x.ival, draw_cy + y.ival, t.sval, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "Pixel") == 0) {
        Value x = parse_expr(); expect(T_COMMA);
        Value y = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx::pixel(draw_cx + x.ival, draw_cy + y.ival, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "Line") == 0) {
        Value x1 = parse_expr(); expect(T_COMMA);
        Value y1 = parse_expr(); expect(T_COMMA);
        Value x2 = parse_expr(); expect(T_COMMA);
        Value y2 = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx_line(draw_cx + x1.ival, draw_cy + y1.ival,
                   draw_cx + x2.ival, draw_cy + y2.ival, static_cast<u32>(c.ival));
      } else if (str::cmp(method, "HLine") == 0) {
        Value x = parse_expr(); expect(T_COMMA);
        Value y = parse_expr(); expect(T_COMMA);
        Value w = parse_expr(); expect(T_COMMA);
        Value c = parse_expr();
        if (gui_mode)
          gfx::hline(draw_cx + x.ival, draw_cy + y.ival, w.ival, static_cast<u32>(c.ival));
      } else {
        error("unknown Gfx method");
      }
      expect(T_RPAREN);
      return make_void();
    }

    // App.* window API
    if (str::cmp(name, "App") == 0 && match(T_DOT)) {
      char method[32];
      tok_text(cur(), method, 32);
      tp++;

      if (str::cmp(method, "Close") == 0) {
        expect(T_LPAREN); expect(T_RPAREN);
        close_requested = true;
        return make_void();
      } else if (str::cmp(method, "Width") == 0) {
        expect(T_LPAREN); expect(T_RPAREN);
        return make_int(draw_cw);
      } else if (str::cmp(method, "Height") == 0) {
        expect(T_LPAREN); expect(T_RPAREN);
        return make_int(draw_ch);
      } else {
        error("unknown App method");
        return make_void();
      }
    }

    // Widget method call: variable.Method(args)
    if (at(T_DOT)) {
      Var *wv = find_var(name);
      if (wv && wv->val.type == V_WIDGET && wv->val.ival >= 0 &&
          wv->val.ival < widget_count) {
        tp++; // skip dot
        char method[32];
        tok_text(cur(), method, 32);
        tp++; // skip method name
        Widget &w = widgets[wv->val.ival];

        // .Draw()
        if (str::cmp(method, "Draw") == 0) {
          expect(T_LPAREN); expect(T_RPAREN);
          if (gui_mode) draw_widget(w);
          return make_void();
        }
        // .HitTest(x, y)
        if (str::cmp(method, "HitTest") == 0) {
          expect(T_LPAREN);
          i32 mx = parse_expr().ival; expect(T_COMMA);
          i32 my = parse_expr().ival; expect(T_RPAREN);
          return make_bool(widget_hit(w, mx, my));
        }
        // .SetText("text")
        if (str::cmp(method, "SetText") == 0) {
          expect(T_LPAREN);
          Value t = parse_expr(); expect(T_RPAREN);
          str::ncpy(w.text, t.sval, 55);
          return make_void();
        }
        // .GetText()
        if (str::cmp(method, "GetText") == 0) {
          expect(T_LPAREN); expect(T_RPAREN);
          return make_str(w.text);
        }
        // .SetPos(x, y)
        if (str::cmp(method, "SetPos") == 0) {
          expect(T_LPAREN);
          w.x = parse_expr().ival; expect(T_COMMA);
          w.y = parse_expr().ival; expect(T_RPAREN);
          return make_void();
        }
        // .SetSize(w, h)
        if (str::cmp(method, "SetSize") == 0) {
          expect(T_LPAREN);
          w.w = parse_expr().ival; expect(T_COMMA);
          w.h = parse_expr().ival; expect(T_RPAREN);
          return make_void();
        }
        // .SetColor(fg) or .SetColor(fg, bg)
        if (str::cmp(method, "SetColor") == 0) {
          expect(T_LPAREN);
          w.fg_color = static_cast<u32>(parse_expr().ival);
          if (match(T_COMMA))
            w.bg_color = static_cast<u32>(parse_expr().ival);
          expect(T_RPAREN);
          return make_void();
        }
        // TextBox: .Click(x, y)
        if (str::cmp(method, "Click") == 0 && w.wtype == W_TEXTBOX) {
          expect(T_LPAREN);
          i32 mx = parse_expr().ival; expect(T_COMMA);
          parse_expr(); // y (unused for textbox)
          expect(T_RPAREN);
          textbox_click(w, mx);
          return make_void();
        }
        // TextBox: .Key(k)
        if (str::cmp(method, "Key") == 0 && w.wtype == W_TEXTBOX) {
          expect(T_LPAREN);
          i32 k = parse_expr().ival; expect(T_RPAREN);
          textbox_key(w, k);
          return make_void();
        }
        // TextBox: .Focus()
        if (str::cmp(method, "Focus") == 0 && w.wtype == W_TEXTBOX) {
          expect(T_LPAREN); expect(T_RPAREN);
          for (i32 i = 0; i < widget_count; i++) widgets[i].focused = false;
          w.focused = true;
          return make_void();
        }
        // CheckBox: .Toggle()
        if (str::cmp(method, "Toggle") == 0 && w.wtype == W_CHECKBOX) {
          expect(T_LPAREN); expect(T_RPAREN);
          w.checked = !w.checked;
          return make_void();
        }
        // CheckBox: .IsChecked()
        if (str::cmp(method, "IsChecked") == 0 && w.wtype == W_CHECKBOX) {
          expect(T_LPAREN); expect(T_RPAREN);
          return make_bool(w.checked);
        }
        error("unknown widget method");
        return make_void();
      }

      // Non-widget variable with dot: obj.Method() → call Method as a function
      if (wv) {
        tp++; // skip dot
        char method[32];
        tok_text(cur(), method, 32);
        tp++; // skip method name

        // Look up method as a declared function
        Func *fn = find_func(method);
        if (fn && at(T_LPAREN)) {
          // Enforce access modifiers for external calls
          if (fn->access != ACC_PUBLIC) {
            char errbuf[80];
            str::cpy(errbuf, "'");
            str::cat(errbuf, method);
            str::cat(errbuf, "' is ");
            str::cat(errbuf, fn->access == ACC_PRIVATE ? "private" : "protected");
            str::cat(errbuf, " in '");
            str::cat(errbuf, fn->owner_class);
            str::cat(errbuf, "'");
            error(errbuf);
            return make_void();
          }
          expect(T_LPAREN);
          // Parse arguments (pass them as params if the function accepts them)
          Value args[6];
          i32 argc = 0;
          while (!at(T_RPAREN) && !at(T_EOF) && argc < 6) {
            args[argc++] = parse_expr();
            if (!match(T_COMMA)) break;
          }
          expect(T_RPAREN);

          // Validate argument count
          if (argc != fn->param_count) {
            char errbuf[64];
            str::cpy(errbuf, "'");
            str::cat(errbuf, method);
            str::cat(errbuf, "' expects ");
            char tmp[8]; tmp[0] = '0' + static_cast<char>(fn->param_count); tmp[1] = '\0';
            str::cat(errbuf, tmp);
            str::cat(errbuf, " arg(s), got ");
            tmp[0] = '0' + static_cast<char>(argc); tmp[1] = '\0';
            str::cat(errbuf, tmp);
            error(errbuf);
            return make_void();
          }

          // Call the function
          scope_depth++;
          for (i32 i = 0; i < fn->param_count; i++)
            add_var(fn->params[i], args[i]);
          i32 saved_tp = tp;
          tp = fn->tok_start;
          had_return = false;
          call_depth++;
          exec_block();
          call_depth--;
          Value ret = return_val;
          had_return = false;
          return_val = make_void();
          pop_scope();
          tp = saved_tp;
          return ret;
        }
        // Property access — just skip and return void
        if (at(T_LPAREN)) {
          expect(T_LPAREN);
          while (!at(T_RPAREN) && !at(T_EOF)) { parse_expr(); if (!match(T_COMMA)) break; }
          expect(T_RPAREN);
        }
        return make_void();
      }
    }

    // Function call
    if (at(T_LPAREN)) {
      tp++; // skip (
      Func *fn = find_func(name);
      if (!fn) { error("undefined function: "); error(name); return make_void(); }
      if (call_depth >= MAX_CALL) { error("stack overflow"); return make_void(); }

      // Evaluate arguments
      Value args[6];
      i32 argc = 0;
      while (!at(T_RPAREN) && !at(T_EOF) && argc < 6) {
        args[argc++] = parse_expr();
        if (!match(T_COMMA)) break;
      }
      expect(T_RPAREN);

      if (argc != fn->param_count) { error("wrong number of arguments"); return make_void(); }

      // Save state
      i32 saved_tp = tp;
      i32 saved_var_count = var_count;
      i32 saved_scope = scope_depth;
      bool saved_return = had_return;
      Value saved_retval = return_val;

      // Set up parameters as local variables
      scope_depth++;
      call_depth++;
      for (i32 i = 0; i < argc; i++)
        add_var(fn->params[i], args[i]);

      had_return = false;
      return_val = make_void();

      // Execute function body
      tp = fn->tok_start;
      exec_block();

      Value result = return_val;
      call_depth--;

      // Restore state
      var_count = saved_var_count;
      scope_depth = saved_scope;
      tp = saved_tp;
      had_return = saved_return;
      return_val = saved_retval;

      return result;
    }

    // ++ / --
    if (match(T_PLUSPLUS)) {
      Var *v = find_var(name);
      if (v) { v->val.ival++; return make_int(v->val.ival - 1); }
      error("undefined variable");
      return make_void();
    }
    if (match(T_MINUSMINUS)) {
      Var *v = find_var(name);
      if (v) { v->val.ival--; return make_int(v->val.ival + 1); }
      error("undefined variable");
      return make_void();
    }

    // Variable lookup
    Var *v = find_var(name);
    if (v) return v->val;

    error("undefined variable: ");
    emit(name);
    emit("\n");
    return make_void();
  }

  error("unexpected expression");
  return make_void();
}

Value parse_mul() {
  Value left = parse_primary();
  while (!had_error) {
    if (match(T_STAR)) { left = make_int(left.ival * parse_primary().ival); }
    else if (match(T_SLASH)) {
      Value r = parse_primary();
      if (r.ival == 0) { error("division by zero"); return make_int(0); }
      left = make_int(left.ival / r.ival);
    }
    else if (match(T_PERCENT)) {
      Value r = parse_primary();
      if (r.ival == 0) { error("modulo by zero"); return make_int(0); }
      left = make_int(left.ival % r.ival);
    }
    else break;
  }
  return left;
}

Value parse_add() {
  Value left = parse_mul();
  while (!had_error) {
    if (match(T_PLUS)) {
      Value right = parse_mul();
      if (left.type == V_STRING || right.type == V_STRING) {
        // String concatenation
        char buf[MAX_SLEN];
        buf[0] = '\0';
        if (left.type == V_STRING) str::cat(buf, left.sval);
        // Build concatenated string
        buf[0] = '\0';
        if (left.type == V_STRING) str::ncpy(buf, left.sval, MAX_SLEN - 1);
        else if (left.type == V_INT) {
          // int to string
          i32 v = left.ival;
          if (v == 0) { str::cpy(buf, "0"); }
          else {
            bool neg = v < 0; if (neg) v = -v;
            char tb[16]; i32 ti = 0;
            while (v > 0) { tb[ti++] = '0' + (v % 10); v /= 10; }
            i32 bi = 0;
            if (neg) buf[bi++] = '-';
            while (ti > 0) buf[bi++] = tb[--ti];
            buf[bi] = '\0';
          }
        }
        else if (left.type == V_BOOL) str::cpy(buf, left.bval ? "True" : "False");

        usize cur_len = str::len(buf);
        if (right.type == V_STRING) {
          str::ncpy(buf + cur_len, right.sval, MAX_SLEN - 1 - static_cast<i32>(cur_len));
        } else if (right.type == V_INT) {
          char tmp[16]; tmp[0] = '\0';
          i32 v = right.ival;
          if (v == 0) { str::cpy(tmp, "0"); }
          else {
            bool neg = v < 0; if (neg) v = -v;
            char tb[16]; i32 ti = 0;
            while (v > 0) { tb[ti++] = '0' + (v % 10); v /= 10; }
            i32 bi = 0;
            if (neg) tmp[bi++] = '-';
            while (ti > 0) tmp[bi++] = tb[--ti];
            tmp[bi] = '\0';
          }
          str::ncpy(buf + cur_len, tmp, MAX_SLEN - 1 - static_cast<i32>(cur_len));
        } else if (right.type == V_BOOL) {
          str::ncpy(buf + cur_len, right.bval ? "True" : "False",
                    MAX_SLEN - 1 - static_cast<i32>(cur_len));
        }
        left = make_str(buf);
      } else {
        left = make_int(left.ival + right.ival);
      }
    }
    else if (match(T_MINUS)) { left = make_int(left.ival - parse_mul().ival); }
    else break;
  }
  return left;
}

Value parse_cmp() {
  Value left = parse_add();
  while (!had_error) {
    if (match(T_LT)) { left = make_bool(left.ival < parse_add().ival); }
    else if (match(T_GT)) { left = make_bool(left.ival > parse_add().ival); }
    else if (match(T_LTE)) { left = make_bool(left.ival <= parse_add().ival); }
    else if (match(T_GTE)) { left = make_bool(left.ival >= parse_add().ival); }
    else break;
  }
  return left;
}

Value parse_eq() {
  Value left = parse_cmp();
  while (!had_error) {
    if (match(T_EQ)) {
      Value right = parse_cmp();
      if (left.type == V_STRING && right.type == V_STRING)
        left = make_bool(str::cmp(left.sval, right.sval) == 0);
      else if (left.type == V_BOOL && right.type == V_BOOL)
        left = make_bool(left.bval == right.bval);
      else
        left = make_bool(left.ival == right.ival);
    }
    else if (match(T_NEQ)) {
      Value right = parse_cmp();
      if (left.type == V_STRING && right.type == V_STRING)
        left = make_bool(str::cmp(left.sval, right.sval) != 0);
      else if (left.type == V_BOOL && right.type == V_BOOL)
        left = make_bool(left.bval != right.bval);
      else
        left = make_bool(left.ival != right.ival);
    }
    else break;
  }
  return left;
}

Value parse_and() {
  Value left = parse_eq();
  while (!had_error && match(T_AND)) {
    Value right = parse_eq();
    bool lb = left.type == V_BOOL ? left.bval : (left.ival != 0);
    bool rb = right.type == V_BOOL ? right.bval : (right.ival != 0);
    left = make_bool(lb && rb);
  }
  return left;
}

Value parse_expr() {
  Value left = parse_and();
  while (!had_error && match(T_OR)) {
    Value right = parse_and();
    bool lb = left.type == V_BOOL ? left.bval : (left.ival != 0);
    bool rb = right.type == V_BOOL ? right.bval : (right.ival != 0);
    left = make_bool(lb || rb);
  }
  return left;
}

// ── Statement execution ─────────────────────────────────────────────────────
void skip_block() {
  expect(T_LBRACE);
  i32 depth = 1;
  while (depth > 0 && !at(T_EOF)) {
    if (match(T_LBRACE)) depth++;
    else if (match(T_RBRACE)) depth--;
    else tp++;
  }
}

void exec_block() {
  if (had_error) return;
  expect(T_LBRACE);
  scope_depth++;
  while (!at(T_RBRACE) && !at(T_EOF) && !had_error && !had_return) {
    exec_stmt();
  }
  expect(T_RBRACE);
  pop_scope();
}

void exec_stmt() {
  if (had_error || had_return) return;

  // Variable declaration: int x = expr; / string s = expr; / bool b = expr; / var x = expr;
  if (at(T_INT) || at(T_STRING) || at(T_BOOL) || at(T_VAR) ||
      at(T_CHAR) || at(T_FLOAT) || at(T_DOUBLE) || at(T_OBJECT)) {
    VType vt = parse_type_kw();
    char name[32];
    tok_text(cur(), name, 32);
    expect(T_IDENT);

    Value init;
    if (vt == V_INT) init = make_int(0);
    else if (vt == V_STRING) init = make_str("");
    else init = make_bool(false);

    if (match(T_ASSIGN)) {
      init = parse_expr();
    }
    add_var(name, init);
    expect(T_SEMI);
    return;
  }

  // Widget/custom type declaration: Button btn; / Button btn = new Button(...);
  if (at(T_IDENT) && peek(1).type == T_IDENT &&
      (peek(2).type == T_SEMI || peek(2).type == T_ASSIGN)) {
    char tname[32];
    tok_text(cur(), tname, 32);
    if (is_widget_type(tname)) {
      tp++; // skip type name
      char vname[32];
      tok_text(cur(), vname, 32);
      expect(T_IDENT);
      Value init = make_widget(-1); // uninitialized widget
      if (match(T_ASSIGN)) {
        init = parse_expr();
      }
      add_var(vname, init);
      expect(T_SEMI);
      return;
    }
    if (!is_valid_type_ident(tname)) {
      char errbuf[64];
      str::cpy(errbuf, "unknown type '");
      str::cat(errbuf, tname);
      str::cat(errbuf, "'");
      error(errbuf);
      return;
    }
  }

  // if / else if / else statement
  if (match(T_IF)) {
    expect(T_LPAREN);
    Value cond = parse_expr();
    expect(T_RPAREN);
    bool truthy = cond.type == V_BOOL ? cond.bval : (cond.ival != 0);

    if (truthy) {
      if (at(T_LBRACE)) exec_block(); else exec_stmt();
      if (match(T_ELSE)) {
        if (at(T_LBRACE)) skip_block();
        else if (at(T_IF)) { /* else if: skip the whole chain */ i32 d = 0; while (!at(T_EOF)) { if (at(T_LBRACE)) { d++; tp++; } else if (at(T_RBRACE)) { d--; tp++; if (d <= 0 && !match(T_ELSE)) break; } else tp++; } }
        else { /* single-stmt else, skip it */ while (!at(T_SEMI) && !at(T_EOF)) tp++; match(T_SEMI); }
      }
    } else {
      if (at(T_LBRACE)) skip_block(); else { while (!at(T_SEMI) && !at(T_EOF)) tp++; match(T_SEMI); }
      if (match(T_ELSE)) {
        if (at(T_LBRACE)) exec_block();
        else if (at(T_IF)) exec_stmt(); // else if → recurse
        else exec_stmt(); // single-stmt else
      }
    }
    return;
  }

  // while statement
  if (match(T_WHILE)) {
    i32 loop_start = tp;
    i32 iterations = 0;
    while (!had_error && !had_return && iterations < 100000) {
      tp = loop_start;
      expect(T_LPAREN);
      Value cond = parse_expr();
      expect(T_RPAREN);
      bool truthy = cond.type == V_BOOL ? cond.bval : (cond.ival != 0);
      if (!truthy) { skip_block(); break; }
      exec_block();
      iterations++;
    }
    if (iterations >= 100000) error("infinite loop detected");
    return;
  }

  // for statement
  if (match(T_FOR)) {
    expect(T_LPAREN);
    scope_depth++;

    // Init
    if (is_type_keyword(cur().type) && cur().type != T_VOID) {
      VType vt = parse_type_kw();
      char name[32];
      tok_text(cur(), name, 32);
      expect(T_IDENT);
      Value init;
      if (vt == V_INT) init = make_int(0);
      else if (vt == V_STRING) init = make_str("");
      else init = make_bool(false);
      if (match(T_ASSIGN)) init = parse_expr();
      add_var(name, init);
      expect(T_SEMI);
    } else if (!at(T_SEMI)) {
      // Assignment init
      char name[32];
      tok_text(cur(), name, 32);
      expect(T_IDENT);
      expect(T_ASSIGN);
      Value val = parse_expr();
      Var *v = find_var(name);
      if (v) v->val = val;
      expect(T_SEMI);
    } else {
      expect(T_SEMI);
    }

    i32 cond_start = tp;
    i32 iterations = 0;

    while (!had_error && !had_return && iterations < 100000) {
      // Condition
      tp = cond_start;
      Value cond = parse_expr();
      expect(T_SEMI);
      i32 incr_start = tp;

      bool truthy = cond.type == V_BOOL ? cond.bval : (cond.ival != 0);

      // Skip increment to find body
      // We need to skip the increment expression to get to the block
      // Save position, skip increment
      while (!at(T_RPAREN) && !at(T_EOF)) tp++;
      expect(T_RPAREN);

      if (!truthy) { skip_block(); break; }

      // Execute body
      exec_block();

      // Execute increment
      tp = incr_start;
      // Parse increment: could be i++, i--, i = expr, i += expr, i -= expr
      if (at(T_IDENT)) {
        char iname[32];
        tok_text(cur(), iname, 32);
        tp++;
        Var *iv = find_var(iname);
        if (iv) {
          if (match(T_PLUSPLUS)) { iv->val.ival++; }
          else if (match(T_MINUSMINUS)) { iv->val.ival--; }
          else if (match(T_ASSIGN)) { iv->val = parse_expr(); }
          else if (match(T_PLUSEQ)) { iv->val.ival += parse_expr().ival; }
          else if (match(T_MINUSEQ)) { iv->val.ival -= parse_expr().ival; }
        }
      }

      iterations++;
    }
    if (iterations >= 100000) error("infinite loop detected");
    pop_scope();
    return;
  }

  // return statement
  if (match(T_RETURN)) {
    if (!at(T_SEMI)) {
      return_val = parse_expr();
    }
    expect(T_SEMI);
    had_return = true;
    return;
  }

  // Expression statement (assignment, function call, etc.)
  if (at(T_IDENT)) {
    char name[32];
    tok_text(cur(), name, 32);

    // Check for assignment: ident = expr;
    if (peek(1).type == T_ASSIGN) {
      tp++; // skip ident
      tp++; // skip =
      Value val = parse_expr();
      Var *v = find_var(name);
      if (v) v->val = val;
      else error("undefined variable");
      expect(T_SEMI);
      return;
    }

    // i++; i--;
    if (peek(1).type == T_PLUSPLUS) {
      tp += 2;
      Var *v = find_var(name);
      if (v) v->val.ival++;
      expect(T_SEMI);
      return;
    }
    if (peek(1).type == T_MINUSMINUS) {
      tp += 2;
      Var *v = find_var(name);
      if (v) v->val.ival--;
      expect(T_SEMI);
      return;
    }

    // i += expr; i -= expr;
    if (peek(1).type == T_PLUSEQ) {
      tp += 2;
      Value val = parse_expr();
      Var *v = find_var(name);
      if (v) v->val.ival += val.ival;
      expect(T_SEMI);
      return;
    }
    if (peek(1).type == T_MINUSEQ) {
      tp += 2;
      Value val = parse_expr();
      Var *v = find_var(name);
      if (v) v->val.ival -= val.ival;
      expect(T_SEMI);
      return;
    }

    // Expression statement (Console.WriteLine, function call, etc.)
    parse_expr();
    expect(T_SEMI);
    return;
  }

  // Empty statement
  if (match(T_SEMI)) return;

  // Nested block
  if (at(T_LBRACE)) { exec_block(); return; }

  error("unexpected statement");
  tp++; // skip to avoid infinite loop
}

// ── Top-level parsing ───────────────────────────────────────────────────────
// Skip C# modifiers: public, private, protected, override, virtual, abstract, readonly, const
bool is_modifier(u8 t) {
  return t == T_PUBLIC || t == T_PRIVATE || t == T_PROTECTED ||
         t == T_OVERRIDE || t == T_VIRTUAL || t == T_ABSTRACT ||
         t == T_READONLY || t == T_CONST;
}
void skip_modifiers() {
  while (is_modifier(cur().type)) tp++;
}

void scan_functions() {
  // First pass: find all static methods
  tp = 0;
  func_count = 0;

  while (!at(T_EOF) && !had_error) {
    // Skip modifiers (public, private, etc.)
    skip_modifiers();

    // Skip 'using X.Y.Z;'
    if (match(T_USING)) {
      while (!at(T_SEMI) && !at(T_EOF)) tp++;
      match(T_SEMI);
      continue;
    }

    // Skip 'namespace X {'  (treat contents as top-level)
    if (match(T_NAMESPACE)) {
      while (!at(T_LBRACE) && !at(T_EOF)) tp++;
      match(T_LBRACE);
      continue;
    }

    // Skip namespace closing brace
    if (match(T_RBRACE)) continue;

    // class Name {
    if (match(T_CLASS)) {
      char cur_class[32];
      tok_text(cur(), cur_class, 32);
      tp++; // skip class name
      expect(T_LBRACE);

      // Methods inside class
      while (!at(T_RBRACE) && !at(T_EOF) && !had_error) {
        // Capture access modifier before skipping
        Access mem_access = ACC_PRIVATE; // C# default is private
        while (is_modifier(cur().type)) {
          if (cur().type == T_PUBLIC) mem_access = ACC_PUBLIC;
          else if (cur().type == T_PRIVATE) mem_access = ACC_PRIVATE;
          else if (cur().type == T_PROTECTED) mem_access = ACC_PROTECTED;
          tp++;
        }
        match(T_STATIC);

        if (at(T_RBRACE) || at(T_EOF)) break;

        // Peek ahead to determine: field or method?
        // Pattern: <type> <name> ( → method
        // Pattern: <type> <name> ; or = → field
        // Type can be a keyword (void, int, string, bool...) or an identifier
        i32 saved = tp;
        bool is_method = false;
        bool is_field = false;
        if (is_type_keyword(cur().type) || at(T_IDENT)) {
          tp++; // skip type
          if (at(T_IDENT)) {
            tp++; // skip name
            if (at(T_LPAREN)) is_method = true;
            else is_field = true;
          }
        }
        tp = saved; // restore

        if (is_method) {
          if (func_count >= MAX_FUNCS) { error("too many functions"); return; }
          Func &fn = funcs[func_count];

          // Parse return type
          if (is_type_keyword(cur().type)) {
            fn.ret_type = parse_type_kw();
          } else if (at(T_IDENT)) {
            char rtn[32]; tok_text(cur(), rtn, 32);
            if (!is_valid_type_ident(rtn) && !is_widget_type(rtn)) {
              char errbuf[64];
              str::cpy(errbuf, "unknown return type '");
              str::cat(errbuf, rtn);
              str::cat(errbuf, "'");
              error(errbuf);
              return;
            }
            fn.ret_type = V_VOID;
            tp++;
          } else {
            error("expected return type in method declaration");
            return;
          }

          tok_text(cur(), fn.name, 32);
          expect(T_IDENT);
          expect(T_LPAREN);

          fn.param_count = 0;
          while (!at(T_RPAREN) && !at(T_EOF) && !had_error && fn.param_count < 6) {
            if (is_type_keyword(cur().type)) {
              fn.param_types[fn.param_count] = parse_type_kw();
            } else if (at(T_IDENT)) {
              char ptn[32]; tok_text(cur(), ptn, 32);
              if (!is_valid_type_ident(ptn) && !is_widget_type(ptn)) {
                char errbuf[64];
                str::cpy(errbuf, "unknown parameter type '");
                str::cat(errbuf, ptn);
                str::cat(errbuf, "'");
                error(errbuf);
                return;
              }
              fn.param_types[fn.param_count] = V_INT;
              tp++;
            } else {
              error("expected parameter type");
              return;
            }
            if (!at(T_IDENT)) {
              error("expected parameter name");
              return;
            }
            tok_text(cur(), fn.params[fn.param_count], 32);
            expect(T_IDENT);
            fn.param_count++;
            if (!match(T_COMMA)) break;
          }
          if (had_error) return;
          expect(T_RPAREN);

          fn.tok_start = tp;
          fn.access = mem_access;
          str::ncpy(fn.owner_class, cur_class, 31);
          func_count++;
          skip_block();
        } else if (is_field) {
          // Field declaration: register as global variable, skip to ;
          bool is_wtype = false;
          if (at(T_IDENT)) {
            char tn[32]; tok_text(cur(), tn, 32);
            is_wtype = is_widget_type(tn);
            if (!is_wtype && !is_valid_type_ident(tn)) {
              char errbuf[64];
              str::cpy(errbuf, "unknown type '");
              str::cat(errbuf, tn);
              str::cat(errbuf, "'");
              error(errbuf);
              return;
            }
          } else if (!is_type_keyword(cur().type)) {
            error("expected type in field declaration");
            return;
          }
          tp++; // skip type
          char vn[32]; tok_text(cur(), vn, 32);
          tp++; // skip name
          if (is_wtype)
            add_var(vn, make_widget(-1));
          else
            add_var(vn, make_int(0));
          while (!at(T_SEMI) && !at(T_EOF)) tp++;
          match(T_SEMI);
        } else {
          // Not a valid member declaration
          char errbuf[64];
          str::cpy(errbuf, "unexpected in class: '");
          if (at(T_IDENT)) {
            char tok[24]; tok_text(cur(), tok, 24);
            str::cat(errbuf, tok);
          } else {
            str::cat(errbuf, "?");
          }
          str::cat(errbuf, "'");
          error(errbuf);
          return;
        }
      }
      match(T_RBRACE);
      continue;
    }

    // Unexpected top-level token
    char errbuf[64];
    str::cpy(errbuf, "unexpected token at top level: '");
    if (at(T_IDENT)) {
      char tok[20]; tok_text(cur(), tok, 20);
      str::cat(errbuf, tok);
    } else {
      str::cat(errbuf, "?");
    }
    str::cat(errbuf, "'");
    error(errbuf);
    return;
  }
}

// Shared output buffer for GUI mode (Console.WriteLine still works)
char gui_out_buf[256];

// Helper to call a function by pointer with optional int args
void call_func(Func *fn, i32 arg0 = 0, i32 arg1 = 0) {
  if (!fn || had_error) return;

  i32 saved_tp = tp;
  i32 saved_var_count = var_count;
  i32 saved_scope = scope_depth;
  bool saved_return = had_return;
  Value saved_retval = return_val;

  scope_depth++;
  call_depth++;
  had_return = false;
  return_val = make_void();

  // Push parameters
  for (i32 i = 0; i < fn->param_count && i < 2; i++) {
    Value v = make_int(i == 0 ? arg0 : arg1);
    add_var(fn->params[i], v);
  }

  tp = fn->tok_start;
  exec_block();

  call_depth--;
  var_count = saved_var_count;
  scope_depth = saved_scope;
  tp = saved_tp;
  had_return = saved_return;
  return_val = saved_retval;
}

void reset_state() {
  tok_count = 0;
  tp = 0;
  var_count = 0;
  scope_depth = 0;
  func_count = 0;
  had_error = false;
  had_return = false;
  return_val = make_void();
  call_depth = 0;
  gui_mode = false;
  close_requested = false;
  draw_cx = draw_cy = draw_cw = draw_ch = 0;
  widget_count = 0;
  str::memset(widgets, 0, sizeof(widgets));
}

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────────────
namespace csharp {

bool run(const char *source, char *out_buf, i32 out_size) {
  src = source;
  reset_state();
  out = out_buf;
  out_cap = out_size;
  out_len = 0;
  out[0] = '\0';

  if (!tokenize()) return false;
  prescan_classes();
  scan_functions();
  if (had_error) return false;

  Func *main_fn = find_func("Main");
  if (!main_fn) {
    error("no Main method found");
    return false;
  }

  tp = main_fn->tok_start;
  exec_block();
  return !had_error;
}

// ── GUI mode API ────────────────────────────────────────────────────────────
bool init(const char *source) {
  src = source;
  reset_state();
  gui_mode = true;
  out = gui_out_buf;
  out_cap = sizeof(gui_out_buf);
  out_len = 0;
  out[0] = '\0';

  if (!tokenize()) { syslog::error("cs", "tokenize failed"); return false; }
  syslog::info("cs", "tokens=%d", tok_count);

  prescan_classes();
  scan_functions();
  syslog::info("cs", "funcs=%d vars=%d widgets=%d err=%d",
               func_count, var_count, widget_count, had_error ? 1 : 0);
  if (had_error) { syslog::error("cs", "scan error: %s", gui_out_buf); return false; }

  for (i32 i = 0; i < func_count; i++)
    syslog::info("cs", "  func[%d]: %s tok=%d", i, funcs[i].name, funcs[i].tok_start);

  // Execute Main() if it exists (for variable initialization)
  Func *main_fn = find_func("Main");
  if (main_fn) {
    syslog::info("cs", "executing Main at tok %d", main_fn->tok_start);
    tp = main_fn->tok_start;
    exec_block();
    syslog::info("cs", "Main done: vars=%d widgets=%d err=%d",
                 var_count, widget_count, had_error ? 1 : 0);
    if (had_error) syslog::error("cs", "Main error: %s", gui_out_buf);
    had_return = false; // reset for callbacks
  } else {
    syslog::info("cs", "no Main found");
  }

  return !had_error;
}

bool has_func(const char *name) {
  return find_func(name) != nullptr;
}

void set_draw_ctx(i32 cx, i32 cy, i32 cw, i32 ch) {
  draw_cx = cx; draw_cy = cy; draw_cw = cw; draw_ch = ch;
}

void call_draw() {
  Func *fn = find_func("OnDraw");
  if (fn) call_func(fn, draw_cw, draw_ch);
}

void call_click(i32 x, i32 y) {
  Func *fn = find_func("OnClick");
  if (fn) call_func(fn, x, y);
}

bool call_key(char key) {
  Func *fn = find_func("OnKey");
  if (!fn) return false;
  call_func(fn, static_cast<i32>(key), 0);
  return true;
}

void call_arrow(char dir) {
  Func *fn = find_func("OnArrow");
  if (!fn) return;
  i32 d = 0;
  if (dir == 'A') d = 0; // up
  else if (dir == 'B') d = 1; // down
  else if (dir == 'C') d = 2; // right
  else if (dir == 'D') d = 3; // left
  call_func(fn, d, 0);
}

bool should_close() { return close_requested; }

bool has_error() { return had_error; }
const char *get_error() { return gui_out_buf; }

void gui_cleanup() {
  gui_mode = false;
  close_requested = false;
  var_count = 0;
  func_count = 0;
  widget_count = 0;
}

} // namespace csharp
