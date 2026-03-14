#include "shell.h"
#include "assoc.h"
#include "disk.h"
#include "env.h"
#include "menu.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "net.h"
#include "registry.h"
#include "string.h"
#include "types.h"
#include "uart.h"

namespace {

constexpr usize CMD_BUF_SIZE = 256;
constexpr usize MAX_ARGS = 16;

char cmd_buf[CMD_BUF_SIZE];
usize cmd_len = 0;

// History
constexpr usize HISTORY_SIZE = 16;
char history[HISTORY_SIZE][CMD_BUF_SIZE];
usize history_count = 0;
usize history_index = 0;

void add_to_history(const char *cmd) {
  if (cmd[0] == '\0')
    return;
  str::ncpy(history[history_count % HISTORY_SIZE], cmd, CMD_BUF_SIZE);
  history_count++;
  history_index = history_count;
}

void print_prompt() {
  char cwd[256];
  fs::get_cwd(cwd, sizeof(cwd));
  uart::puts("\033[1;32moguz@oguzos\033[0m:\033[1;34m");
  uart::puts(cwd);
  uart::puts("\033[0m$ ");
}

// Parse command string into arguments
usize parse_args(char *input, char *args[], usize max_args) {
  usize argc = 0;
  char *p = input;

  while (*p && argc < max_args) {
    // Skip whitespace
    while (*p && str::is_whitespace(*p))
      p++;
    if (!*p)
      break;

    // Check for quoted string
    if (*p == '"') {
      p++; // Skip opening quote
      args[argc++] = p;
      while (*p && *p != '"')
        p++;
      if (*p)
        *p++ = '\0';
    } else {
      args[argc++] = p;
      while (*p && !str::is_whitespace(*p))
        p++;
      if (*p)
        *p++ = '\0';
    }
  }
  return argc;
}

// ---- Built-in commands ----

void cmd_help() {
  uart::puts("\n\033[1;33m=== OguzOS Commands ===\033[0m\n\n");
  uart::puts("  \033[1mhelp\033[0m              Show this help message\n");
  uart::puts("  \033[1mclear\033[0m             Clear the screen\n");
  uart::puts("  \033[1mecho\033[0m <text>       Print text to console\n");
  uart::puts("  \033[1muname\033[0m             Show system information\n");
  uart::puts("  \033[1muptime\033[0m            Show (simulated) uptime\n");
  uart::puts("  \033[1mhistory\033[0m           Show command history\n");
  uart::puts("\n  \033[1;36m-- File System --\033[0m\n");
  uart::puts("  \033[1mpwd\033[0m               Print working directory\n");
  uart::puts("  \033[1mls\033[0m                List directory contents\n");
  uart::puts("  \033[1mcd\033[0m <dir>          Change directory\n");
  uart::puts("  \033[1mmkdir\033[0m <name>      Create a directory\n");
  uart::puts("  \033[1mtouch\033[0m <name>      Create an empty file\n");
  uart::puts("  \033[1mcat\033[0m <file>        Display file contents\n");
  uart::puts("  \033[1mwrite\033[0m <file> <text>  Write text to file\n");
  uart::puts("  \033[1mappend\033[0m <file> <text> Append text to file\n");
  uart::puts(
      "  \033[1mrm\033[0m <name>         Remove file or empty directory\n");
  uart::puts("  \033[1mstat\033[0m <name>        Show file/directory info\n");
  uart::puts("  \033[1msync\033[0m              Save filesystem to disk\n");
  uart::puts("\n  \033[1;36m-- Network --\033[0m\n");
  uart::puts("  \033[1mifconfig\033[0m          Show network configuration\n");
  uart::puts(
      "  \033[1mping\033[0m <ip|host> [count] Ping an IP/hostname (default: 4)\n");
  uart::puts("  \033[1mdhcp\033[0m              Request IP via DHCP\n");
  uart::puts(
      "  \033[1mcurl\033[0m <url>        Fetch a URL via HTTP (GET)\n");
  uart::puts("\n  \033[1;36m-- Environment --\033[0m\n");
  uart::puts("  \033[1menv\033[0m               Show environment variables\n");
  uart::puts("  \033[1mexport\033[0m KEY=VALUE  Set environment variable\n");
  uart::puts("  \033[1munset\033[0m KEY         Remove environment variable\n");
  uart::puts("\n  \033[1;36m-- File Associations --\033[0m\n");
  uart::puts("  \033[1massoc\033[0m .ext app    Associate extension with app\n");
  uart::puts("  \033[1munassoc\033[0m .ext      Remove association\n");
  uart::puts("  \033[1mlsassoc\033[0m           List all associations\n");
  uart::puts("\n  \033[1;36m-- Start Menu --\033[0m\n");
  uart::puts("  \033[1mpin\033[0m <app> [label] Pin app or path to menu\n");
  uart::puts("  \033[1munpin\033[0m <app|label> Remove from menu\n");
  uart::puts("  \033[1mlsmenu\033[0m            List menu entries\n");
  uart::puts("\n  \033[1;36m-- System --\033[0m\n");
  uart::puts("  \033[1mgui\033[0m               Launch graphical desktop\n");
  uart::puts("  \033[1mhalt\033[0m              Sync & halt the system\n");
  uart::puts("  \033[1mreboot\033[0m            Sync & reboot\n");
  uart::puts("\n  Apps in \033[1mPATH\033[0m (/bin) can be launched by name.\n");
  uart::putc('\n');
}

void cmd_clear() { uart::puts("\033[2J\033[H"); }

void cmd_echo(usize argc, char *args[]) {
  for (usize i = 1; i < argc; i++) {
    if (i > 1)
      uart::putc(' ');
    uart::puts(args[i]);
  }
  uart::putc('\n');
}

void cmd_uname() { uart::puts("OguzOS 1.0.0 arm64 (QEMU virt / UTM)\n"); }

void cmd_uptime() {
  // Read the ARM generic timer
  u64 cnt;
  u64 freq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (freq == 0)
    freq = 1;
  u64 seconds = cnt / freq;
  u64 minutes = seconds / 60;
  u64 hours = minutes / 60;

  uart::puts("up ");
  if (hours > 0) {
    uart::put_int(static_cast<i64>(hours));
    uart::puts("h ");
  }
  uart::put_int(static_cast<i64>(minutes % 60));
  uart::puts("m ");
  uart::put_int(static_cast<i64>(seconds % 60));
  uart::puts("s\n");
}

void cmd_history_show() {
  usize start = 0;
  if (history_count > HISTORY_SIZE) {
    start = history_count - HISTORY_SIZE;
  }
  for (usize i = start; i < history_count; i++) {
    uart::puts("  ");
    uart::put_int(static_cast<i64>(i + 1));
    uart::puts("  ");
    uart::puts(history[i % HISTORY_SIZE]);
    uart::putc('\n');
  }
}

void cmd_pwd() {
  char cwd[256];
  fs::get_cwd(cwd, sizeof(cwd));
  uart::puts(cwd);
  uart::putc('\n');
}

void cmd_cd(usize argc, char *args[]) {
  const char *target = (argc > 1) ? args[1] : "/";
  if (!fs::cd(target)) {
    uart::puts("cd: no such directory: ");
    uart::puts(target);
    uart::putc('\n');
  }
}

void cmd_mkdir(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: mkdir <name>\n");
    return;
  }
  fs::mkdir(args[1]);
}

