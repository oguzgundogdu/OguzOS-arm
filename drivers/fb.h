#pragma once

#include "types.h"

/*
 * OguzOS Framebuffer Driver
 * Uses QEMU ramfb device via fw_cfg interface.
 * Provides a 640x480 32-bit (XRGB8888) linear framebuffer.
 */

namespace fb {

// Initialize ramfb framebuffer via fw_cfg
bool init();

// Check if framebuffer is available
bool is_available();

// Get pointer to pixel buffer (u32 per pixel, XRGB8888)
u32 *buffer();

// Screen dimensions
u32 width();
u32 height();

} // namespace fb
