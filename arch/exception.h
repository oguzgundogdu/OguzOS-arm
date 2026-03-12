#pragma once

/*
 * OguzOS Exception Handling
 *
 * Provides crash recovery for app callbacks. When an app causes a
 * synchronous exception (bad pointer, alignment fault, etc.), the
 * exception handler restores state to the try_enter() call point
 * instead of crashing the whole OS.
 *
 * Usage:
 *   if (try_enter() == 0) {
 *       // Normal path — call untrusted code
 *       app->on_draw(...);
 *       try_leave();
 *   } else {
 *       // Recovery path — app crashed, clean up
 *   }
 */

extern "C" {

// Save recovery point. Returns 0 normally, 1 on crash recovery.
int try_enter();

// Clear recovery point (call after untrusted code returns safely).
void try_leave();

}
