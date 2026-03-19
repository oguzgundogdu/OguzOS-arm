# OguzOS - ARM64 Minimal Operating System

A minimal operating system written in freestanding C++17 for ARM64 (AArch64), featuring a graphical desktop, networking stack, in-memory filesystem, and a built-in C# interpreter. Runs on **QEMU** and **UTM** (macOS).

## Features

- **Graphical desktop** with window manager, taskbar, and start menu
- **7 built-in GUI apps**: Notepad, Terminal, Task Manager, Settings, Browser, C# IDE, C# GUI Host
- **Interactive shell** with 50+ built-in commands and command history
- **In-memory filesystem** with directories, files, and disk persistence
- **Network stack**: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, HTTP, NTP
- **Mini C# interpreter** with console and GUI modes, plus a widget system
- **UART console** via PL011 with ANSI color support
- **No standard library** — everything from scratch

## Screenshot
<img width="1283" height="724" alt="image" src="https://github.com/user-attachments/assets/372e6887-b108-4a16-b517-6b2dc67c030c" />

> Boot the graphical desktop with `make gui`, or use text-only mode with `make run`.

## Shell Commands

### File System

| Command | Description |
|---------|-------------|
| `pwd` | Print working directory |
| `ls` | List directory contents |
| `cd <dir>` | Change directory |
| `mkdir <name>` | Create a directory |
| `touch <name>` | Create an empty file |
| `cat <file>` | Display file contents |
| `write <file> <text>` | Write text to a file |
| `append <file> <text>` | Append text to a file |
| `cp <src> <dst>` | Copy a file |
| `mv <src> <dst>` | Move or rename a file |
| `rm <name>` | Remove file or empty directory |
| `stat <name>` | Show file/directory info |
| `head <file> [n]` | Show first n lines (default 10) |
| `tail <file> [n]` | Show last n lines (default 10) |
| `wc <file>` | Count lines, words, bytes |
| `grep <pattern> <file>` | Search for pattern in file |
| `find [name]` | Search filesystem by name |
| `tree [path]` | Show directory tree |
| `xxd <file>` | Hex dump of file |
| `df` | Show filesystem usage |
| `sync` | Save filesystem to disk |

### System

| Command | Description |
|---------|-------------|
| `uname` | Show system information |
| `uptime` | Show system uptime |
| `date` | Show current date/time (NTP-synced) |
| `hostname` | Show system hostname |
| `whoami` | Show current user |
| `free` | Show memory/node usage |
| `dmesg` | Show kernel log |
| `which <cmd>` | Show command path |
| `history` | Show command history |
| `clear` | Clear the screen |
| `echo <text>` | Print text |
| `help` | Show all commands |

### Networking

| Command | Description |
|---------|-------------|
| `ifconfig` | Show network configuration |
| `ping <ip\|host> [count]` | Ping an IP address or hostname |
| `dhcp` | Request/renew IP via DHCP |
| `curl <url>` | Fetch URL via HTTP GET |

### Environment & Configuration

| Command | Description |
|---------|-------------|
| `env` | Show all environment variables |
| `export KEY=VALUE` | Set environment variable |
| `unset KEY` | Remove environment variable |
| `assoc <.ext> <app_id>` | Associate extension with app |
| `unassoc <.ext>` | Remove file association |
| `lsassoc` | List all file associations |
| `pin <app\|path> [label]` | Pin app or path to start menu |
| `unpin <app\|label>` | Remove from start menu |
| `lsmenu` | List start menu entries |

### C# Interpreter

| Command | Description |
|---------|-------------|
| `csrun <file.cs>` | Run C# console program |
| `csgui <file.cs>` | Run C# GUI program (requires GUI) |

### System Control

| Command | Description |
|---------|-------------|
| `gui` | Launch graphical desktop |
| `halt` | Sync and halt system |
| `reboot` | Sync and reboot system |

## GUI Applications

| App | Description |
|-----|-------------|
| **Notepad** | Text editor with cursor navigation, selection, save/save-as |
| **Terminal** | GUI terminal emulator with scrollback and command history |
| **Task Manager** | System monitor showing hardware status, open windows, and resource usage |
| **Settings** | System configuration (timezone, display, keyboard, background, file types, menu) |
| **Browser** | Web browser via yamur-proxy (Node.js + Puppeteer) |
| **C# IDE** | Code editor with syntax highlighting, templates, and run support |
| **C# GUI Host** | Runs interactive C# GUI programs (.csg) with widget support |

