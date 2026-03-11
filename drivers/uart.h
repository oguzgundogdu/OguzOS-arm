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

} // namespace uart