void cmd_touch(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: touch <name>\n");
    return;
  }
  fs::touch(args[1]);
}

void cmd_cat(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: cat <file>\n");
    return;
  }
  const char *content = fs::cat(args[1]);
  if (content) {
    uart::puts(content);
    // Ensure trailing newline
    usize len = str::len(content);
    if (len > 0 && content[len - 1] != '\n') {
      uart::putc('\n');
    }
  } else {
    uart::puts("cat: file not found: ");
    uart::puts(args[1]);
    uart::putc('\n');
  }
}

void cmd_write(usize argc, char *args[]) {
  if (argc < 3) {
    uart::puts("usage: write <file> <text>\n");
    return;
  }
  // Reconstruct the text from args[2..argc]
  char text[fs::MAX_CONTENT];
  text[0] = '\0';
  for (usize i = 2; i < argc; i++) {
    if (i > 2)
      str::cat(text, " ");
    str::cat(text, args[i]);
  }
  str::cat(text, "\n");
  fs::write(args[1], text);
}

void cmd_append(usize argc, char *args[]) {
  if (argc < 3) {
    uart::puts("usage: append <file> <text>\n");
    return;
  }
  char text[fs::MAX_CONTENT];
  text[0] = '\0';
  for (usize i = 2; i < argc; i++) {
    if (i > 2)
      str::cat(text, " ");
    str::cat(text, args[i]);
  }
  str::cat(text, "\n");
  fs::append(args[1], text);
}

