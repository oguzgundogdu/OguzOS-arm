#include "clipboard.h"
#include "string.h"

namespace {

constexpr i32 CLIP_BUF_SIZE = 2048;
char clip_buf[CLIP_BUF_SIZE];
bool clip_has_data = false;

} // anonymous namespace

namespace clipboard {

void copy(const char *text) {
  if (!text) { clear(); return; }
  str::ncpy(clip_buf, text, CLIP_BUF_SIZE);
  clip_has_data = true;
}

const char *paste() {
  return clip_has_data ? clip_buf : "";
}

bool has_data() {
  return clip_has_data;
}

void clear() {
  clip_buf[0] = '\0';
  clip_has_data = false;
}

} // namespace clipboard
