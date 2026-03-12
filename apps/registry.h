#pragma once

#include "app.h"
#include "types.h"

/*
 * OgzApp Registry
 *
 * Central registry where .ogz apps register themselves.
 * The GUI and start menu query this to discover available apps.
 */

namespace apps {

constexpr i32 MAX_APPS = 16;

// Register an app (called at init time by each app module)
void register_app(const OgzApp *app);

// Get number of registered apps
i32 count();

// Get app by index (0..count-1), returns nullptr if invalid
const OgzApp *get(i32 index);

// Find app by id (e.g. "notepad.ogz"), returns nullptr if not found
const OgzApp *find(const char *id);

} // namespace apps
