#include "app.h"
#include "fs.h"
#include "graphics.h"
#include "registry.h"
#include "string.h"
#include "syslog.h"

/*
 * terminal.ogz — OguzOS Terminal
 *
 * A GUI terminal emulator. Supports basic shell commands:
 * ls, cd, pwd, cat, mkdir, touch, rm, echo, clear, help
 */

namespace {

struct TermState {
  char output[3800]; // scrollback buffer
  i32 out_len;
  char cmd[200];     // current command line
  i32 cmd_len;
  i32 scroll;        // scroll offset (lines from bottom)
  char cwd[64];      // current working directory path
};

static_assert(sizeof(TermState) <= 4096, "TermState exceeds app_state");

constexpr u32 COL_BG = 0x001E1E2E;
constexpr u32 COL_TEXT = 0x00CDD6F4;
constexpr u32 COL_PROMPT = 0x0089B4FA;
constexpr u32 COL_CURSOR = 0x00F5E0DC;

void term_append(TermState *s, const char *text) {
  i32 tlen = static_cast<i32>(str::len(text));
  // If output buffer would overflow, drop oldest lines
  while (s->out_len + tlen >= 3799) {
    // Find first newline and remove everything up to it
    i32 nl = -1;
    for (i32 i = 0; i < s->out_len; i++) {
      if (s->output[i] == '\n') {
        nl = i;
        break;
      }
    }
    if (nl < 0)
      nl = s->out_len / 4; // drop a quarter if no newline found
    i32 remove = nl + 1;
    for (i32 i = 0; i < s->out_len - remove; i++)
      s->output[i] = s->output[i + remove];
    s->out_len -= remove;
    s->output[s->out_len] = '\0';
  }
  str::cat(s->output, text);
  s->out_len += tlen;
}

void term_prompt(TermState *s) {
  term_append(s, s->cwd);
  term_append(s, "$ ");
}

void term_exec(TermState *s) {
  // Parse command
  char cmd_copy[200];
  str::ncpy(cmd_copy, s->cmd, 199);

  // Skip leading spaces
  char *p = cmd_copy;
  while (*p == ' ')
    p++;

  if (*p == '\0') {
    term_append(s, "\n");
    term_prompt(s);
    return;
  }

  // Extract command name and argument
  char *arg = p;
  while (*arg && *arg != ' ')
    arg++;
  if (*arg == ' ') {
    *arg = '\0';
    arg++;
    while (*arg == ' ')
      arg++;
  }

  term_append(s, "\n");

  if (str::cmp(p, "help") == 0) {
    term_append(s, "Commands:\n");
    term_append(s, "  ls        List directory\n");
    term_append(s, "  cd <dir>  Change directory\n");
    term_append(s, "  pwd       Print working directory\n");
    term_append(s, "  cat <f>   Show file contents\n");
    term_append(s, "  mkdir <d> Create directory\n");
    term_append(s, "  touch <f> Create file\n");
    term_append(s, "  echo <t>  Print text\n");
    term_append(s, "  rm <f>    Remove file/dir\n");
    term_append(s, "  clear     Clear screen\n");
    term_append(s, "  help      This message\n");
  } else if (str::cmp(p, "clear") == 0) {
    s->output[0] = '\0';
    s->out_len = 0;
  } else if (str::cmp(p, "pwd") == 0) {
    term_append(s, s->cwd);
    term_append(s, "\n");
  } else if (str::cmp(p, "ls") == 0) {
    // Save/restore fs cwd
    char saved_cwd[256];
    fs::get_cwd(saved_cwd, sizeof(saved_cwd));
    fs::cd(s->cwd);

    i32 dir_idx = fs::resolve(s->cwd);
    if (dir_idx >= 0) {
      const fs::Node *dir = fs::get_node(dir_idx);
      if (dir && dir->type == fs::NodeType::Directory) {
        for (usize i = 0; i < dir->child_count; i++) {
          const fs::Node *child = fs::get_node(dir->children[i]);
          if (child && child->used) {
            if (child->type == fs::NodeType::Directory)
              term_append(s, " [dir] ");
            else
              term_append(s, "       ");
            term_append(s, child->name);
            term_append(s, "\n");
          }
        }
      }
    }
    fs::cd(saved_cwd);
  } else if (str::cmp(p, "cd") == 0) {
    if (*arg == '\0') {
      str::ncpy(s->cwd, "/", 63);
    } else if (str::cmp(arg, "..") == 0) {
      // Go up
      usize len = str::len(s->cwd);
      if (len > 1) {
        char *last = s->cwd;
        for (char *c = s->cwd; *c; c++)
          if (*c == '/')
            last = c;
        if (last == s->cwd)
          s->cwd[1] = '\0';
        else
          *last = '\0';
      }
    } else if (arg[0] == '/') {
      // Absolute path
      if (fs::resolve(arg) >= 0)
        str::ncpy(s->cwd, arg, 63);
      else
        term_append(s, "cd: no such directory\n");
    } else {
      // Relative path
      char newpath[128];
      str::ncpy(newpath, s->cwd, 127);
      if (str::len(newpath) > 1)
        str::cat(newpath, "/");
      str::cat(newpath, arg);
      if (fs::resolve(newpath) >= 0)
        str::ncpy(s->cwd, newpath, 63);
      else
        term_append(s, "cd: no such directory\n");
    }
  } else if (str::cmp(p, "cat") == 0) {
    if (*arg == '\0') {
      term_append(s, "cat: missing filename\n");
    } else {
      // Save/restore cwd
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      const char *content = fs::cat(arg);
      if (content) {
        term_append(s, content);
        if (str::len(content) > 0 && content[str::len(content) - 1] != '\n')
          term_append(s, "\n");
      } else {
        term_append(s, "cat: file not found\n");
      }
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "mkdir") == 0) {
    if (*arg == '\0') {
      term_append(s, "mkdir: missing name\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      if (!fs::mkdir(arg))
        term_append(s, "mkdir: failed\n");
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "touch") == 0) {
    if (*arg == '\0') {
      term_append(s, "touch: missing name\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      if (!fs::touch(arg))
        term_append(s, "touch: failed\n");
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "rm") == 0) {
    if (*arg == '\0') {
      term_append(s, "rm: missing name\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      if (!fs::rm(arg))
        term_append(s, "rm: failed\n");
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "echo") == 0) {
    term_append(s, arg);
    term_append(s, "\n");
  } else {
    term_append(s, p);
    term_append(s, ": command not found\n");
  }

  term_prompt(s);
}

void terminal_open(u8 *state) {
  auto *s = reinterpret_cast<TermState *>(state);
  s->output[0] = '\0';
  s->out_len = 0;
  s->cmd[0] = '\0';
  s->cmd_len = 0;
  s->scroll = 0;
  str::ncpy(s->cwd, "/", 63);

  term_append(s, "OguzOS Terminal v1.0\n");
  term_append(s, "Type 'help' for commands.\n\n");
  term_prompt(s);
  syslog::info("term", "terminal opened");
}

void terminal_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<TermState *>(state);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();
  i32 line_h = fh + 2;
  constexpr i32 PAD = 4;

  // Dark background
  gfx::fill_rect(cx, cy, cw, ch, COL_BG);

  i32 visible_lines = (ch - PAD * 2) / line_h;

  i32 draw_x = cx + PAD;
  i32 draw_y = cy + PAD;
  i32 max_x = cx + cw - PAD;

  // Count total lines
  i32 total_lines = 1;
  for (i32 i = 0; i < s->out_len; i++)
    if (s->output[i] == '\n')
      total_lines++;

  i32 start_line = 0;
  if (total_lines > visible_lines)
    start_line = total_lines - visible_lines;

  i32 cur_line = 0;
  i32 tx = draw_x;
  i32 ty = draw_y;

  // Draw output
  for (i32 i = 0; i < s->out_len; i++) {
    if (cur_line >= start_line && ty + fh <= cy + ch - PAD) {
      if (s->output[i] == '\n') {
        // newline
      } else if (tx + fw <= max_x) {
        gfx::draw_char(tx, ty, s->output[i], COL_TEXT, COL_BG);
        tx += fw;
      }
    }
    if (s->output[i] == '\n') {
      cur_line++;
      if (cur_line >= start_line)
        ty += line_h;
      tx = draw_x;
    }
  }

  // Draw current command text
  for (i32 i = 0; i < s->cmd_len; i++) {
    if (ty + fh <= cy + ch - PAD && tx + fw <= max_x) {
      gfx::draw_char(tx, ty, s->cmd[i], COL_TEXT, COL_BG);
      tx += fw;
    }
  }

  // Draw cursor
  if (ty + fh <= cy + ch - PAD) {
    gfx::fill_rect(tx, ty, fw, fh, COL_CURSOR);
  }
}

bool terminal_key(u8 *state, char key) {
  auto *s = reinterpret_cast<TermState *>(state);

  syslog::debug("term", "key received: 0x%x ('%c')",
                static_cast<unsigned int>(static_cast<u8>(key)),
                (key >= 32 && key <= 126) ? static_cast<int>(key) : static_cast<int>('.'));

  if (key == '\r' || key == '\n') {
    syslog::info("term", "exec: '%s'", s->cmd);
    // Echo command to output
    term_append(s, s->cmd);
    // Execute
    term_exec(s);
    s->cmd[0] = '\0';
    s->cmd_len = 0;
    return true;
  }

  if (key == 0x7F || key == 0x08) { // Backspace
    if (s->cmd_len > 0) {
      s->cmd_len--;
      s->cmd[s->cmd_len] = '\0';
    }
    return true;
  }

  // Printable
  if (key >= 32 && key <= 126 && s->cmd_len < 199) {
    s->cmd[s->cmd_len++] = key;
    s->cmd[s->cmd_len] = '\0';
    return true;
  }

  syslog::debug("term", "key ignored: 0x%x", static_cast<unsigned int>(static_cast<u8>(key)));
  return false;
}

void terminal_arrow(u8 *, char) {
  // Could implement command history here later
}

void terminal_close(u8 *) {}

const OgzApp terminal_app = {
    "Terminal",        // name
    "terminal.ogz",   // id
    720,               // default_w
    500,               // default_h
    terminal_open,     // on_open
    terminal_draw,     // on_draw
    terminal_key,      // on_key
    terminal_arrow,    // on_arrow
    terminal_close,    // on_close
    nullptr,           // on_click
    nullptr,           // on_scroll
    nullptr,           // on_mouse_down
    nullptr,           // on_mouse_move
};

} // anonymous namespace

namespace apps {
void register_terminal() { register_app(&terminal_app); }
} // namespace apps
