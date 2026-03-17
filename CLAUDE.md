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
make distclean    # Also remove disk.img (forces fresh filesystem on next boot)
```

**Toolchain:** Requires an AArch64 cross-compiler (`aarch64-elf-g++`, `aarch64-none-elf-g++`, or `aarch64-linux-gnu-g++`). The Makefile auto-detects which is available. Override with `CROSS=aarch64-linux-gnu- make`.

**Important:** After changing filesystem layout or config file formats, run `make distclean` to delete the old `disk.img` — otherwise `load_from_disk()` restores the stale image and skips `fs::init()`.

## Project Structure

```
arch/       — ARM64 bootstrap (boot.S), exception vectors (exception.S), linker script
kernel/     — Kernel entry point (kernel_main)
drivers/    — PL011 UART, Virtio block/net, ramfb, virtio-tablet, virtio-keyboard
fs/         — In-memory hierarchical file system (128 nodes, 4KB/file)
net/        — Network stack (ARP, IPv4, ICMP, UDP, DHCP, DNS, HTTP, NTP)
gui/        — Window manager, desktop, start menu, file explorer
apps/       — GUI applications (.ogz.cpp files): notepad, terminal, task manager, settings, browser, C# IDE, C# GUI host
shell/      — UART shell, shared command library (commands.cpp)
lib/        — String/memory utils, syslog, settings, env vars, file assoc, menu config
lang/       — Mini C# interpreter (console + GUI modes) with widget system
scripts/    — QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/      — Generated object files and binaries (gitignored)
```

## Architecture

### Boot Flow

`arch/boot.S` → `kernel_main()` (kernel/kernel.cpp):
1. UART, disk, framebuffer, mouse, keyboard init
2. Network init (virtio-net + DHCP + NTP)
3. Filesystem: load from disk or `fs::init()` (creates `/bin`, `/home`, `/etc`, `/tmp`, `/var`)
4. `settings::load()`, `env::init()`, `assoc::init()`+load, `syslog::init()`
5. Register apps → populate `/bin` with app descriptors → build `/etc/menu` defaults
6. `menu::init()`+load
7. If framebuffer available → `gui::run()`, else → `shell::run()`

### Two Shells — Shared Command Library

The OS has two independent command dispatchers:
- **UART shell** (`shell/shell.cpp` `execute()`) — runs in text-only mode or as serial fallback
- **GUI terminal** (`apps/terminal.ogz.cpp` `term_exec()`) — runs as a windowed app inside the GUI

Both call the same implementations in `shell/commands.cpp` via a callback-based output abstraction:
```cpp
using OutFn = void (*)(void *ctx, const char *text);
// UART: uart_out(nullptr, text) → uart::puts(text)
// Terminal: term_out(state, text) → term_append(state, text)
```

**When adding a new command:** implement it in `commands.cpp`/`commands.h`, then add dispatch entries to BOTH `shell.cpp` AND `terminal.ogz.cpp`. The terminal also needs `uart::capture_start/stop` for commands that print directly to UART (e.g., `net::ping()`).

### Configuration Subsystems

Three runtime-configurable subsystems persist to `/etc/` and auto-save to disk:

| System | File | Header | Purpose |
|--------|------|--------|---------|
| `settings::` | `/etc/settings` | `lib/settings.h` | Timezone, desktop color, keyboard layout |
| `assoc::` | `/etc/filetypes` | `lib/assoc.h` | File extension → app ID mapping |
| `menu::` | `/etc/menu` | `lib/menu.h` | Start menu entries (apps, shortcuts, commands, built-ins) |

All three call `fs::sync_to_disk()` on save. `env::` (environment variables) is in-memory only.

### GUI File Opening Flow

When a file is double-clicked in the explorer:
1. `explorer_activate()` builds the absolute path
2. `gui::open_file(path, content)` checks:
   - `.ogz` extension → `gui::open_app(name)` (launch the app)
   - `assoc::find_for_file(name)` → if app has `on_open_file` callback, open app with file content
   - Fallback → `open_text_viewer(name, content)`

### Start Menu

The start menu is data-driven from `menu::` config (not auto-discovered from the app registry). Entry types: `ENTRY_APP`, `ENTRY_SHORTCUT`, `ENTRY_COMMAND`, `ENTRY_SEP`, `ENTRY_EXPLORER`, `ENTRY_ABOUT`, `ENTRY_SHUTDOWN`, `ENTRY_RESTART`. The GUI calls `build_menu()` at startup to populate the display from `menu::` entries.

## Adding New Source Files

Object files are listed explicitly in the Makefile `OBJS` list (not auto-discovered). Some output names differ from source names to avoid basename collisions:

- `drivers/net.cpp` → `build/netdev.o` (namespace `netdev`, header: `drivers/netdev.h`)
- `net/net.cpp` → `build/netstack.o` (namespace `net`, header: `net/net.h`)
- `apps/settings.ogz.cpp` → `build/settingsapp.o` (avoids collision with `lib/settings.cpp` → `build/settings.o`)
- `lang/csharp.cpp` → `build/csharp_interp.o` (namespace `csharp`, header: `lang/csharp.h`)
- `apps/csharp.ogz.cpp` → `build/csharp_ide.o` (avoids collision with `lang/csharp.cpp`)

When adding a new `.cpp` file: add a build rule in the Makefile following the existing pattern and append the new `.o` to the `OBJS` list.

## Adding New GUI Apps

Apps use the `.ogz.cpp` naming convention and follow a function-pointer interface defined in `apps/app.h`.

**Steps:**
1. Create `apps/myapp.ogz.cpp` implementing the `OgzApp` struct callbacks
2. Add Makefile rule: `$(BUILD_DIR)/myapp.o: $(APPS_DIR)/myapp.ogz.cpp | $(BUILD_DIR)` and add to `OBJS`
3. In `kernel/kernel.cpp`: declare `namespace apps { void register_myapp(); }` and call it in `kernel_main()` before the shell loop
4. The app auto-gets a `/bin/myapp.ogz` entry and appears in the default start menu

**App registry** holds up to 16 apps (`MAX_APPS`). Registration calls `apps::register_app(&app_struct)` which stores the pointer and creates the `/bin/` entry.

**OgzApp callbacks:** `on_open`, `on_draw`, `on_key`, `on_arrow`, `on_close` are required; `on_click`, `on_scroll`, `on_mouse_down`, `on_mouse_move`, `on_open_file` can be `nullptr`. `on_key` returns `bool` (true if the key was consumed). Each app window gets a 4096-byte `app_state` buffer — use `static_assert(sizeof(MyState) <= 4096)` to enforce.

**`on_open_file`** — optional callback `void (*)(u8 *state, const char *path, const char *content)` called after `on_open` when the app is launched to open a specific file. Used by Notepad (text editing) and Terminal (command execution).

**Coordinate convention:** `on_draw` receives content area in screen coordinates (cx, cy, cw, ch) excluding the 24px title bar. `on_click`/`on_mouse_down`/`on_mouse_move` receive (rx, ry) relative to content area origin. Scroll delta: positive = up, negative = down.

## Adding New Shell Commands

1. Implement in `shell/commands.h` + `shell/commands.cpp` using the `OutFn` callback pattern
2. Add dispatch to `shell/shell.cpp` `execute()` (UART shell)
3. Add dispatch to `apps/terminal.ogz.cpp` `term_exec()` (GUI terminal) — for commands that call UART-printing functions (like `net::ping()`), wrap with `uart::capture_start/stop`
4. Update help text in both places

## C# Interpreter and Widget System

The `lang/` directory contains a mini C# interpreter (`csharp::` namespace) with two execution modes:

- **Console mode**: `csharp::run(source, out_buf, out_size)` — executes code, captures output to buffer. Built-in: `Console.WriteLine/Write`.
- **GUI mode**: `csharp::init(source)` then per-frame `call_draw()`, `call_click(x,y)`, `call_key(key)`, `call_arrow(dir)` — for interactive programs. Built-in: `Gfx.Clear/FillRect/Rect/DrawText/Pixel/Line`, `App.Close/Width/Height`.

**Widget system** (GUI mode only): 5 widget types — `Label`, `Button`, `TextBox`, `CheckBox`, `Panel`. Max 16 widgets per program. Created with C# `new` syntax (e.g., `Button btn = new Button(x, y, w, h);`). Widgets support `.Text`, `.Clicked`, `.GetText()`, `.SetText()`, `.SetChecked()`.

Two apps use this: `csharp.ogz.cpp` (C# IDE with syntax highlighting, templates for Console/.csg programs) and `csgui.ogz.cpp` (GUI host for running .csg programs with `OnDraw`/`OnClick`/`OnKey`/`OnArrow` callbacks).

## Graphics Primitives

`gui/graphics.h` (`gfx::` namespace) provides double-buffered rendering:

- `gfx::clear(color)`, `gfx::pixel(x, y, color)`, `gfx::fill_rect(x, y, w, h, color)`, `gfx::rect(x, y, w, h, color)` (outline), `gfx::hline(x, y, w, color)`
- `gfx::draw_char(x, y, c, fg, bg)`, `gfx::draw_text(x, y, text, fg, bg)`, `gfx::draw_text_nobg(x, y, text, fg)`
- `gfx::text_width(text)`, `gfx::font_w()` (8px), `gfx::font_h()` (15px)
- `gfx::swap()` — copy backbuffer to framebuffer (called once per frame)

Color format: `0x00RRGGBB`. Backbuffer supports up to 1920×1080.

## UART Output Capture

`uart::capture_start(buf, size)` / `uart::capture_stop()` in `drivers/uart.h` — when active, `uart::putc()` also writes to the provided buffer (skipping `\r`). Used by the GUI terminal to capture output from functions that print directly to UART (network commands, etc.).

## Constraints

- **Freestanding C++17**: no STL, no libc, no exceptions, no RTTI (`-ffreestanding -nostdlib -fno-exceptions -fno-rtti`)
- All memory/string operations must use custom implementations in `lib/string.cpp`
- `kernel_main` must be declared `extern "C"` — called from assembly. Three C++ ABI stubs (`__cxa_pure_virtual`, `__cxa_atexit`, `__dso_handle`) are provided in kernel.cpp
- Network byte order conversions needed for all protocol headers (ARM64 is little-endian)
- Hardware addresses are hardcoded for QEMU virt machine — do not change without updating QEMU args
- QEMU runtime: 1GB RAM, cortex-a72, virtio-blk for disk, virtio-net with user-mode networking
- The kernel is single-threaded; secondary CPUs are parked in boot.S
- PSCI calls used for halt/reboot; ARM generic timer used for uptime
- QEMU user-mode networking: gateway 10.0.2.2, DHCP assigns 10.0.2.15, DNS at 10.0.2.3
- App state limited to 4096 bytes per window instance; max 8 simultaneous windows
- `syslog::init()` must be called after `fs::init()` to enable file logging
- Virtio mouse driver uses 64 event buffers with queue size 128 — insufficient buffers cause delayed button release events (sticky drag)
- `settings::save()` and `assoc::save()` call `fs::sync_to_disk()` internally — no separate sync needed
- The Settings app has 6 tabs: Region, Display, Keyboard, Background, Files, Menu
