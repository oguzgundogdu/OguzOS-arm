#include "disk.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "netdev.h"
#include "registry.h"
#include "shell.h"
#include "string.h"
#include "syslog.h"
#include "uart.h"

// App registration (defined in apps/*.ogz.cpp)
namespace apps {
void register_notepad();
void register_terminal();
}

namespace {

void print_banner() {
  uart::puts("\033[2J\033[H"); // Clear screen

  uart::puts("\033[1;36m");
  uart::puts("  ___                   ___  ____  \n");
  uart::puts(" / _ \\ __ _ _   _ ___  / _ \\/ ___| \n");
  uart::puts("| | | / _` | | | |_ / | | | \\___ \\ \n");
  uart::puts("| |_| | (_| | |_| |/ /  | |_| |___) |\n");
  uart::puts(" \\___/ \\__, |\\__,_/___|  \\___/|____/ \n");
  uart::puts("       |___/                         \n");
  uart::puts("\033[0m\n");

  uart::puts("\033[1;33m");
  uart::puts("         OguzOS v1.0 - ARM64\n");
  uart::puts("    Minimal Operating System for UTM\n");
  uart::puts("\033[0m\n");

  uart::puts("\033[0;37m");
  uart::puts("  Architecture : AArch64 (ARM64)\n");
  uart::puts("  Platform     : QEMU virt machine\n");
  uart::puts("  Console      : PL011 UART\n");
  if (disk::is_available()) {
    uart::puts("  File System  : In-Memory FS + Disk\n");
    uart::puts("  Disk         : virtio-blk (");
    uart::put_int(static_cast<i64>(disk::get_capacity() / 2));
    uart::puts(" KB)\n");
  } else {
    uart::puts("  File System  : In-Memory FS (no disk)\n");
  }
  if (netdev::is_available()) {
    uart::puts("  Network      : virtio-net");
    if (net::is_available())
      uart::puts(" (DHCP configured)");
    uart::putc('\n');
  }
  uart::puts("\033[0m\n");

  uart::puts("  Welcome! Type \033[1mhelp\033[0m for available commands.\n");
  uart::puts("  Type \033[1mcat /home/welcome.txt\033[0m to read the welcome "
             "file.\n\n");

  uart::puts("\033[0;90m");
  uart::puts("  ─────────────────────────────────────\n");
  uart::puts("\033[0m\n");
}

} // anonymous namespace

extern "C" void kernel_main() {
  // Initialize UART first (for any debug output)
  uart::init();

  syslog::info("kernel", "OguzOS booting...");

  // Initialize disk (virtio-blk)
  bool has_disk = disk::init();
  syslog::info("kernel", "disk: %s", has_disk ? "ok" : "not found");

  // Initialize framebuffer (ramfb, only present with 'make gui')
  bool has_fb = fb::init();
  syslog::info("kernel", "framebuffer: %s", has_fb ? "ok" : "not found");

  // Initialize mouse (virtio-tablet, only present with 'make gui')
  bool has_mouse = mouse::init();
  syslog::info("kernel", "mouse: %s", has_mouse ? "ok" : "not found");

  // Initialize keyboard (virtio-keyboard, only present with 'make gui')
  bool has_kbd = keyboard::init();
  syslog::info("kernel", "keyboard: %s", has_kbd ? "ok" : "not found");

  // Initialize network (virtio-net + DHCP)
  netdev::init();
  if (netdev::is_available()) {
    net::init();
  }

  // Try loading filesystem from disk, fall back to default init
  if (has_disk && fs::load_from_disk()) {
    syslog::info("kernel", "filesystem restored from disk");
  } else {
    fs::init();
    syslog::info("kernel", "filesystem initialized (default)");
  }

  // Register .ogz apps
  apps::register_notepad();
  apps::register_terminal();
  syslog::info("kernel", "registered %d apps", apps::count());

  // Print welcome banner
  print_banner();

  // If framebuffer is available, launch GUI directly
  if (has_fb) {
    syslog::info("kernel", "launching GUI");
    gui::run();
    syslog::info("kernel", "GUI exited, falling back to shell");
  }

  // Initialize and run shell (never returns)
  shell::init();
  shell::run();
}

// C++ ABI support for freestanding environment
extern "C" void __cxa_pure_virtual() {
  for (;;)
    ;
}
extern "C" void __cxa_atexit() {}
extern "C" void __dso_handle() {}
