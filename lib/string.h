#pragma once

#include "types.h"

namespace str {

usize len(const char *s);
int cmp(const char *a, const char *b);
int ncmp(const char *a, const char *b, usize n);
void cpy(char *dst, const char *src);
void ncpy(char *dst, const char *src, usize n);
void cat(char *dst, const char *src);
bool starts_with(const char *str, const char *prefix);
const char *find_char(const char *s, char c);
bool is_whitespace(char c);

// Simple memory operations (needed since we have no libc)
void *memset(void *ptr, int value, usize size);
void *memcpy(void *dst, const void *src, usize size);
int memcmp(const void *a, const void *b, usize size);

// Printf-style formatting into buffer. Returns chars written (excluding NUL).
// Supports: %s, %d, %u, %x, %c, %%
i32 format(char *buf, i32 size, const char *fmt, ...);

} // namespace str