void cmd_rm(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: rm <name>\n");
    return;
  }
  fs::rm(args[1]);
}

void cmd_stat(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: stat <name>\n");
    return;
  }
  fs::stat(args[1]);
}

void cmd_sync() {
  if (!disk::is_available()) {
    uart::puts("sync: no disk available\n");
    return;
  }
  uart::puts("Syncing filesystem to disk... ");
  if (fs::sync_to_disk()) {
    uart::puts("\033[1;32mdone\033[0m\n");
  } else {
    uart::puts("\033[1;31mfailed\033[0m\n");
  }
}

void cmd_ifconfig() { net::ifconfig(); }

void cmd_ping(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: ping <ip|host> [count]\n");
    return;
  }
  u32 count = 4;
  if (argc >= 3) {
    count = 0;
    for (const char *p = args[2]; *p >= '0' && *p <= '9'; p++)
      count = count * 10 + static_cast<u32>(*p - '0');
    if (count == 0)
      count = 1;
  }
  net::ping(args[1], count);
}

void cmd_dhcp() { net::dhcp(); }

void cmd_curl(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: curl <url>\n");
    return;
  }
  net::curl(args[1]);
}

void cmd_halt() {
  if (disk::is_available()) {
    uart::puts("Syncing to disk... ");
    if (fs::sync_to_disk())
      uart::puts("ok\n");
    else
      uart::puts("failed\n");
  }
  uart::puts("System halted.\n");
  // QEMU virt PSCI SYSTEM_OFF (0x84000008)
  u64 psci_off = 0x84000008;
  asm volatile("mov x0, %0\n"
               "hvc #0\n" ::"r"(psci_off)
               : "x0");
  // If HVC doesn't work, just loop
  for (;;) {
    asm volatile("wfe");
  }
}

void cmd_gui() {
  if (!fb::is_available()) {
    uart::puts("gui: framebuffer not available\n");
    uart::puts("  Run with: make gui\n");
    return;
  }
  uart::puts("Entering GUI mode... (press Escape to return)\n");
  gui::run();
  uart::puts("\033[2J\033[H"); // clear screen on return
  uart::puts("Returned to shell.\n");
}

void cmd_env() {
  for (i32 i = 0; i < env::count(); i++) {
    const char *k = env::key_at(i);
    const char *v = env::value_at(i);
    if (k) {
      uart::puts(k);
      uart::putc('=');
      uart::puts(v);
      uart::putc('\n');
    }
  }
}

void cmd_export(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: export KEY=VALUE\n");
    return;
  }
  // Parse KEY=VALUE
  char *eq = args[1];
  while (*eq && *eq != '=')
    eq++;
  if (*eq != '=') {
    uart::puts("export: invalid format, use KEY=VALUE\n");
    return;
  }
  *eq = '\0';
  env::set(args[1], eq + 1);
}

void cmd_unset(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: unset KEY\n");
    return;
  }
  if (!env::unset(args[1])) {
    uart::puts("unset: not found: ");
    uart::puts(args[1]);
    uart::putc('\n');
  }
}

void cmd_assoc(usize argc, char *args[]) {
  if (argc < 3) {
    uart::puts("usage: assoc <.ext> <app_id>\n");
    uart::puts("  e.g.: assoc .txt notepad.ogz\n");
    return;
  }
  assoc::set(args[1], args[2]);
  assoc::save();
  uart::puts(args[1]);
  uart::puts(" -> ");
  uart::puts(args[2]);
  uart::putc('\n');
}

