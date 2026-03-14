#pragma once

#include "types.h"

/*
 * OguzOS GUI System
 * Window manager, desktop, and file explorer.
 * Enter with gui::run(), exit with Escape key.
 */

namespace gui {

// Main GUI event loop. Takes over from the shell.
// Returns when user presses Escape.
void run();

// Window type IDs for task manager
constexpr i32 WTYPE_EXPLORER = 0;
constexpr i32 WTYPE_TEXTVIEW = 1;
constexpr i32 WTYPE_APP = 2;

// Open a .ogz app by its id (e.g. "notepad.ogz")
void open_app(const char *app_id);

// Query open windows for task manager
i32 get_window_count();
const char *get_window_title(i32 index);
bool is_window_active(i32 index);
i32 get_window_type(i32 index);
const char *get_window_app_id(i32 index);

} // namespace gui
