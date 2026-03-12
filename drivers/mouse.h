#pragma once

#include "types.h"

/*
 * OguzOS Mouse Driver
 * Uses virtio-input (virtio-tablet-device) for absolute positioning.
 */

namespace mouse {

// Probe virtio-mmio bus for a virtio-input tablet device
bool init();

// Check if mouse/tablet device is available
bool is_available();

// Poll for mouse events. Updates position and button state.
// Returns true if any new events were processed.
bool poll(i32 &x, i32 &y, bool &left, bool &right);

} // namespace mouse
