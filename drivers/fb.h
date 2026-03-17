#pragma once

#include "types.h"

/*
 * OguzOS Framebuffer Driver
 * Uses QEMU ramfb device via fw_cfg interface.
 * Provides a 32-bit (XRGB8888) linear framebuffer with runtime resolution.
 */

namespace fb {

// Initialize ramfb framebuffer via fw_cfg
bool init();

// Change resolution at runtime (re-configures ramfb via DMA)
bool set_resolution(u32 w, u32 h);

// Check if framebuffer is available
bool is_available();

// Get pointer to pixel buffer (u32 per pixel, XRGB8888)
u32 *buffer();

// Screen dimensions
u32 width();
u32 height();

} // namespace fb
