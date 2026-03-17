#include "csharp.h"
#include "string.h"

namespace {

// ── Limits ──────────────────────────────────────────────────────────────────
constexpr i32 MAX_TOKENS = 1024;
constexpr i32 MAX_VARS = 48;
constexpr i32 MAX_FUNCS = 16;
constexpr i32 MAX_CALL = 16;
constexpr i32 MAX_SLEN = 96;

// ── Token types ─────────────────────────────────────────────────────────────
enum Tok : u8 {
  T_EOF, T_INT_LIT, T_STR_LIT, T_IDENT,
  // Keywords
  T_USING, T_CLASS, T_STATIC, T_VOID, T_INT, T_STRING, T_BOOL,
  T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN, T_TRUE, T_FALSE, T_NULL, T_NEW,
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
enum VType : u8 { V_VOID, V_INT, V_STRING, V_BOOL };

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
struct Func {
  char name[32];
  i32 tok_start;    // token index of first '{' of body
  i32 param_count;
  char params[6][32];
  VType param_types[6];
  VType ret_type;
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

char *out;
i32 out_cap;
i32 out_len;

bool had_error;
bool had_return;
Value return_val;

i32 call_depth;

// ── Output helpers ──────────────────────────────────────────────────────────
void emit(const char *s) {
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

    // Integer literal
    if (is_digit(c)) {
      i32 start = i;
      i32 val = 0;
      while (i < slen && is_digit(src[i])) {
        val = val * 10 + (src[i] - '0');
        i++;
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
    error("unexpected token");
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
  if (match(T_INT)) return V_INT;
  if (match(T_STRING)) return V_STRING;
  if (match(T_BOOL)) return V_BOOL;
  return V_VOID;
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

  // Variable declaration: int x = expr; / string s = expr; / bool b = expr;
  if (at(T_INT) || at(T_STRING) || at(T_BOOL)) {
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

  // if statement
  if (match(T_IF)) {
    expect(T_LPAREN);
    Value cond = parse_expr();
    expect(T_RPAREN);
    bool truthy = cond.type == V_BOOL ? cond.bval : (cond.ival != 0);

    if (truthy) {
      exec_block();
      if (match(T_ELSE)) skip_block();
    } else {
      skip_block();
      if (match(T_ELSE)) exec_block();
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
    if (at(T_INT) || at(T_STRING) || at(T_BOOL)) {
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
void scan_functions() {
  // First pass: find all static methods
  tp = 0;
  func_count = 0;

  while (!at(T_EOF) && !had_error) {
    // Skip 'using X.Y.Z;'
    if (match(T_USING)) {
      while (!at(T_SEMI) && !at(T_EOF)) tp++;
      match(T_SEMI);
      continue;
    }

    // class Name {
    if (match(T_CLASS)) {
      tp++; // skip class name
      expect(T_LBRACE);

      // Methods inside class
      while (!at(T_RBRACE) && !at(T_EOF) && !had_error) {
        if (match(T_STATIC)) {
          if (func_count >= MAX_FUNCS) { error("too many functions"); return; }
          Func &fn = funcs[func_count];
          fn.ret_type = parse_type_kw();

          tok_text(cur(), fn.name, 32);
          expect(T_IDENT);
          expect(T_LPAREN);

          // Parameters
          fn.param_count = 0;
          while (!at(T_RPAREN) && !at(T_EOF) && fn.param_count < 6) {
            fn.param_types[fn.param_count] = parse_type_kw();
            tok_text(cur(), fn.params[fn.param_count], 32);
            expect(T_IDENT);
            fn.param_count++;
            if (!match(T_COMMA)) break;
          }
          expect(T_RPAREN);

          fn.tok_start = tp; // points to '{'
          func_count++;

          // Skip the body
          skip_block();
        } else {
          tp++; // skip unknown token
        }
      }
      match(T_RBRACE);
      continue;
    }

    tp++; // skip unrecognized top-level token
  }
}

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────────────
namespace csharp {

bool run(const char *source, char *out_buf, i32 out_size) {
  // Reset state
  src = source;
  tok_count = 0;
  tp = 0;
  var_count = 0;
  scope_depth = 0;
  func_count = 0;
  out = out_buf;
  out_cap = out_size;
  out_len = 0;
  out[0] = '\0';
  had_error = false;
  had_return = false;
  return_val = make_void();
  call_depth = 0;

  // Tokenize
  if (!tokenize()) return false;

  // First pass: find functions
  scan_functions();
  if (had_error) return false;

  // Find and execute Main
  Func *main_fn = find_func("Main");
  if (!main_fn) {
    error("no Main method found");
    return false;
  }

  tp = main_fn->tok_start;
  exec_block();

  return !had_error;
}

} // namespace csharp
