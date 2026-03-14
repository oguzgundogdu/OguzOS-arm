#pragma once

#include "types.h"

/*
 * OguzOS File Associations
 *
 * Maps file extensions to app IDs (e.g. ".txt" -> "notepad.ogz").
 * Persisted to /etc/filetypes. Configurable via shell or settings.
 */

namespace assoc {

constexpr i32 MAX_ASSOCS = 32;
constexpr i32 MAX_EXT = 16;   // e.g. ".txt"
constexpr i32 MAX_APP_ID = 32; // e.g. "notepad.ogz"

// Initialize with empty table
void init();

// Set an association (creates or updates)
void set(const char *ext, const char *app_id);

// Get app id for extension, returns nullptr if not found.
// ext should include the dot (e.g. ".txt").
const char *get(const char *ext);

// Find app id for a filename by matching its extension.
// Returns nullptr if no association found.
const char *find_for_file(const char *filename);

// Remove an association, returns true if found
bool unset(const char *ext);

// Number of associations
i32 count();

// Access by index for iteration
const char *ext_at(i32 index);
const char *app_at(i32 index);

// Persist to /etc/filetypes and load back
void save();
void load();

} // namespace assoc
