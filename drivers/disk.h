#pragma once

#include "types.h"

/*
 * OguzOS Virtio Block Device Driver
 * Interfaces with QEMU's virtio-blk via virtio-mmio transport.
 */

namespace disk {

// Probe virtio-mmio bus and initialize the first block device found
bool init();

// Read a single 512-byte sector
bool read_sector(u64 sector, void *buf);

// Write a single 512-byte sector
bool write_sector(u64 sector, const void *buf);

// Read multiple consecutive sectors
bool read_sectors(u64 start, u32 count, void *buf);

// Write multiple consecutive sectors
bool write_sectors(u64 start, u32 count, const void *buf);

// Get disk capacity in 512-byte sectors
u64 get_capacity();

// Check if a disk was found and initialized
bool is_available();

} // namespace disk
