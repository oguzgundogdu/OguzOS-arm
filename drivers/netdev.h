#pragma once

#include "types.h"

/*
 * OguzOS Virtio Network Device Driver
 * Interfaces with QEMU's virtio-net via virtio-mmio transport.
 */

namespace netdev {

// Probe virtio-mmio bus and initialize the first network device found
bool init();

// Check if a network device was found and initialized
bool is_available();

// Get the device MAC address
void get_mac(u8 mac[6]);

// Send an Ethernet frame (raw frame, no virtio header needed)
bool send(const void *data, u32 len);

// Try to receive an Ethernet frame. Returns frame length, or -1 if none.
i32 recv(void *buf, u32 buf_size);

} // namespace netdev
