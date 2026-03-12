#pragma once

#include "types.h"

/*
 * OgzApp — OguzOS Application Interface
 *
 * Each .ogz app implements this interface. Apps are compiled into the kernel
 * and registered at startup. The GUI window manager calls these callbacks
 * to let apps draw, handle input, and manage their own state.
 *
 * app_state: 512-byte scratch area owned by each app instance.
 * Apps cast this to their own state struct.
 */

struct OgzApp {
  const char *name;   // Display name (e.g. "Notepad")
  const char *id;     // Unique ID    (e.g. "notepad.ogz")
  i32 default_w;      // Default window width
  i32 default_h;      // Default window height

  // Called when app window is created. Initialize app_state here.
  void (*on_open)(u8 *app_state);

  // Called every frame to draw app content.
  // cx,cy = content area origin; cw,ch = content area size.
  void (*on_draw)(u8 *app_state, i32 cx, i32 cy, i32 cw, i32 ch);

  // Called when a keyboard key is received while this app is focused.
  // Returns true if the app consumed the key.
  bool (*on_key)(u8 *app_state, char key);

  // Called when an arrow key is pressed: 'A'=up, 'B'=down, 'C'=right, 'D'=left.
  void (*on_arrow)(u8 *app_state, char direction);

  // Called when the app window is closed. Clean up here.
  void (*on_close)(u8 *app_state);
};
