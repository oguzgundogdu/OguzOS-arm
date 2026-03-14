#include "app.h"
#include "assoc.h"
#include "commands.h"
#include "disk.h"
#include "env.h"
#include "fb.h"
#include "fs.h"
#include "graphics.h"
#include "gui.h"
#include "menu.h"
#include "net.h"
#include "netdev.h"
#include "registry.h"
#include "string.h"
#include "syslog.h"
#include "uart.h"

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

// Adapter: cmd::OutFn -> term_append
void term_out(void *ctx, const char *text) {
  term_append(reinterpret_cast<TermState *>(ctx), text);
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
    term_append(s, " ls cd pwd cat mkdir touch rm\n");
    term_append(s, " cp mv write append stat\n");
    term_append(s, " head tail wc grep find tree\n");
    term_append(s, " xxd df echo clear\n");
    term_append(s, " date hostname whoami uname\n");
    term_append(s, " uptime free dmesg which\n");
    term_append(s, " env export unset sync\n");
    term_append(s, " ifconfig ping dhcp curl\n");
    term_append(s, " assoc unassoc lsassoc\n");
    term_append(s, " pin unpin lsmenu\n");
    term_append(s, " reboot halt <app>\n");
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
  } else if (str::cmp(p, "env") == 0) {
    for (i32 i = 0; i < env::count(); i++) {
      const char *k = env::key_at(i);
      const char *v = env::value_at(i);
      if (k) {
        term_append(s, k);
        term_append(s, "=");
        term_append(s, v);
        term_append(s, "\n");
      }
    }
  } else if (str::cmp(p, "export") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: export KEY=VALUE\n");
    } else {
      // Parse KEY=VALUE
      char kv[200];
      str::ncpy(kv, arg, 199);
      char *eq = kv;
      while (*eq && *eq != '=')
        eq++;
      if (*eq == '=') {
        *eq = '\0';
        env::set(kv, eq + 1);
      } else {
        term_append(s, "export: invalid format, use KEY=VALUE\n");
      }
    }
  } else if (str::cmp(p, "unset") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: unset KEY\n");
    } else {
      if (!env::unset(arg)) {
        term_append(s, "unset: not found: ");
        term_append(s, arg);
        term_append(s, "\n");
      }
    }
  } else if (str::cmp(p, "assoc") == 0) {
    // Parse ".ext app_id"
    char a_copy[200];
    str::ncpy(a_copy, arg, 199);
    char *a2 = a_copy;
    while (*a2 && *a2 != ' ') a2++;
    if (*a2 == ' ') { *a2 = '\0'; a2++; while (*a2 == ' ') a2++; }
    if (a_copy[0] == '\0' || *a2 == '\0') {
      term_append(s, "usage: assoc .ext app.ogz\n");
    } else {
      assoc::set(a_copy, a2);
      assoc::save();
      term_append(s, a_copy);
      term_append(s, " -> ");
      term_append(s, a2);
      term_append(s, "\n");
    }
  } else if (str::cmp(p, "unassoc") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: unassoc .ext\n");
    } else if (!assoc::unset(arg)) {
      term_append(s, "unassoc: not found\n");
    } else {
      assoc::save();
    }
  } else if (str::cmp(p, "lsassoc") == 0) {
    for (i32 i = 0; i < assoc::count(); i++) {
      const char *ext = assoc::ext_at(i);
      const char *app = assoc::app_at(i);
      if (ext) {
        term_append(s, "  ");
        term_append(s, ext);
        term_append(s, " -> ");
        term_append(s, app);
        term_append(s, "\n");
      }
    }
  } else if (str::cmp(p, "pin") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: pin <app|path> [label]\n");
      term_append(s, "       pin --cmd <cmd> [label]\n");
    } else if (str::ncmp(arg, "--cmd ", 6) == 0) {
      // pin --cmd "command" [label]
      char cmd_buf[200];
      str::ncpy(cmd_buf, arg + 6, 199);
      char *clbl = cmd_buf;
      while (*clbl && *clbl != ' ') clbl++;
      if (*clbl == ' ') { *clbl = '\0'; clbl++; while (*clbl == ' ') clbl++; }
      menu::add(menu::ENTRY_COMMAND, *clbl ? clbl : cmd_buf, cmd_buf);
      menu::save();
      term_append(s, "Pinned command: ");
      term_append(s, cmd_buf);
      term_append(s, "\n");
    } else {
      // Parse "id label"
      char pin_id[64];
      str::ncpy(pin_id, arg, 63);
      char *lbl = pin_id;
      while (*lbl && *lbl != ' ') lbl++;
      if (*lbl == ' ') { *lbl = '\0'; lbl++; while (*lbl == ' ') lbl++; }
      if (apps::find(pin_id)) {
        if (menu::has_app(pin_id)) {
          term_append(s, "already in menu\n");
        } else {
          const OgzApp *a = apps::find(pin_id);
          i32 pos = 0;
          for (i32 i = 0; i < menu::count(); i++) {
            const menu::Entry *e = menu::get(i);
            if (e && e->type == menu::ENTRY_SEP) { pos = i; break; }
            pos = i + 1;
          }
          menu::insert(pos, menu::ENTRY_APP, *lbl ? lbl : a->name, pin_id);
          menu::save();
          term_append(s, "Pinned: ");
          term_append(s, pin_id);
          term_append(s, "\n");
        }
      } else {
        menu::add(menu::ENTRY_SHORTCUT, *lbl ? lbl : pin_id, pin_id);
        menu::save();
        term_append(s, "Pinned shortcut: ");
        term_append(s, pin_id);
        term_append(s, "\n");
      }
    }
  } else if (str::cmp(p, "unpin") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: unpin <app|label>\n");
    } else {
      i32 idx = menu::find(arg);
      if (idx < 0) {
        for (i32 i = 0; i < menu::count(); i++) {
          const menu::Entry *e = menu::get(i);
          if (e && str::cmp(e->label, arg) == 0) { idx = i; break; }
        }
      }
      if (idx < 0) {
        term_append(s, "unpin: not found\n");
      } else {
        const menu::Entry *e = menu::get(idx);
        if (e && e->type == menu::ENTRY_SHUTDOWN) {
          term_append(s, "unpin: cannot remove Shutdown\n");
        } else {
          menu::remove(idx);
          menu::save();
          term_append(s, "Unpinned\n");
        }
      }
    }
  } else if (str::cmp(p, "lsmenu") == 0) {
    for (i32 i = 0; i < menu::count(); i++) {
      const menu::Entry *e = menu::get(i);
      if (!e) continue;
      char ib[4];
      ib[0] = '0' + static_cast<char>(i % 10);
      if (i >= 10) { ib[0] = '0' + static_cast<char>(i / 10); ib[1] = '0' + static_cast<char>(i % 10); ib[2] = '\0'; }
      else { ib[1] = '\0'; }
      term_append(s, ib);
      term_append(s, ". ");
      if (e->type == menu::ENTRY_SEP) { term_append(s, "--------\n"); continue; }
      term_append(s, e->label);
      if (e->id[0]) { term_append(s, " ("); term_append(s, e->id); term_append(s, ")"); }
      term_append(s, "\n");
    }
  } else if (str::cmp(p, "uname") == 0) {
    term_append(s, "OguzOS 1.0.0 arm64 (QEMU virt / UTM)\n");
  } else if (str::cmp(p, "uptime") == 0) {
    u64 cnt, freq;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) freq = 1;
    u64 sec = cnt / freq;
    u64 min = sec / 60;
    u64 hr = min / 60;
    char ubuf[64];
    ubuf[0] = '\0';
    str::cat(ubuf, "up ");
    auto append_u64 = [](char *dst, u64 v) {
      char tmp[20]; i32 i = 0;
      if (v == 0) { tmp[i++] = '0'; }
      else { while (v > 0) { tmp[i++] = '0' + static_cast<char>(v % 10); v /= 10; } }
      usize len = str::len(dst);
      while (i > 0) dst[len++] = tmp[--i];
      dst[len] = '\0';
    };
    if (hr > 0) { append_u64(ubuf, hr); str::cat(ubuf, "h "); }
    append_u64(ubuf, min % 60); str::cat(ubuf, "m ");
    append_u64(ubuf, sec % 60); str::cat(ubuf, "s\n");
    term_append(s, ubuf);
  } else if (str::cmp(p, "sync") == 0) {
    if (!disk::is_available()) {
      term_append(s, "sync: no disk\n");
    } else if (fs::sync_to_disk()) {
      term_append(s, "synced\n");
    } else {
      term_append(s, "sync: failed\n");
    }
  } else if (str::cmp(p, "stat") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: stat <name>\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      i32 fi = fs::resolve(arg);
      if (fi < 0) {
        term_append(s, "stat: not found\n");
      } else {
        const fs::Node *n = fs::get_node(fi);
        if (n) {
          term_append(s, "  Name: "); term_append(s, n->name); term_append(s, "\n");
          term_append(s, "  Type: ");
          term_append(s, n->type == fs::NodeType::Directory ? "directory" : "file");
          term_append(s, "\n");
        }
      }
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "write") == 0) {
    // write <file> <text>
    char w_copy[200];
    str::ncpy(w_copy, arg, 199);
    char *wtxt = w_copy;
    while (*wtxt && *wtxt != ' ') wtxt++;
    if (*wtxt == ' ') { *wtxt = '\0'; wtxt++; while (*wtxt == ' ') wtxt++; }
    if (w_copy[0] == '\0' || *wtxt == '\0') {
      term_append(s, "usage: write <file> <text>\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      char wbuf[fs::MAX_CONTENT];
      str::ncpy(wbuf, wtxt, fs::MAX_CONTENT - 2);
      str::cat(wbuf, "\n");
      fs::write(w_copy, wbuf);
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "append") == 0) {
    char a_copy2[200];
    str::ncpy(a_copy2, arg, 199);
    char *atxt = a_copy2;
    while (*atxt && *atxt != ' ') atxt++;
    if (*atxt == ' ') { *atxt = '\0'; atxt++; while (*atxt == ' ') atxt++; }
    if (a_copy2[0] == '\0' || *atxt == '\0') {
      term_append(s, "usage: append <file> <text>\n");
    } else {
      char saved_cwd[256];
      fs::get_cwd(saved_cwd, sizeof(saved_cwd));
      fs::cd(s->cwd);
      char abuf[fs::MAX_CONTENT];
      str::ncpy(abuf, atxt, fs::MAX_CONTENT - 2);
      str::cat(abuf, "\n");
      fs::append(a_copy2, abuf);
      fs::cd(saved_cwd);
    }
  } else if (str::cmp(p, "ifconfig") == 0) {
    char cbuf[512];
    uart::capture_start(cbuf, sizeof(cbuf));
    net::ifconfig();
    uart::capture_stop();
    term_append(s, cbuf);
  } else if (str::cmp(p, "ping") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: ping <ip|host> [count]\n");
    } else {
      // Parse "target count"
      char pt[128];
      str::ncpy(pt, arg, 127);
      char *pc = pt;
      while (*pc && *pc != ' ') pc++;
      u32 cnt = 4;
      if (*pc == ' ') {
        *pc = '\0'; pc++;
        cnt = 0;
        while (*pc >= '0' && *pc <= '9') { cnt = cnt * 10 + static_cast<u32>(*pc - '0'); pc++; }
        if (cnt == 0) cnt = 1;
      }
      char cbuf[2048];
      uart::capture_start(cbuf, sizeof(cbuf));
      net::ping(pt, cnt);
      uart::capture_stop();
      term_append(s, cbuf);
    }
  } else if (str::cmp(p, "dhcp") == 0) {
    char cbuf[512];
    uart::capture_start(cbuf, sizeof(cbuf));
    net::dhcp();
    uart::capture_stop();
    term_append(s, cbuf);
  } else if (str::cmp(p, "curl") == 0) {
    if (*arg == '\0') {
      term_append(s, "usage: curl <url>\n");
    } else {
      char cbuf[3600];
      uart::capture_start(cbuf, sizeof(cbuf));
      net::curl(arg);
      uart::capture_stop();
      term_append(s, cbuf);
    }
  } else if (str::cmp(p, "halt") == 0) {
    if (disk::is_available()) fs::sync_to_disk();
    term_append(s, "System halted.\n");
    u64 psci_off = 0x84000008;
    asm volatile("mov x0, %0\nhvc #0\n" ::"r"(psci_off) : "x0");
    for (;;) asm volatile("wfe");
  } else if (str::cmp(p, "reboot") == 0) {
    if (disk::is_available()) fs::sync_to_disk();
    term_append(s, "Rebooting...\n");
    u64 psci_reset = 0x84000009;
    asm volatile("mov x0, %0\nhvc #0\n" ::"r"(psci_reset) : "x0");
    for (;;) asm volatile("wfe");
  } else if (str::cmp(p, "cp") == 0) {
    char c1[200]; str::ncpy(c1, arg, 199);
    char *c2 = c1; while (*c2 && *c2 != ' ') c2++;
    if (*c2 == ' ') { *c2 = '\0'; c2++; while (*c2 == ' ') c2++; }
    if (c1[0] == '\0' || *c2 == '\0') term_append(s, "usage: cp <src> <dst>\n");
    else { char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd); cmd::cp(term_out, s, c1, c2); fs::cd(sv); }
  } else if (str::cmp(p, "mv") == 0) {
    char c1[200]; str::ncpy(c1, arg, 199);
    char *c2 = c1; while (*c2 && *c2 != ' ') c2++;
    if (*c2 == ' ') { *c2 = '\0'; c2++; while (*c2 == ' ') c2++; }
    if (c1[0] == '\0' || *c2 == '\0') term_append(s, "usage: mv <src> <dst>\n");
    else { char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd); cmd::mv(term_out, s, c1, c2); fs::cd(sv); }
  } else if (str::cmp(p, "head") == 0) {
    if (*arg == '\0') { term_append(s, "usage: head <file> [n]\n"); }
    else {
      char hf[128]; str::ncpy(hf, arg, 127);
      char *hn = hf; while (*hn && *hn != ' ') hn++;
      i32 n = 10;
      if (*hn == ' ') { *hn = '\0'; hn++; n = 0; while (*hn >= '0' && *hn <= '9') { n = n * 10 + (*hn - '0'); hn++; } if (n == 0) n = 10; }
      char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd);
      cmd::head(term_out, s, hf, n);
      fs::cd(sv);
    }
  } else if (str::cmp(p, "tail") == 0) {
    if (*arg == '\0') { term_append(s, "usage: tail <file> [n]\n"); }
    else {
      char tf[128]; str::ncpy(tf, arg, 127);
      char *tn = tf; while (*tn && *tn != ' ') tn++;
      i32 n = 10;
      if (*tn == ' ') { *tn = '\0'; tn++; n = 0; while (*tn >= '0' && *tn <= '9') { n = n * 10 + (*tn - '0'); tn++; } if (n == 0) n = 10; }
      char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd);
      cmd::tail(term_out, s, tf, n);
      fs::cd(sv);
    }
  } else if (str::cmp(p, "wc") == 0) {
    if (*arg == '\0') term_append(s, "usage: wc <file>\n");
    else { char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd); cmd::wc(term_out, s, arg); fs::cd(sv); }
  } else if (str::cmp(p, "grep") == 0) {
    char g1[200]; str::ncpy(g1, arg, 199);
    char *g2 = g1; while (*g2 && *g2 != ' ') g2++;
    if (*g2 == ' ') { *g2 = '\0'; g2++; while (*g2 == ' ') g2++; }
    if (g1[0] == '\0' || *g2 == '\0') term_append(s, "usage: grep <pat> <file>\n");
    else { char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd); cmd::grep(term_out, s, g1, g2); fs::cd(sv); }
  } else if (str::cmp(p, "find") == 0) {
    cmd::find(term_out, s, arg);
  } else if (str::cmp(p, "tree") == 0) {
    cmd::tree(term_out, s, *arg ? arg : "/", 0);
  } else if (str::cmp(p, "df") == 0) {
    cmd::df(term_out, s);
  } else if (str::cmp(p, "xxd") == 0) {
    if (*arg == '\0') term_append(s, "usage: xxd <file>\n");
    else { char sv[256]; fs::get_cwd(sv, sizeof(sv)); fs::cd(s->cwd); cmd::xxd(term_out, s, arg); fs::cd(sv); }
  } else if (str::cmp(p, "date") == 0) {
    cmd::date(term_out, s);
  } else if (str::cmp(p, "hostname") == 0) {
    cmd::hostname(term_out, s);
  } else if (str::cmp(p, "whoami") == 0) {
    cmd::whoami(term_out, s);
  } else if (str::cmp(p, "free") == 0) {
    cmd::free_cmd(term_out, s);
  } else if (str::cmp(p, "dmesg") == 0) {
    cmd::dmesg(term_out, s);
  } else if (str::cmp(p, "which") == 0) {
    if (*arg == '\0') term_append(s, "usage: which <cmd>\n");
    else cmd::which(term_out, s, arg);
  } else {
    // Try to resolve command from PATH
    const char *app_id = env::resolve_command(p);
    if (app_id && apps::find(app_id)) {
      term_append(s, "Launching ");
      term_append(s, app_id);
      term_append(s, "...\n");
      gui::open_app(app_id);
    } else {
      term_append(s, p);
      term_append(s, ": command not found\n");
    }
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

void terminal_open_file(u8 *state, const char * /*path*/, const char *content) {
  // "content" is treated as a command to execute
  auto *s = reinterpret_cast<TermState *>(state);
  if (!content || content[0] == '\0')
    return;
  str::ncpy(s->cmd, content, 199);
  s->cmd_len = static_cast<i32>(str::len(s->cmd));
  term_append(s, s->cmd);
  term_exec(s);
  s->cmd[0] = '\0';
  s->cmd_len = 0;
}

const OgzApp terminal_app = {
    "Terminal",           // name
    "terminal.ogz",      // id
    720,                  // default_w
    500,                  // default_h
    terminal_open,        // on_open
    terminal_draw,        // on_draw
    terminal_key,         // on_key
    terminal_arrow,       // on_arrow
    terminal_close,       // on_close
    nullptr,              // on_click
    nullptr,              // on_scroll
    nullptr,              // on_mouse_down
    nullptr,              // on_mouse_move
    terminal_open_file,   // on_open_file
};

} // anonymous namespace

namespace apps {
void register_terminal() { register_app(&terminal_app); }
} // namespace apps