void cmd_unassoc(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: unassoc <.ext>\n");
    return;
  }
  if (!assoc::unset(args[1])) {
    uart::puts("unassoc: not found: ");
    uart::puts(args[1]);
    uart::putc('\n');
  } else {
    assoc::save();
  }
}

void cmd_lsassoc() {
  if (assoc::count() == 0) {
    uart::puts("  (no file associations)\n");
    return;
  }
  for (i32 i = 0; i < assoc::count(); i++) {
    const char *ext = assoc::ext_at(i);
    const char *app = assoc::app_at(i);
    if (ext) {
      uart::puts("  ");
      uart::puts(ext);
      uart::puts(" -> ");
      uart::puts(app);
      uart::putc('\n');
    }
  }
}

void cmd_pin(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: pin <app_id|path> [label]\n");
    uart::puts("       pin --cmd \"command\" [label]\n");
    uart::puts("  e.g.: pin notepad.ogz\n");
    uart::puts("        pin /home \"My Files\"\n");
    uart::puts("        pin --cmd \"ping 10.0.2.2\" \"Ping GW\"\n");
    return;
  }

  // --cmd flag: pin a shell command
  if (str::cmp(args[1], "--cmd") == 0) {
    if (argc < 3) {
      uart::puts("pin: --cmd requires a command\n");
      return;
    }
    const char *cmd = args[2];
    const char *label = (argc >= 4) ? args[3] : cmd;
    menu::add(menu::ENTRY_COMMAND, label, cmd);
    menu::save();
    uart::puts("Pinned command: ");
    uart::puts(label);
    uart::putc('\n');
    return;
  }

  const char *id = args[1];
  const char *label = (argc >= 3) ? args[2] : "";

  // Check if it's an app id
  if (apps::find(id)) {
    if (menu::has_app(id)) {
      uart::puts("pin: already in menu: ");
      uart::puts(id);
      uart::putc('\n');
      return;
    }
    // Insert before first separator
    i32 pos = 0;
    for (i32 i = 0; i < menu::count(); i++) {
      const menu::Entry *e = menu::get(i);
      if (e && e->type == menu::ENTRY_SEP) {
        pos = i;
        break;
      }
      pos = i + 1;
    }
    const OgzApp *app = apps::find(id);
    menu::insert(pos, menu::ENTRY_APP, label[0] ? label : app->name, id);
  } else {
    // Treat as a shortcut path
    menu::add(menu::ENTRY_SHORTCUT, label[0] ? label : id, id);
  }
  menu::save();
  uart::puts("Pinned: ");
  uart::puts(id);
  uart::putc('\n');
}

void cmd_unpin(usize argc, char *args[]) {
  if (argc < 2) {
    uart::puts("usage: unpin <app_id|label>\n");
    return;
  }
  i32 idx = menu::find(args[1]);
  if (idx < 0) {
    // Try matching by label
    for (i32 i = 0; i < menu::count(); i++) {
      const menu::Entry *e = menu::get(i);
      if (e && str::cmp(e->label, args[1]) == 0) {
        idx = i;
        break;
      }
    }
  }
  if (idx < 0) {
    uart::puts("unpin: not found: ");
    uart::puts(args[1]);
    uart::putc('\n');
    return;
  }
  const menu::Entry *e = menu::get(idx);
  // Don't allow removing built-in shutdown
  if (e && e->type == menu::ENTRY_SHUTDOWN) {
    uart::puts("unpin: cannot remove Shutdown\n");
    return;
  }
  menu::remove(idx);
  menu::save();
  uart::puts("Unpinned: ");
  uart::puts(args[1]);
  uart::putc('\n');
}