## C# Interpreter

OguzOS includes a mini C# interpreter with two execution modes:

**Console mode** — `csrun file.cs`
- `Console.WriteLine()` / `Console.Write()` for output

**GUI mode** — `csgui file.csg`
- Drawing: `Gfx.Clear`, `Gfx.FillRect`, `Gfx.Rect`, `Gfx.DrawText`, `Gfx.Pixel`, `Gfx.Line`
- Callbacks: `OnDraw`, `OnClick`, `OnKey`, `OnArrow`
- Widgets: `Label`, `Button`, `TextBox`, `CheckBox`, `Panel` (max 16 per program)
- System: `App.Close()`, `App.Width()`, `App.Height()`

## Building

### Prerequisites

Install the AArch64 cross-compiler toolchain and QEMU:

```bash
# macOS (Homebrew)
brew install aarch64-elf-gcc qemu

# Ubuntu/Debian
sudo apt install gcc-aarch64-linux-gnu qemu-system-aarch64

# Arch Linux
sudo pacman -S aarch64-linux-gnu-gcc qemu-system-aarch64
```

### Build & Run

```bash
make              # Build kernel → produces build/oguzos.bin
make run          # Build and run with QEMU (text-only, exit: Ctrl+A then X)
make gui          # Build and run with QEMU graphical mode (desktop + mouse + keyboard)
make debug        # Run with QEMU GDB server (-S -s) for debugging
make dump         # Disassemble the ELF binary
make clean        # Remove build/ directory
make distclean    # Remove build/ and disk.img (forces fresh filesystem on next boot)
```

Override the cross-compiler prefix: `CROSS=aarch64-linux-gnu- make`

## Running on UTM (macOS)

### Quick Setup

1. Create new VM → **Emulate** → **Other**
2. Architecture: `ARM64 (aarch64)`, Machine: `virt`, CPU: `cortex-a72`, Memory: `1024 MB`
3. In QEMU settings, disable **UEFI Boot** and add:
   ```
   -kernel /path/to/OguzOS-arm/build/oguzos.bin
   ```
4. For text-only: set display to **Serial**
5. For GUI mode: add ramfb, virtio-tablet, and virtio-keyboard devices
6. Boot!

### Alternative (Raw QEMU Arguments)

Set these in UTM's QEMU settings:
```
-machine virt -cpu cortex-a72 -m 1G -nographic -kernel /path/to/build/oguzos.bin
```

## Architecture

```
arch/       — ARM64 bootstrap (boot.S), exception vectors, linker script
kernel/     — Kernel entry point (kernel_main)
drivers/    — PL011 UART, Virtio block/net, ramfb, virtio-tablet, virtio-keyboard
fs/         — In-memory hierarchical filesystem (128 nodes, 4KB/file, disk persistence)
net/        — Network stack (ARP, IPv4, ICMP, UDP, DHCP, DNS, HTTP, NTP)
gui/        — Window manager, desktop, taskbar, start menu, file explorer
apps/       — GUI applications (Notepad, Terminal, Task Manager, Settings, Browser, C# IDE, C# GUI Host)
shell/      — UART shell and shared command library
lib/        — String/memory utils, syslog, settings, env vars, file associations, menu config
lang/       — Mini C# interpreter (console + GUI modes) with widget system
scripts/    — QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/      — Generated object files and binaries (gitignored)
```

## Technical Details

- **Freestanding C++17** — no STL, no libc, no exceptions, no RTTI
- **ARM64 bare metal** — single-threaded, secondary CPUs parked
- **PL011 UART** at `0x09000000` (QEMU virt machine)
- **Kernel loaded** at `0x40080000` with 64KB stack
- **1GB RAM** on QEMU virt machine (cortex-a72)
- **Virtio drivers** for block storage, networking, mouse (tablet), and keyboard
- **ARM generic timer** for uptime calculation
- **PSCI calls** for halt/reboot
- **Double-buffered graphics** via ramfb (up to 1920×1080)
- **Network**: DHCP auto-config, DNS resolution, NTP time sync, HTTP client
- **Filesystem**: 128 nodes, 4KB per file, persisted to virtio-blk disk

## License

MIT — do whatever you want with it.
