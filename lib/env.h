#pragma once

#include "types.h"

/*
 * OguzOS Environment Variables
 *
 * Simple key-value store for system environment variables.
 * Default vars: PATH, HOME, USER, HOSTNAME, SHELL
 */

namespace env {

constexpr i32 MAX_VARS = 32;
constexpr i32 MAX_KEY = 32;
constexpr i32 MAX_VALUE = 128;

// Initialize with default environment variables
void init();

// Set a variable (creates if not found, updates if exists)
void set(const char *key, const char *value);

// Get a variable value, returns nullptr if not found
const char *get(const char *key);

// Remove a variable, returns true if found and removed
bool unset(const char *key);

// Number of defined variables
i32 count();

// Access by index (for iteration)
const char *key_at(i32 index);
const char *value_at(i32 index);

// Resolve a command name to an app id by searching PATH directories.
// Returns the app id (e.g. "notepad.ogz") if found, nullptr otherwise.
const char *resolve_command(const char *name);

} // namespace env