void cmd_lsmenu() {
  if (menu::count() == 0) {
    uart::puts("  (empty menu)\n");
    return;
  }
  for (i32 i = 0; i < menu::count(); i++) {
    const menu::Entry *e = menu::get(i);
    if (!e)
      continue;
    uart::puts("  ");
    char idx_buf[8];
    i32 v = i, j = 0;
    char tmp[8];
    if (v == 0) { tmp[j++] = '0'; }
    else { while (v > 0) { tmp[j++] = '0' + (v % 10); v /= 10; } }
    i32 k = 0;
    while (j > 0) idx_buf[k++] = tmp[--j];
    idx_buf[k] = '\0';
    uart::puts(idx_buf);
    uart::puts(". ");
    switch (e->type) {
    case menu::ENTRY_APP:
      uart::puts("[app] ");
      break;
    case menu::ENTRY_SHORTCUT:
      uart::puts("[shortcut] ");
      break;
    case menu::ENTRY_SEP:
      uart::puts("--------\n");
      continue;
    case menu::ENTRY_EXPLORER:
      uart::puts("[explorer] ");
      break;
    case menu::ENTRY_ABOUT:
      uart::puts("[about] ");
      break;
    case menu::ENTRY_SHUTDOWN:
      uart::puts("[shutdown] ");
      break;
    case menu::ENTRY_COMMAND:
      uart::puts("[cmd] ");
      break;
    }
    uart::puts(e->label);
    if (e->id[0]) {
      uart::puts(" (");
      uart::puts(e->id);
      uart::puts(")");
    }
    uart::putc('\n');
  }
}

void cmd_reboot() {
  if (disk::is_available()) {
    uart::puts("Syncing to disk... ");
    if (fs::sync_to_disk())
      uart::puts("ok\n");
    else
      uart::puts("failed\n");
  }
  uart::puts("Rebooting...\n");
  // QEMU virt PSCI SYSTEM_RESET (0x84000009)
  u64 psci_reset = 0x84000009;
  asm volatile("mov x0, %0\n"
               "hvc #0\n" ::"r"(psci_reset)
               : "x0");
  for (;;) {
    asm volatile("wfe");
  }
}

void execute(char *input) {
  char *args[MAX_ARGS];
  usize argc = parse_args(input, args, MAX_ARGS);

  if (argc == 0)
    return;

  const char *cmd = args[0];

  if (str::cmp(cmd, "help") == 0)
    cmd_help();
  else if (str::cmp(cmd, "clear") == 0)
    cmd_clear();
  else if (str::cmp(cmd, "echo") == 0)
    cmd_echo(argc, args);
  else if (str::cmp(cmd, "uname") == 0)
    cmd_uname();
  else if (str::cmp(cmd, "uptime") == 0)
    cmd_uptime();
  else if (str::cmp(cmd, "history") == 0)
    cmd_history_show();
  else if (str::cmp(cmd, "pwd") == 0)
    cmd_pwd();
  else if (str::cmp(cmd, "ls") == 0)
    fs::ls();
  else if (str::cmp(cmd, "cd") == 0)
    cmd_cd(argc, args);
  else if (str::cmp(cmd, "mkdir") == 0)
    cmd_mkdir(argc, args);
  else if (str::cmp(cmd, "touch") == 0)
    cmd_touch(argc, args);
  else if (str::cmp(cmd, "cat") == 0)
    cmd_cat(argc, args);
  else if (str::cmp(cmd, "write") == 0)
    cmd_write(argc, args);
  else if (str::cmp(cmd, "append") == 0)
    cmd_append(argc, args);
  else if (str::cmp(cmd, "rm") == 0)
    cmd_rm(argc, args);
  else if (str::cmp(cmd, "stat") == 0)
    cmd_stat(argc, args);
  else if (str::cmp(cmd, "sync") == 0)
    cmd_sync();
  else if (str::cmp(cmd, "ifconfig") == 0)
    cmd_ifconfig();
  else if (str::cmp(cmd, "ping") == 0)
    cmd_ping(argc, args);
  else if (str::cmp(cmd, "dhcp") == 0)
    cmd_dhcp();
  else if (str::cmp(cmd, "curl") == 0)
    cmd_curl(argc, args);
  else if (str::cmp(cmd, "gui") == 0)
    cmd_gui();
  else if (str::cmp(cmd, "halt") == 0)
    cmd_halt();
  else if (str::cmp(cmd, "reboot") == 0)
    cmd_reboot();
  else if (str::cmp(cmd, "env") == 0)
    cmd_env();
  else if (str::cmp(cmd, "export") == 0)
    cmd_export(argc, args);
  else if (str::cmp(cmd, "unset") == 0)
    cmd_unset(argc, args);
  else if (str::cmp(cmd, "assoc") == 0)
    cmd_assoc(argc, args);
  else if (str::cmp(cmd, "unassoc") == 0)
    cmd_unassoc(argc, args);
  else if (str::cmp(cmd, "lsassoc") == 0)
    cmd_lsassoc();
  else if (str::cmp(cmd, "pin") == 0)
    cmd_pin(argc, args);
  else if (str::cmp(cmd, "unpin") == 0)
    cmd_unpin(argc, args);
  else if (str::cmp(cmd, "lsmenu") == 0)
    cmd_lsmenu();
  else {
    // Try to resolve command from PATH (e.g. "notepad" -> "/bin/notepad.ogz")
    const char *app_id = env::resolve_command(cmd);
    if (app_id && apps::find(app_id)) {
      if (fb::is_available()) {
        uart::puts("Launching ");
        uart::puts(app_id);
        uart::puts("...\n");
        gui::open_app(app_id);
      } else {
        uart::puts(cmd);
        uart::puts(": GUI required (run with: make gui)\n");
      }
    } else {
      uart::puts("oguzos: command not found: ");
      uart::puts(cmd);
      uart::puts("\n");
    }
  }
}

