#pragma once

#include "types.h"

/*
 * OguzOS System Settings
 * Global settings store read by the GUI and modified by the Settings app.
 */

namespace settings {

// Timezone offset in minutes from UTC (e.g., +180 for UTC+3)
void set_tz_offset(i32 minutes);
i32 get_tz_offset();

// Desktop background color (XRGB8888)
void set_desktop_color(u32 color);
u32 get_desktop_color();

// Keyboard layout index
void set_kbd_layout(i32 index);
i32 get_kbd_layout();

} // namespace settings
