#include "syslog.h"
#include "fs.h"
#include "string.h"
#include "uart.h"

namespace {

syslog::Level current_level =
    static_cast<syslog::Level>(SYSLOG_MIN_LEVEL);

bool persist_enabled = false;

// Format buffer (for UART output with ANSI colors)
constexpr usize BUF_SIZE = 256;
char buf[BUF_SIZE];
usize buf_pos = 0;

// Plain-text buffer (for file logging, no ANSI codes)
char file_line[BUF_SIZE];
usize file_pos = 0;

void buf_reset() { buf_pos = 0; }

void buf_putc(char c) {
  if (buf_pos < BUF_SIZE - 1)
    buf[buf_pos++] = c;
}

void buf_puts(const char *s) {
  while (*s && buf_pos < BUF_SIZE - 1)
    buf[buf_pos++] = *s++;
}

void buf_flush() {
  buf[buf_pos] = '\0';
  uart::puts(buf);
  buf_pos = 0;
}

// File line helpers
void fl_reset() { file_pos = 0; }

void fl_putc(char c) {
  if (file_pos < BUF_SIZE - 1)
    file_line[file_pos++] = c;
}

void fl_puts(const char *s) {
  while (*s && file_pos < BUF_SIZE - 1)
    file_line[file_pos++] = *s++;
}

void fl_finish() { file_line[file_pos] = '\0'; }

// Write decimal integer into both buffers
void dual_put_int(i64 val) {
  if (val < 0) {
    buf_putc('-');
    fl_putc('-');
    val = -val;
  }
  if (val == 0) {
    buf_putc('0');
    fl_putc('0');
    return;
  }
  char tmp[20];
  int i = 0;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  while (i > 0) {
    char c = tmp[--i];
    buf_putc(c);
    fl_putc(c);
  }
}

void dual_put_uint(u64 val) {
  if (val == 0) {
    buf_putc('0');
    fl_putc('0');
    return;
  }
  char tmp[20];
  int i = 0;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  while (i > 0) {
    char c = tmp[--i];
    buf_putc(c);
    fl_putc(c);
  }
}

void dual_put_hex(u64 val) {
  if (val == 0) {
    buf_puts("0x0");
    fl_puts("0x0");
    return;
  }
  buf_puts("0x");
  fl_puts("0x");
  char tmp[16];
  int i = 0;
  while (val > 0) {
    u8 nib = static_cast<u8>(val & 0xF);
    tmp[i++] = nib < 10 ? ('0' + nib) : ('a' + nib - 10);
    val >>= 4;
  }
  while (i > 0) {
    char c = tmp[--i];
    buf_putc(c);
    fl_putc(c);
  }
}

void dual_putc(char c) {
  buf_putc(c);
  fl_putc(c);
}

void dual_puts(const char *s) {
  buf_puts(s);
  fl_puts(s);
}

void dual_pad3(u64 val) {
  char a = '0' + static_cast<char>((val / 100) % 10);
  char b = '0' + static_cast<char>((val / 10) % 10);
  char c = '0' + static_cast<char>(val % 10);
  buf_putc(a); buf_putc(b); buf_putc(c);
  fl_putc(a);  fl_putc(b);  fl_putc(c);
}

void get_uptime(u64 &secs, u64 &ms) {
  u64 cnt, freq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (freq == 0)
    freq = 1;
  secs = cnt / freq;
  ms = (cnt % freq) * 1000 / freq;
}

// Plain-text level labels (for file)
const char *level_plain(syslog::Level l) {
  switch (l) {
  case syslog::Level::DEBUG: return "DBG ";
  case syslog::Level::INFO:  return "INFO";
  case syslog::Level::WARN:  return "WARN";
  case syslog::Level::ERROR: return "ERR ";
  }
  return "??? ";
}

// ANSI-colored level labels (for UART)
const char *level_ansi(syslog::Level l) {
  switch (l) {
  case syslog::Level::DEBUG: return "\033[0;90mDBG ";
  case syslog::Level::INFO:  return "\033[0;32mINFO";
  case syslog::Level::WARN:  return "\033[0;33mWARN";
  case syslog::Level::ERROR: return "\033[0;31mERR ";
  }
  return "??? ";
}

// Rotate log file: drop oldest lines when near capacity
void rotate_log() {
  // Save/restore global fs cwd
  char saved_cwd[256];
  fs::get_cwd(saved_cwd, sizeof(saved_cwd));
  fs::cd("/var/log");

  const char *content = fs::cat("syslog");
  if (!content) {
    fs::cd(saved_cwd);
    return;
  }

  usize len = str::len(content);
  if (len < fs::MAX_CONTENT - 300) {
    // Plenty of space, no rotation needed
    fs::cd(saved_cwd);
    return;
  }

  // Find a newline near 40% mark and drop everything before it
  usize cut = len * 2 / 5;
  while (cut < len && content[cut] != '\n')
    cut++;
  if (cut < len)
    cut++; // skip the newline

  // Rewrite file with remaining content
  fs::write("syslog", content + cut);
  fs::cd(saved_cwd);
}

// Append line to /var/log/syslog
void persist_line() {
  if (!persist_enabled)
    return;

  char saved_cwd[256];
  fs::get_cwd(saved_cwd, sizeof(saved_cwd));
  fs::cd("/var/log");

  // Check if rotation needed before append
  i32 idx = fs::resolve("/var/log/syslog");
  if (idx >= 0) {
    const fs::Node *n = fs::get_node(idx);
    if (n && n->content_len + file_pos >= fs::MAX_CONTENT - 10)
      rotate_log();
  }

  fs::append("syslog", file_line);
  fs::cd(saved_cwd);
}

void vlog(syslog::Level level, const char *subsystem, const char *fmt,
          __builtin_va_list args) {
  if (static_cast<u8>(level) < static_cast<u8>(current_level))
    return;

  buf_reset();
  fl_reset();

  // Timestamp: [SSSS.MMM] — same in both buffers
  u64 secs, ms;
  get_uptime(secs, ms);
  dual_putc('[');
  if (secs < 10)
    dual_puts("   ");
  else if (secs < 100)
    dual_puts("  ");
  else if (secs < 1000)
    dual_putc(' ');
  dual_put_uint(secs);
  dual_putc('.');
  dual_pad3(ms);
  dual_puts("] ");

  // Level: ANSI to UART, plain to file
  buf_puts(level_ansi(level));
  fl_puts(level_plain(level));

  // Subsystem tag — same in both
  dual_puts(" [");
  dual_puts(subsystem);
  dual_puts("] ");

  // Formatted message — same in both
  const char *p = fmt;
  while (*p) {
    if (*p == '%' && *(p + 1)) {
      p++;
      switch (*p) {
      case 's':
        dual_puts(__builtin_va_arg(args, const char *));
        break;
      case 'd':
      case 'i':
        dual_put_int(static_cast<i64>(__builtin_va_arg(args, int)));
        break;
      case 'l':
        if (*(p + 1) == 'd' || *(p + 1) == 'i') {
          p++;
          dual_put_int(__builtin_va_arg(args, i64));
        } else if (*(p + 1) == 'u') {
          p++;
          dual_put_uint(__builtin_va_arg(args, u64));
        } else if (*(p + 1) == 'x') {
          p++;
          dual_put_hex(__builtin_va_arg(args, u64));
        }
        break;
      case 'u':
        dual_put_uint(static_cast<u64>(__builtin_va_arg(args, unsigned int)));
        break;
      case 'x':
        dual_put_hex(static_cast<u64>(__builtin_va_arg(args, unsigned int)));
        break;
      case 'c':
        dual_putc(static_cast<char>(__builtin_va_arg(args, int)));
        break;
      case '%':
        dual_putc('%');
        break;
      default:
        dual_putc('%');
        dual_putc(*p);
        break;
      }
    } else {
      dual_putc(*p);
    }
    p++;
  }

  // UART: ANSI reset + newline
  buf_puts("\033[0m\n");
  buf_flush();

  // File: plain newline
  fl_putc('\n');
  fl_finish();
  persist_line();
}

} // anonymous namespace

