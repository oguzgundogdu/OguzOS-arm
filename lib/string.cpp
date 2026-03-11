#include "string.h"

namespace str {

usize len(const char *s) {
  usize n = 0;
  while (s[n])
    n++;
  return n;
}

int cmp(const char *a, const char *b) {
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return static_cast<u8>(*a) - static_cast<u8>(*b);
}

int ncmp(const char *a, const char *b, usize n) {
  for (usize i = 0; i < n; i++) {
    if (a[i] != b[i])
      return static_cast<u8>(a[i]) - static_cast<u8>(b[i]);
    if (a[i] == '\0')
      return 0;
  }
  return 0;
}

void cpy(char *dst, const char *src) {
  while (*src)
    *dst++ = *src++;
  *dst = '\0';
}

void ncpy(char *dst, const char *src, usize n) {
  usize i;
  for (i = 0; i < n - 1 && src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

void cat(char *dst, const char *src) {
  while (*dst)
    dst++;
  while (*src)
    *dst++ = *src++;
  *dst = '\0';
}

bool starts_with(const char *s, const char *prefix) {
  while (*prefix) {
    if (*s != *prefix)
      return false;
    s++;
    prefix++;
  }
  return true;
}

const char *find_char(const char *s, char c) {
  while (*s) {
    if (*s == c)
      return s;
    s++;
  }
  return nullptr;
}

bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void *memset(void *ptr, int value, usize size) {
  auto *p = static_cast<u8 *>(ptr);
  for (usize i = 0; i < size; i++)
    p[i] = static_cast<u8>(value);
  return ptr;
}

void *memcpy(void *dst, const void *src, usize size) {
  auto *d = static_cast<u8 *>(dst);
  auto *s = static_cast<const u8 *>(src);
  for (usize i = 0; i < size; i++)
    d[i] = s[i];
  return dst;
}

int memcmp(const void *a, const void *b, usize size) {
  auto *pa = static_cast<const u8 *>(a);
  auto *pb = static_cast<const u8 *>(b);
  for (usize i = 0; i < size; i++) {
    if (pa[i] != pb[i])
      return pa[i] - pb[i];
  }
  return 0;
}

} // namespace str

// Required by the compiler for freestanding C++
extern "C" void *memset(void *ptr, int value, unsigned long size) {
  return str::memset(ptr, value, size);
}

extern "C" void *memcpy(void *dst, const void *src, unsigned long size) {
  return str::memcpy(dst, src, size);
}
