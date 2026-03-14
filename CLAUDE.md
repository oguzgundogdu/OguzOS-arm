# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OguzOS is a minimal ARM64 (AArch64) operating system written in freestanding C++17 with no standard library. It targets QEMU's `virt` machine (cortex-a72) and UTM on macOS.

## Build Commands

```bash
make              # Build kernel → produces build/oguzos.bin
make run          # Build and run with QEMU text-only (exit: Ctrl+A then X)
make gui          # Build and run with QEMU graphical mode (ramfb + mouse + keyboard)
make debug        # Run with QEMU GDB server (-S -s) for debugging
make dump         # Disassemble the ELF
make clean        # Remove build/ directory
make distclean    # Also remove disk.img
```

**Toolchain:** Requires an AArch64 cross-compiler (`aarch64-elf-g++`, `aarch64-none-elf-g++`, or `aarch64-linux-gnu-g++`). The Makefile auto-detects which is available. Override with `CROSS=aarch64-linux-gnu- make`.

## Project Structure

```
arch/       — ARM64 bootstrap (boot.S), exception vectors (exception.S), linker script
kernel/     — Kernel entry point (kernel_main)
drivers/    — Hardware drivers: PL011 UART, Virtio block/net, ramfb, virtio-tablet, virtio-keyboard
fs/         — In-memory hierarchical file system
net/        — Network protocol stack (ARP, IPv4, ICMP, UDP, DHCP)
gui/        — Window manager, desktop rendering, 8x8 bitmap font
apps/       — GUI applications (notepad, terminal, task manager, settings)
shell/      — Interactive shell with 20+ commands
lib/        — Freestanding string/memory utilities, syslog, settings store, type aliases
scripts/    — QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/      — Generated object files and binaries (gitignored)
```

## Architecture

**Boot flow:** `arch/boot.S` → `kernel_main()` (kernel/kernel.cpp) → initializes UART, disk, net, FS, syslog, settings → registers apps → DHCP → launches shell loop (never returns).

**Key subsystems:**

- **arch/boot.S** — Parks secondary CPUs (via MPIDR_EL1), sets up 64KB stack below 0x40080000, zeros BSS, jumps to `kernel_main`
- **arch/exception.S** — ARM64 exception vector table with try/catch crash recovery (`try_enter`/`try_leave`); used by the GUI to isolate app crashes without killing the OS
- **arch/linker.ld** — Kernel loaded at 0x40080000; defines `.text`, `.rodata`, `.data`, `.bss` sections and `__bss_start`/`__bss_end` symbols
- **drivers/** — PL011 UART (0x09000000), virtio-blk (disk), virtio-net, ramfb framebuffer (640x480 XRGB8888 at 0x46000000), virtio-tablet (mouse), virtio-keyboard (8 layouts)
- **gui/** — Window manager with up to 8 windows, desktop with start menu, file explorer; entered via `gui` shell command
- **apps/** — Pluggable GUI apps using OgzApp interface (see "Adding New Apps" below)
- **lib/syslog** — Dual-output logging (UART with ANSI colors + `/var/log/syslog` file); must init after `fs::init()`
- **lib/settings** — Key-value store persisted to `/etc/settings` (timezone, background color, keyboard layout)
- **net/** — Ethernet framing, ARP, IPv4, ICMP echo (ping), UDP, DHCP client
- **fs/** — Max 128 nodes, 4KB per file; can persist to disk via `sync`
- **shell/** — History (16 entries), ANSI colors, quoted arg parsing

## Adding New Source Files

Object files are listed explicitly in the Makefile (not auto-discovered). Some output names differ from source names to avoid basename collisions:

- `drivers/net.cpp` → `build/netdev.o` (namespace `netdev`, header: `drivers/netdev.h`)
- `net/net.cpp` → `build/netstack.o` (namespace `net`, header: `net/net.h`)
- `apps/settings.ogz.cpp` → `build/settingsapp.o` (avoids collision with `lib/settings.cpp` → `build/settings.o`)

When adding a new `.cpp` file: add a build rule in the Makefile following the existing pattern and append the new `.o` to the `OBJS` list.

## Adding New GUI Apps

Apps use the `.ogz.cpp` naming convention and follow a function-pointer interface defined in `apps/app.h`.

**Steps:**
1. Create `apps/myapp.ogz.cpp` implementing the `OgzApp` struct callbacks
2. Add Makefile rule: `$(BUILD_DIR)/myapp.o: $(APPS_DIR)/myapp.ogz.cpp | $(BUILD_DIR)` and add to `OBJS`
3. In `kernel/kernel.cpp`: declare `namespace apps { void register_myapp(); }` and call it in `kernel_main()` before the shell loop

**OgzApp callbacks:** `on_open`, `on_draw`, `on_key`, `on_arrow`, `on_close` are required; `on_click`, `on_scroll`, `on_mouse_down`, `on_mouse_move` can be `nullptr`. Each app window gets a 4096-byte `app_state` buffer — use `static_assert(sizeof(MyState) <= 4096)` to enforce. The app auto-appears in the start menu via the registry.

**Coordinate convention:** App callbacks receive content area coordinates (cx, cy, cw, ch) excluding the 24px title bar. Mouse click/move positions are relative to content area origin. Scroll delta: positive = up, negative = down.

## Constraints

- **Freestanding C++17**: no STL, no libc, no exceptions, no RTTI (`-ffreestanding -nostdlib -fno-exceptions -fno-rtti`)
- All memory/string operations must use custom implementations in `lib/string.cpp`
- `kernel_main` must be declared `extern "C"` — called from assembly. Three C++ ABI stubs (`__cxa_pure_virtual`, `__cxa_atexit`, `__dso_handle`) are provided in kernel.cpp
- Network byte order conversions needed for all protocol headers (ARM64 is little-endian)
- Hardware addresses are hardcoded for QEMU virt machine — do not change without updating QEMU args
- QEMU runtime: 512MB RAM, cortex-a72, virtio-blk for disk, virtio-net with user-mode networking
- The kernel is single-threaded; secondary CPUs are parked in boot.S
- PSCI calls used for halt/reboot; ARM generic timer used for uptime
- QEMU user-mode networking: gateway 10.0.2.2, DHCP assigns 10.0.2.15, DNS at 10.0.2.3
- App state limited to 4096 bytes per window instance; max 8 simultaneous windows
- `syslog::init()` must be called after `fs::init()` to enable file logging
