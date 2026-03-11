#include "shell.h"
#include "disk.h"
#include "fs.h"
#include "net.h"
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
  uart::puts("\n  \033[1;36m-- System --\033[0m\n");
  uart::puts("  \033[1mhalt\033[0m              Sync & halt the system\n");
  uart::puts("  \033[1mreboot\033[0m            Sync & reboot\n");
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
  else if (str::cmp(cmd, "halt") == 0)
    cmd_halt();
  else if (str::cmp(cmd, "reboot") == 0)
    cmd_reboot();
  else {
    uart::puts("oguzos: command not found: ");
    uart::puts(cmd);
    uart::puts("\n");
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
