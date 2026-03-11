# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OguzOS is a minimal ARM64 (AArch64) operating system written in freestanding C++17 with no standard library. It targets QEMU's `virt` machine (cortex-a72) and UTM on macOS.

## Build Commands

```bash
make              # Build kernel → produces build/oguzos.bin
make run          # Build and run with QEMU (exit: Ctrl+A then X)
make debug        # Run with QEMU GDB server (-S -s) for debugging
make dump         # Disassemble the ELF
make clean        # Remove build/ directory
make distclean    # Also remove disk.img
```

**Toolchain:** Requires an AArch64 cross-compiler (`aarch64-elf-g++`, `aarch64-none-elf-g++`, or `aarch64-linux-gnu-g++`). The Makefile auto-detects which is available. Override with `CROSS=aarch64-linux-gnu- make`.

## Project Structure

```
arch/       — ARM64 bootstrap (boot.S) and linker script (linker.ld)
kernel/     — Kernel entry point (kernel_main)
drivers/    — Hardware drivers: PL011 UART, Virtio block, Virtio net
fs/         — In-memory hierarchical file system
net/        — Network protocol stack (ARP, IPv4, ICMP, UDP, DHCP)
shell/      — Interactive shell with 20+ commands
lib/        — Freestanding string/memory utilities and type aliases
scripts/    — QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/      — Generated object files and binaries (gitignored)
```

## Architecture

**Boot flow:** `arch/boot.S` → `kernel_main()` (kernel/kernel.cpp) → initializes UART, disk, net, FS → DHCP → launches shell loop (never returns).

**Key components:**

- **arch/boot.S** — Parks secondary CPUs (via MPIDR_EL1), sets up 64KB stack below 0x40080000, zeros BSS, jumps to `kernel_main`
- **arch/linker.ld** — Kernel loaded at 0x40080000; defines `.text`, `.rodata`, `.data`, `.bss` sections and `_bss_start`/`_bss_end` symbols
- **drivers/uart.cpp/h** — PL011 UART driver at 0x09000000 (console I/O, baud 115200)
- **drivers/disk.cpp/h** — Virtio block device driver probing virtio-mmio at 0x0a000000 (512-byte sector R/W)
- **drivers/net.cpp, netdev.h** — Virtio network device driver (device_id=1); RX/TX via virtqueues, 8 pre-posted RX buffers
- **net/net.cpp/h** — Minimal network stack: Ethernet framing, ARP resolution/table, IPv4 send/receive, ICMP echo (ping), UDP, DHCP client
- **fs/fs.cpp/h** — Max 128 nodes, 4KB per file; can persist to disk via `sync`
- **shell/shell.cpp/h** — History (16 entries), ANSI colors, quoted arg parsing
- **lib/string.cpp/h** — All memory/string operations (no libc available)
- **lib/types.h** — Fixed-width type aliases (u8–u64, i8–i64, usize)

**Naming note:** The driver header is `drivers/netdev.h` (namespace `netdev`) while the protocol stack header is `net/net.h` (namespace `net`) to avoid filename collision with `-I` flags.

## Constraints

- **Freestanding C++17**: no STL, no libc, no exceptions, no RTTI (`-ffreestanding -nostdlib -fno-exceptions -fno-rtti`)
- All memory/string operations must use custom implementations in lib/string.cpp
- Network byte order conversions needed for all protocol headers (ARM64 is little-endian)
- Hardware addresses are hardcoded for QEMU virt machine — do not change without updating QEMU args
- QEMU runtime: 128MB RAM, cortex-a72, virtio-blk for disk, virtio-net with user-mode networking
- The kernel is single-threaded; secondary CPUs are parked in boot.S
- QEMU user-mode networking: gateway 10.0.2.2, DHCP assigns 10.0.2.15, DNS at 10.0.2.3