void read_line() {
  cmd_len = 0;
  str::memset(cmd_buf, 0, CMD_BUF_SIZE);

  while (true) {
    char c = uart::getc();

    if (c == '\r' || c == '\n') {
      uart::puts("\r\n");
      cmd_buf[cmd_len] = '\0';
      return;
    }

    // Backspace / DEL
    if (c == 0x7F || c == '\b') {
      if (cmd_len > 0) {
        cmd_len--;
        uart::puts("\b \b");
      }
      continue;
    }

    // Escape sequences (arrow keys)
    if (c == 0x1B) {
      char c2 = uart::getc();
      if (c2 == '[') {
        char c3 = uart::getc();
        if (c3 == 'A') { // Up arrow - previous history
          if (history_count > 0 && history_index > 0) {
            history_index--;
            usize idx = history_index % HISTORY_SIZE;
            // Clear current line
            while (cmd_len > 0) {
              uart::puts("\b \b");
              cmd_len--;
            }
            str::cpy(cmd_buf, history[idx]);
            cmd_len = str::len(cmd_buf);
            uart::puts(cmd_buf);
          }
        } else if (c3 == 'B') { // Down arrow - next history
          if (history_index < history_count) {
            history_index++;
            while (cmd_len > 0) {
              uart::puts("\b \b");
              cmd_len--;
            }
            if (history_index < history_count) {
              usize idx = history_index % HISTORY_SIZE;
              str::cpy(cmd_buf, history[idx]);
              cmd_len = str::len(cmd_buf);
              uart::puts(cmd_buf);
            } else {
              cmd_buf[0] = '\0';
              cmd_len = 0;
            }
          }
        }
        // Ignore other escape sequences (left/right, etc.)
      }
      continue;
    }

    // Ctrl+C
    if (c == 0x03) {
      uart::puts("^C\n");
      cmd_len = 0;
      cmd_buf[0] = '\0';
      return;
    }

    // Ctrl+L (clear screen)
    if (c == 0x0C) {
      cmd_clear();
      print_prompt();
      uart::puts(cmd_buf);
      continue;
    }

    // Regular character
    if (cmd_len < CMD_BUF_SIZE - 1 && c >= 0x20) {
      cmd_buf[cmd_len++] = c;
      uart::putc(c);
    }
  }
}

} // anonymous namespace

namespace shell {

void init() {
  cmd_len = 0;
  history_count = 0;
  history_index = 0;
}

void run() {
  while (true) {
    print_prompt();
    read_line();
    if (cmd_buf[0] != '\0') {
      add_to_history(cmd_buf);
      // Make a copy since parse_args modifies the string
      char exec_buf[CMD_BUF_SIZE];
      str::cpy(exec_buf, cmd_buf);
      execute(exec_buf);
    }
  }
}

} // namespace shell
