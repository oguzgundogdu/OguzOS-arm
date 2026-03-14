#pragma once

#include "types.h"

/*
 * OguzOS Start Menu Configuration
 *
 * Ordered list of menu entries. Each entry is either:
 *   - An app launch  (type=APP, id="notepad.ogz")
 *   - A shortcut      (type=SHORTCUT, label + path)
 *   - A separator      (type=SEP)
 *   - A built-in       (type=EXPLORER / ABOUT / SHUTDOWN)
 *
 * Persisted to /etc/menu. Configurable via shell or settings.
 */

namespace menu {

constexpr i32 MAX_ENTRIES = 24;
constexpr i32 MAX_LABEL = 32;
constexpr i32 MAX_ID = 32;

enum EntryType : i32 {
  ENTRY_APP = 0,      // launch .ogz app by id
  ENTRY_SHORTCUT = 1, // open a file/folder path
  ENTRY_SEP = 2,      // separator line
  ENTRY_EXPLORER = 3, // built-in: File Explorer
  ENTRY_ABOUT = 4,    // built-in: About
  ENTRY_SHUTDOWN = 5, // built-in: Shutdown
  ENTRY_COMMAND = 6,  // run a shell command in Terminal
  ENTRY_RESTART = 7,  // built-in: Restart
};

struct Entry {
  EntryType type;
  char label[MAX_LABEL]; // display name
  char id[MAX_ID];       // app id or path (depending on type)
};

// Initialize with empty menu
void init();

// Add an entry at the end. Returns index or -1 if full.
i32 add(EntryType type, const char *label, const char *id);

// Insert at position, shifting others down. Returns true on success.
bool insert(i32 pos, EntryType type, const char *label, const char *id);

// Remove entry at index
bool remove(i32 index);

// Move entry from one position to another
bool move(i32 from, i32 to);

// Number of entries
i32 count();

// Access by index
const Entry *get(i32 index);

// Find first entry with matching id, returns index or -1
i32 find(const char *id);

// Check if an app id is in the menu
bool has_app(const char *app_id);

// Persist to /etc/menu and load back
void save();
void load();

} // namespace menu
