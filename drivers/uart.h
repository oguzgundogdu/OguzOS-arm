#pragma once

#include "types.h"

/*
 * PL011 UART driver for QEMU virt machine
 * Base address: 0x09000000
 */

namespace uart {

void init();
void putc(char c);
char getc();
void puts(const char *str);
void put_hex(u64 value);
void put_int(i64 value);

// Non-blocking: returns true and sets c if data available
bool try_getc(char &c);

// Output capture: when set, putc also writes to this buffer.
// Set buf=nullptr to stop capturing. Not re-entrant.
void capture_start(char *buf, usize buf_size);
void capture_stop();

} // namespace uart
