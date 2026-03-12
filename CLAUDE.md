# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OguzOS is a minimal ARM64 (AArch64) operating system written in freestanding C++17 with no standard library. It targets QEMU's `virt` machine (cortex-a72) and UTM on macOS.

## Build Commands

```bash
make              # Build kernel → produces build/oguzos.bin
make run          # Build and run with QEMU text-only (exit: Ctrl+A then X)
make gui          # Build and run with QEMU graphical mode (ramfb + mouse)
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
drivers/    — Hardware drivers: PL011 UART, Virtio block/net, ramfb, virtio-tablet
fs/         — In-memory hierarchical file system
net/        — Network protocol stack (ARP, IPv4, ICMP, UDP, DHCP)
gui/        — Graphical desktop: window manager, file explorer, 8x8 bitmap font
shell/      — Interactive shell with 20+ commands
lib/        — Freestanding string/memory utilities and type aliases
scripts/    — QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/      — Generated object files and binaries (gitignored)
```

## Architecture

**Boot flow:** `arch/boot.S` → `kernel_main()` (kernel/kernel.cpp) → initializes UART, disk, net, FS → DHCP → launches shell loop (never returns).

**Key components:**

- **arch/boot.S** — Parks secondary CPUs (via MPIDR_EL1), sets up 64KB stack below 0x40080000, zeros BSS, jumps to `kernel_main`
- **arch/linker.ld** — Kernel loaded at 0x40080000; defines `.text`, `.rodata`, `.data`, `.bss` sections and `__bss_start`/`__bss_end` symbols
- **drivers/uart.cpp/h** — PL011 UART driver at 0x09000000 (console I/O, baud 115200)
- **drivers/disk.cpp/h** — Virtio block device driver probing virtio-mmio at 0x0a000000 (512-byte sector R/W)
- **drivers/net.cpp, netdev.h** — Virtio network device driver (device_id=1); RX/TX via virtqueues, 8 pre-posted RX buffers
- **drivers/fb.cpp/h** — ramfb framebuffer via fw_cfg (0x09020000); 640x480 XRGB8888 at 0x46000000
- **drivers/mouse.cpp/h** — Virtio-input tablet driver (device_id=18); absolute positioning mapped to screen coords
- **gui/gui.cpp/h** — Window manager, desktop, file explorer; entered via `gui` shell command, exit with Escape
- **gui/graphics.cpp/h** — Pixel, rect, text drawing primitives on the framebuffer
- **gui/font.h** — Embedded 8x8 bitmap font covering printable ASCII (32-126)
- **net/net.cpp/h** — Minimal network stack: Ethernet framing, ARP resolution/table, IPv4 send/receive, ICMP echo (ping), UDP, DHCP client
- **fs/fs.cpp/h** — Max 128 nodes, 4KB per file; can persist to disk via `sync`
- **shell/shell.cpp/h** — History (16 entries), ANSI colors, quoted arg parsing
- **lib/string.cpp/h** — All memory/string operations (no libc available)
- **lib/types.h** — Fixed-width type aliases (u8–u64, i8–i64, usize)

**Naming note:** The driver header is `drivers/netdev.h` (namespace `netdev`) while the protocol stack header is `net/net.h` (namespace `net`) to avoid filename collision with `-I` flags.

## Adding New Source Files

Object files are listed explicitly in the Makefile (not auto-discovered). Some output names differ from source names to avoid basename collisions:

- `drivers/net.cpp` → `build/netdev.o`
- `net/net.cpp` → `build/netstack.o`

When adding a new `.cpp` file: add a build rule in the Makefile following the existing pattern and append the new `.o` to the `OBJS` list.

## Constraints

- **Freestanding C++17**: no STL, no libc, no exceptions, no RTTI (`-ffreestanding -nostdlib -fno-exceptions -fno-rtti`)
- All memory/string operations must use custom implementations in lib/string.cpp
- `kernel_main` must be declared `extern "C"` — called from assembly. Three C++ ABI stubs (`__cxa_pure_virtual`, `__cxa_atexit`, `__dso_handle`) are provided in kernel.cpp
- Network byte order conversions needed for all protocol headers (ARM64 is little-endian)
- Hardware addresses are hardcoded for QEMU virt machine — do not change without updating QEMU args
- QEMU runtime: 128MB RAM, cortex-a72, virtio-blk for disk, virtio-net with user-mode networking
- The kernel is single-threaded; secondary CPUs are parked in boot.S
- PSCI calls used for halt/reboot; ARM generic timer used for uptime
- QEMU user-mode networking: gateway 10.0.2.2, DHCP assigns 10.0.2.15, DNS at 10.0.2.3
