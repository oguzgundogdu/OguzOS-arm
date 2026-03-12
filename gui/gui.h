#pragma once

/*
 * OguzOS GUI System
 * Window manager, desktop, and file explorer.
 * Enter with gui::run(), exit with Escape key.
 */

namespace gui {

// Main GUI event loop. Takes over from the shell.
// Returns when user presses Escape.
void run();

} // namespace gui
