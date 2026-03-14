#include "uart.h"

namespace {

// PL011 UART registers (QEMU virt machine)
constexpr u64 UART_BASE = 0x09000000;

volatile u32 *reg(u64 offset) {
  return reinterpret_cast<volatile u32 *>(UART_BASE + offset);
}

constexpr u64 UARTDR = 0x000;   // Data register
constexpr u64 UARTFR = 0x018;   // Flag register
constexpr u64 UARTIBRD = 0x024; // Integer baud rate
constexpr u64 UARTFBRD = 0x028; // Fractional baud rate
constexpr u64 UARTLCR = 0x02C;  // Line control register
constexpr u64 UARTCR = 0x030;   // Control register
constexpr u64 UARTIMSC = 0x038; // Interrupt mask

constexpr u32 FR_TXFF = (1 << 5); // TX FIFO full
constexpr u32 FR_RXFE = (1 << 4); // RX FIFO empty

// Output capture state
char *cap_buf = nullptr;
usize cap_size = 0;
usize cap_len = 0;

} // anonymous namespace

namespace uart {

void init() {
  // Disable UART
  *reg(UARTCR) = 0;

  // Set baud rate (115200 with 24MHz clock)
  // Divisor = 24000000 / (16 * 115200) = 13.0208
  *reg(UARTIBRD) = 13;
  *reg(UARTFBRD) = 1;

  // 8 bits, FIFO enabled, no parity, 1 stop bit
  *reg(UARTLCR) = (1 << 4) | (3 << 5); // FIFO enable | 8-bit word

  // Enable UART, TX, RX
  *reg(UARTCR) = (1 << 0) | (1 << 8) | (1 << 9);

  // Mask all interrupts
  *reg(UARTIMSC) = 0;
}

void putc(char c) {
  // Wait until TX FIFO is not full
  while (*reg(UARTFR) & FR_TXFF) {
  }
  *reg(UARTDR) = static_cast<u32>(c);

  // Capture to buffer if active (skip \r for terminal display)
  if (cap_buf && c != '\r' && cap_len < cap_size - 1) {
    cap_buf[cap_len++] = c;
    cap_buf[cap_len] = '\0';
  }
}

char getc() {
  // Wait until RX FIFO is not empty
  while (*reg(UARTFR) & FR_RXFE) {
  }
  return static_cast<char>(*reg(UARTDR) & 0xFF);
}

void puts(const char *str) {
  while (*str) {
    if (*str == '\n') {
      putc('\r');
    }
    putc(*str++);
  }
}

void put_hex(u64 value) {
  puts("0x");
  const char *hex = "0123456789ABCDEF";
  bool leading = true;
  for (int i = 60; i >= 0; i -= 4) {
    u8 nibble = (value >> i) & 0xF;
    if (nibble != 0)
      leading = false;
    if (!leading || i == 0) {
      putc(hex[nibble]);
    }
  }
}

void put_int(i64 value) {
  if (value < 0) {
    putc('-');
    value = -value;
  }
  if (value == 0) {
    putc('0');
    return;
  }
  char buf[20];
  int i = 0;
  while (value > 0) {
    buf[i++] = '0' + (value % 10);
    value /= 10;
  }
  while (--i >= 0) {
    putc(buf[i]);
  }
}

void capture_start(char *buf, usize buf_size) {
  cap_buf = buf;
  cap_size = buf_size;
  cap_len = 0;
  if (buf)
    buf[0] = '\0';
}

void capture_stop() {
  cap_buf = nullptr;
  cap_size = 0;
  cap_len = 0;
}

bool try_getc(char &c) {
  if (*reg(UARTFR) & FR_RXFE)
    return false;
  c = static_cast<char>(*reg(UARTDR) & 0xFF);
  return true;
}

} // namespace uart