namespace syslog {

void init() {
  // Create /var/log directory structure
  char saved_cwd[256];
  fs::get_cwd(saved_cwd, sizeof(saved_cwd));

  fs::cd("/");
  if (fs::resolve("/var") < 0)
    fs::mkdir("var");
  fs::cd("/var");
  if (fs::resolve("/var/log") < 0)
    fs::mkdir("log");
  fs::cd("/var/log");
  if (fs::resolve("/var/log/syslog") < 0)
    fs::touch("syslog");

  fs::cd(saved_cwd);
  persist_enabled = true;

  info("syslog", "file logging enabled -> /var/log/syslog");
}

void set_level(Level level) { current_level = level; }

Level get_level() { return current_level; }

void log(Level level, const char *subsystem, const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  vlog(level, subsystem, fmt, args);
  __builtin_va_end(args);
}

void debug(const char *subsystem, const char *fmt, ...) {
  if constexpr (SYSLOG_MIN_LEVEL > 0)
    return;
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  vlog(Level::DEBUG, subsystem, fmt, args);
  __builtin_va_end(args);
}

void info(const char *subsystem, const char *fmt, ...) {
  if constexpr (SYSLOG_MIN_LEVEL > 1)
    return;
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  vlog(Level::INFO, subsystem, fmt, args);
  __builtin_va_end(args);
}

void warn(const char *subsystem, const char *fmt, ...) {
  if constexpr (SYSLOG_MIN_LEVEL > 2)
    return;
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  vlog(Level::WARN, subsystem, fmt, args);
  __builtin_va_end(args);
}

void error(const char *subsystem, const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  vlog(Level::ERROR, subsystem, fmt, args);
  __builtin_va_end(args);
}

} // namespace syslog
