#pragma once

#include "types.h"

/*
 * OguzOS Keyboard Driver
 * Uses virtio-input (virtio-keyboard-device) for key input from QEMU GUI.
 */

namespace keyboard {

// Probe virtio-mmio bus for a virtio-input keyboard device
bool init();

// Check if keyboard device is available
bool is_available();

// Process pending virtio events (call once per frame)
void poll();

// Get next buffered key press as ASCII (returns false if empty)
bool get_key(char &key);

// Get next buffered arrow key: 'A'=up, 'B'=down, 'C'=right, 'D'=left
bool get_arrow(char &dir);

} // namespace keyboard
