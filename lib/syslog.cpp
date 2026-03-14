#include "syslog.h"
#include "string.h"
#include "uart.h"

namespace {

syslog::Level current_level =
    static_cast<syslog::Level>(SYSLOG_MIN_LEVEL);

// Format buffer
constexpr usize BUF_SIZE = 256;
char buf[BUF_SIZE];
usize buf_pos = 0;

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

// Write decimal integer into buffer
void buf_put_int(i64 val) {
  if (val < 0) {
    buf_putc('-');
    val = -val;
  }
  if (val == 0) {
    buf_putc('0');
    return;
  }
  char tmp[20];
  int i = 0;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  while (i > 0)
    buf_putc(tmp[--i]);
}

// Write unsigned integer into buffer
void buf_put_uint(u64 val) {
  if (val == 0) {
    buf_putc('0');
    return;
  }
  char tmp[20];
  int i = 0;
  while (val > 0) {
    tmp[i++] = '0' + static_cast<char>(val % 10);
    val /= 10;
  }
  while (i > 0)
    buf_putc(tmp[--i]);
}

// Write hex integer into buffer
void buf_put_hex(u64 val) {
  if (val == 0) {
    buf_puts("0x0");
    return;
  }
  buf_puts("0x");
  char tmp[16];
  int i = 0;
  while (val > 0) {
    u8 nib = static_cast<u8>(val & 0xF);
    tmp[i++] = nib < 10 ? ('0' + nib) : ('a' + nib - 10);
    val >>= 4;
  }
  while (i > 0)
    buf_putc(tmp[--i]);
}

// Write zero-padded decimal (for timestamps)
void buf_pad3(u64 val) {
  buf_putc('0' + static_cast<char>((val / 100) % 10));
  buf_putc('0' + static_cast<char>((val / 10) % 10));
  buf_putc('0' + static_cast<char>(val % 10));
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

const char *level_label(syslog::Level l) {
  switch (l) {
  case syslog::Level::DEBUG:
    return "\033[0;90mDBG ";
  case syslog::Level::INFO:
    return "\033[0;32mINFO";
  case syslog::Level::WARN:
    return "\033[0;33mWARN";
  case syslog::Level::ERROR:
    return "\033[0;31mERR ";
  }
  return "??? ";
}

void vlog(syslog::Level level, const char *subsystem, const char *fmt,
          __builtin_va_list args) {
  if (static_cast<u8>(level) < static_cast<u8>(current_level))
    return;

  buf_reset();

  // Timestamp: [SSSS.MMM]
  u64 secs, ms;
  get_uptime(secs, ms);
  buf_putc('[');
  // Right-align seconds in 4 chars
  if (secs < 10)
    buf_puts("   ");
  else if (secs < 100)
    buf_puts("  ");
  else if (secs < 1000)
    buf_putc(' ');
  buf_put_uint(secs);
  buf_putc('.');
  buf_pad3(ms);
  buf_puts("] ");

  // Level with color
  buf_puts(level_label(level));

  // Subsystem tag
  buf_puts(" [");
  buf_puts(subsystem);
  buf_puts("] ");

  // Formatted message
  const char *p = fmt;
  while (*p) {
    if (*p == '%' && *(p + 1)) {
      p++;
      switch (*p) {
      case 's':
        buf_puts(__builtin_va_arg(args, const char *));
        break;
      case 'd':
      case 'i':
        buf_put_int(static_cast<i64>(__builtin_va_arg(args, int)));
        break;
      case 'l':
        // %ld / %li
        if (*(p + 1) == 'd' || *(p + 1) == 'i') {
          p++;
          buf_put_int(__builtin_va_arg(args, i64));
        } else if (*(p + 1) == 'u') {
          p++;
          buf_put_uint(__builtin_va_arg(args, u64));
        } else if (*(p + 1) == 'x') {
          p++;
          buf_put_hex(__builtin_va_arg(args, u64));
        }
        break;
      case 'u':
        buf_put_uint(static_cast<u64>(__builtin_va_arg(args, unsigned int)));
        break;
      case 'x':
        buf_put_hex(static_cast<u64>(__builtin_va_arg(args, unsigned int)));
        break;
      case 'c':
        buf_putc(static_cast<char>(__builtin_va_arg(args, int)));
        break;
      case '%':
        buf_putc('%');
        break;
      default:
        buf_putc('%');
        buf_putc(*p);
        break;
      }
    } else {
      buf_putc(*p);
    }
    p++;
  }

  // Reset color + newline
  buf_puts("\033[0m\n");
  buf_flush();
}

} // anonymous namespace

namespace syslog {

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
