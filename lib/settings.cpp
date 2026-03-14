#include "settings.h"

namespace {

i32 tz_offset = 0;               // UTC
u32 desktop_color = 0x00336699;   // Default blue
i32 kbd_layout = 0;              // English (US)

} // anonymous namespace

namespace settings {

void set_tz_offset(i32 m) { tz_offset = m; }
i32 get_tz_offset() { return tz_offset; }

void set_desktop_color(u32 c) { desktop_color = c; }
u32 get_desktop_color() { return desktop_color; }

void set_kbd_layout(i32 i) { kbd_layout = i; }
i32 get_kbd_layout() { return kbd_layout; }

} // namespace settings
