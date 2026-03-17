#include "assoc.h"
#include "disk.h"
#include "env.h"
#include "menu.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "netdev.h"
#include "registry.h"
#include "settings.h"
#include "shell.h"
#include "string.h"
#include "syslog.h"
#include "uart.h"

// App registration (defined in apps/*.ogz.cpp)
namespace apps {
void register_notepad();
void register_terminal();
void register_taskman();
void register_settings();
void register_browser();
void register_csharp();
void register_csgui();
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
    // Sync system clock from NTP
    if (net::is_available()) {
      if (net::ntp_sync())
        syslog::info("kernel", "NTP time synced");
      else
        syslog::warn("kernel", "NTP sync failed, using uptime clock");
    }
  }

  // Try loading filesystem from disk, fall back to default init
  if (has_disk && fs::load_from_disk()) {
    syslog::info("kernel", "filesystem restored from disk");
  } else {
    fs::init();
    syslog::info("kernel", "filesystem initialized (default)");
  }

  // Load saved settings from /etc/settings
  settings::load();

  // Apply saved resolution (if different from default 1920x1080)
  if (has_fb) {
    u32 sw = settings::get_res_w();
    u32 sh = settings::get_res_h();
    if (sw != fb::width() || sh != fb::height()) {
      if (fb::set_resolution(sw, sh))
        syslog::info("kernel", "resolution set to %dx%d from settings", sw, sh);
    }
  }

  // Initialize environment variables
  env::init();

  // Initialize file associations (load saved or set defaults)
  assoc::init();
  assoc::load();
  if (assoc::count() == 0) {
    assoc::set(".txt", "notepad.ogz");
    assoc::set(".md", "notepad.ogz");
    assoc::set(".log", "notepad.ogz");
    assoc::set(".cfg", "notepad.ogz");
    assoc::set(".conf", "notepad.ogz");
    assoc::set(".csv", "notepad.ogz");
    assoc::set(".sh", "notepad.ogz");
    assoc::set(".json", "notepad.ogz");
    assoc::set(".cs", "terminal.ogz");
    assoc::set(".csg", "csgui.ogz");
    assoc::save();
    syslog::info("kernel", "created default /etc/filetypes");
  }

  // Enable file logging now that fs is ready
  syslog::init();

  // Register .ogz apps
  apps::register_notepad();
  apps::register_terminal();
  apps::register_taskman();
  apps::register_settings();
  apps::register_browser();
  apps::register_csharp();
  apps::register_csgui();
  syslog::info("kernel", "registered %d apps", apps::count());

  // Ensure key directories exist (even on disk-restored FS)
  fs::cd("/");
  if (fs::resolve("/bin") < 0)
    fs::mkdir("bin");
  if (fs::resolve("/home") < 0)
    fs::mkdir("home");
  if (fs::resolve("/home/Desktop") < 0) {
    fs::cd("/home");
    fs::mkdir("Desktop");
    fs::cd("/");
  }
  fs::cd("/bin");
  for (i32 i = 0; i < apps::count(); i++) {
    const OgzApp *app = apps::get(i);
    if (!app)
      continue;
    fs::touch(app->id);
    char desc[256];
    str::cpy(desc, "#!/ogz\n");
    str::cat(desc, "name=");
    str::cat(desc, app->name);
    str::cat(desc, "\nid=");
    str::cat(desc, app->id);
    str::cat(desc, "\ntype=application\n");
    fs::write(app->id, desc);
  }
  fs::cd("/");
  syslog::info("kernel", "installed %d binaries in /bin", apps::count());

  // Initialize start menu (load saved or build default)
  menu::init();
  menu::load();
  if (menu::count() == 0) {
    // Pin all registered apps
    for (i32 i = 0; i < apps::count(); i++) {
      const OgzApp *app = apps::get(i);
      if (app)
        menu::add(menu::ENTRY_APP, app->name, app->id);
    }
    menu::add(menu::ENTRY_SEP, "---", "");
    menu::add(menu::ENTRY_EXPLORER, "File Explorer", "");
    menu::add(menu::ENTRY_ABOUT, "About OguzOS", "");
    menu::add(menu::ENTRY_SEP, "---", "");
    menu::add(menu::ENTRY_RESTART, "Restart", "");
    menu::add(menu::ENTRY_SHUTDOWN, "Shutdown", "");
    menu::save();
    syslog::info("kernel", "created default /etc/menu");
  }

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
