#include "settings.h"
#include "fs.h"
#include "string.h"
#include "syslog.h"

namespace {

i32 tz_offset = 0;               // UTC
u32 desktop_color = 0x00336699;   // Default blue

#ifndef DEFAULT_KBD
#define DEFAULT_KBD 0
#endif
#ifndef DEFAULT_RES_W
#define DEFAULT_RES_W 1920
#endif
#ifndef DEFAULT_RES_H
#define DEFAULT_RES_H 1080
#endif

i32 kbd_layout = DEFAULT_KBD;
u32 res_w = DEFAULT_RES_W;
u32 res_h = DEFAULT_RES_H;

// Simple integer-to-string helper
void i32_to_str(i32 val, char *buf) {
  if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
  char tmp[16];
  i32 i = 0;
  bool neg = val < 0;
  if (neg) val = -val;
  while (val > 0) { tmp[i++] = '0' + static_cast<char>(val % 10); val /= 10; }
  i32 j = 0;
  if (neg) buf[j++] = '-';
  while (i > 0) buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// Simple string-to-integer
i32 str_to_i32(const char *s) {
  bool neg = false;
  if (*s == '-') { neg = true; s++; }
  i32 val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return neg ? -val : val;
}

// Parse "key=value\n" from text, return value as i32 or default
i32 parse_int(const char *text, const char *key, i32 def) {
  usize klen = str::len(key);
  const char *p = text;
  while (*p) {
    // Check if line starts with key=
    bool match = true;
    for (usize i = 0; i < klen; i++) {
      if (p[i] != key[i]) { match = false; break; }
    }
    if (match && p[klen] == '=') {
      return str_to_i32(p + klen + 1);
    }
    // Skip to next line
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
  }
  return def;
}

// Parse u32 value (stored as decimal)
u32 parse_u32(const char *text, const char *key, u32 def) {
  i32 v = parse_int(text, key, -1);
  return v >= 0 ? static_cast<u32>(v) : def;
}

} // anonymous namespace

namespace settings {

void set_tz_offset(i32 m) { tz_offset = m; }
i32 get_tz_offset() { return tz_offset; }

void set_desktop_color(u32 c) { desktop_color = c; }
u32 get_desktop_color() { return desktop_color; }

void set_kbd_layout(i32 i) { kbd_layout = i; }
i32 get_kbd_layout() { return kbd_layout; }

void set_resolution(u32 w, u32 h) { res_w = w; res_h = h; }
u32 get_res_w() { return res_w; }
u32 get_res_h() { return res_h; }

void save() {
  // Ensure /etc exists
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/");
  fs::mkdir("etc");  // ok if already exists
  fs::cd("/etc");
  fs::touch("settings");  // ok if already exists

  // Build settings file content
  char buf[384];
  buf[0] = '\0';

  str::cat(buf, "tz=");
  char tmp[16];
  i32_to_str(tz_offset, tmp);
  str::cat(buf, tmp);
  str::cat(buf, "\n");

  str::cat(buf, "bg=");
  i32_to_str(static_cast<i32>(desktop_color), tmp);
  str::cat(buf, tmp);
  str::cat(buf, "\n");

  str::cat(buf, "kb=");
  i32_to_str(kbd_layout, tmp);
  str::cat(buf, tmp);
  str::cat(buf, "\n");

  str::cat(buf, "rw=");
  i32_to_str(static_cast<i32>(res_w), tmp);
  str::cat(buf, tmp);
  str::cat(buf, "\n");

  str::cat(buf, "rh=");
  i32_to_str(static_cast<i32>(res_h), tmp);
  str::cat(buf, tmp);
  str::cat(buf, "\n");

  fs::write("settings", buf);

  // Persist filesystem to disk so settings survive reboot
  fs::sync_to_disk();
  syslog::info("settings", "saved to /etc/settings");

  // Restore cwd
  fs::cd(old_cwd);
}

void load() {
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/etc");
  const char *data = fs::cat("settings");
  if (data && data[0] != '\0') {
    tz_offset = parse_int(data, "tz", 0);
    desktop_color = parse_u32(data, "bg", 0x00336699);
    kbd_layout = parse_int(data, "kb", 0);
    res_w = parse_u32(data, "rw", 1920);
    res_h = parse_u32(data, "rh", 1080);
    syslog::info("settings", "loaded: tz=%d bg=0x%x kb=%d res=%dx%d",
                  tz_offset, desktop_color, kbd_layout, res_w, res_h);
  } else {
    syslog::info("settings", "no saved settings found, using defaults");
  }

  fs::cd(old_cwd);
}

} // namespace settings
