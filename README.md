# OguzOS - ARM64 Minimal Operating System

A minimal operating system written in C++ for ARM64 (AArch64), designed to run on **UTM** (macOS) or **QEMU** directly.

## Features

- **Welcome banner** with ASCII art on boot
- **Interactive shell** with 20+ built-in commands
- **In-memory file system** with directories, files, read/write support
- **Networking** via virtio-net with DHCP, ARP, IPv4, ICMP (ping)
- **UART console** via PL011 (QEMU virt machine standard)
- **Command history** with arrow key navigation
- **ANSI color** support for a polished terminal experience

## Shell Commands

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `clear` | Clear the screen |
| `echo <text>` | Print text to console |
| `uname` | Show system information |
| `uptime` | Show system uptime |
| `history` | Show command history |
| `pwd` | Print working directory |
| `ls` | List directory contents |
| `cd <dir>` | Change directory |
| `mkdir <name>` | Create a directory |
| `touch <name>` | Create an empty file |
| `cat <file>` | Display file contents |
| `write <file> <text>` | Write text to a file |
| `append <file> <text>` | Append text to a file |
| `rm <name>` | Remove file or empty directory |
| `stat <name>` | Show file/directory info |
| `ifconfig` | Show network configuration |
| `ping <ip\|host> [count]` | Ping an IP address or hostname |
| `dhcp` | Request/renew IP via DHCP |
| `halt` | Halt the system |
| `reboot` | Reboot the system |

## Building

### Prerequisites

Install the AArch64 cross-compiler toolchain:

```bash
# macOS (Homebrew)
brew install aarch64-elf-gcc

# Ubuntu/Debian
sudo apt install gcc-aarch64-linux-gnu

# Arch Linux
sudo pacman -S aarch64-linux-gnu-gcc
```

### Compile

```bash
cd OguzOS-arm
make
```

This produces `build/oguzos.bin` — a raw binary kernel image.

### Test with QEMU (optional)

```bash
# Install QEMU if needed
brew install qemu   # macOS
# or: sudo apt install qemu-system-aarch64   # Linux

# Run directly
make run

# Exit with: Ctrl+A then X
```

## Running on UTM (macOS)

### Step-by-step Setup

1. **Open UTM** and click **Create a New Virtual Machine**

2. Select **Emulate** (not Virtualize)

3. Select **Other** as the operating system

4. **Architecture**: `ARM64 (aarch64)`

5. **System**:
   - Machine: `virt` (default)
   - CPU: `cortex-a72`
   - Memory: `128 MB` (enough for our OS)

6. **Skip** the drive/storage step (we don't need a disk)

7. **Summary**: give it a name like "OguzOS" and click **Save**

8. **Configure the kernel**:
   - Click on the VM, then click the **Settings** icon (gear)
   - Go to **QEMU** tab
   - Check **UEFI Boot**: **OFF** (uncheck it)
   - Under QEMU arguments, add:
     ```
     -kernel /path/to/OguzOS-arm/build/oguzos.bin
     ```
     Replace `/path/to/` with the actual path to your built binary.

9. **Serial Console**:
   - Go to **Display** settings
   - Change from VGA to **Serial** (Console Only)

10. **Start** the VM — you should see the OguzOS banner and shell!

### Alternative UTM Setup (Simpler)

1. Create new VM → **Emulate** → **Other**
2. In QEMU settings, set these raw QEMU arguments:
   ```
   -machine virt -cpu cortex-a72 -m 128M -nographic -kernel /path/to/build/oguzos.bin
   ```
3. Set display to **Serial**
4. Boot!

## Architecture

```
arch/           → ARM64 bootstrap (boot.S) and linker script (linker.ld)
kernel/         → Kernel entry point, banner, initializes subsystems
drivers/        → Hardware drivers: PL011 UART, Virtio block, Virtio net
fs/             → In-memory hierarchical file system
net/            → Network protocol stack (ARP, IPv4, ICMP, UDP, DHCP)
shell/          → Interactive command shell with history
lib/            → Freestanding string/memory utilities and type aliases
scripts/        → QEMU launcher (run.sh) and UTM image builder (mkimage.sh)
build/          → Generated object files and binaries
```

## Technical Details

- **No standard library** — everything from scratch (freestanding C++17)
- **No exceptions/RTTI** — minimal C++ runtime
- **PL011 UART** at `0x09000000` (QEMU virt machine standard)
- **Kernel loaded** at `0x40080000`
- **64KB stack** below kernel base
- **ARM generic timer** used for uptime calculation
- **PSCI** calls for halt/reboot
- **Virtio-net** with user-mode networking (QEMU `-netdev user`)
- **Network stack**: Ethernet, ARP, IPv4, ICMP echo, UDP, DHCP client

## License

MIT — do whatever you want with it.
