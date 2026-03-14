#pragma once

#include "types.h"

/*
 * OguzOS Network Stack
 * Minimal implementation: Ethernet, ARP, IPv4, ICMP, UDP, DHCP
 */

namespace net {

// Initialize network stack and run DHCP
bool init();

// Check if network is configured (has IP address)
bool is_available();

// Shell commands
void ifconfig();
void ping(const char *target, u32 count);
void dhcp();
void curl(const char *url);

// NTP time sync — returns true if time was set
bool ntp_sync();

// Get current wall-clock UTC timestamp (seconds since 1970-01-01)
// Returns 0 if time not synced yet
u64 get_epoch();

} // namespace net
