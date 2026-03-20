#include "assoc.h"
#include "disk.h"
#include "env.h"
#include "menu.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "mmu.h"
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

  // Enable MMU with identity mapping (VA = PA).
  // All RAM starts as AP=00 (EL1 only) so kernel code can execute.
  mmu::init();

  // Grant EL0 access to all RAM except the first 2 MB block (L2[0])
  // which contains kernel .text and must stay AP=00 so EL1 can
  // execute from it (AP=01 forces PXN=1 per ARM spec).
  {
    u64 user_start = 0x40200000ULL;  /* L2[1] onward */
    u64 user_size  = 0x3FE00000ULL;  /* rest of the 1 GB region */
    mmu::set_user_accessible(user_start, user_size, true);
    mmu::flush_tlb();
  }

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
    assoc::set(".cs", "csharp.ogz");
    assoc::set(".csg", "csgui.ogz");
    assoc::set(".sln", "csharp.ogz");
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
  // Install C# .ogz apps into /bin/
  fs::touch("calculator.ogz");
  fs::write("calculator.ogz",
    "using System;\n"
    "class Calc {\n"
    "    static string display;\n"
    "    static string op1;\n"
    "    static string op;\n"
    "    static bool fresh;\n"
    "    static bool err;\n"
    "    static Label lblD;\n"
    "    static Label lblE;\n"
    "    static int BW;\n"
    "    static int BH;\n"
    "    static int P;\n"
    "    static int SX;\n"
    "    static int SY;\n"
    "\n"
    "    static void Main() {\n"
    "        display = \"0\";\n"
    "        op1 = \"\";\n"
    "        op = \"\";\n"
    "        fresh = true;\n"
    "        err = false;\n"
    "        BW = 58;\n"
    "        BH = 44;\n"
    "        P = 4;\n"
    "        SX = 12;\n"
    "        SY = 72;\n"
    "        lblE = new Label(16, 12, \"\");\n"
    "        lblD = new Label(16, 38, \"0\");\n"
    "    }\n"
    "\n"
    "    static void Digit(string d) {\n"
    "        if (err) { return; }\n"
    "        if (fresh) { display = d; fresh = false; }\n"
    "        else {\n"
    "            if (display == \"0\") { display = d; }\n"
    "            else { display = display + d; }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    static void DoOp(string nop) {\n"
    "        if (err) { return; }\n"
    "        if (op != \"\" && !fresh) { Equals(); }\n"
    "        op1 = display;\n"
    "        op = nop;\n"
    "        fresh = true;\n"
    "        lblE.SetText(op1 + \" \" + op);\n"
    "    }\n"
    "\n"
    "    static void Equals() {\n"
    "        if (err) { return; }\n"
    "        if (op == \"\") { return; }\n"
    "        int a = int.Parse(op1);\n"
    "        int b = int.Parse(display);\n"
    "        lblE.SetText(op1 + \" \" + op + \" \" + display + \" =\");\n"
    "        int r = 0;\n"
    "        if (op == \"+\") { r = a + b; }\n"
    "        if (op == \"-\") { r = a - b; }\n"
    "        if (op == \"x\") { r = a * b; }\n"
    "        if (op == \"/\") {\n"
    "            if (b == 0) { err = true; display = \"Error\"; lblE.SetText(\"\"); op1 = \"\"; op = \"\"; fresh = true; return; }\n"
    "            r = a / b;\n"
    "        }\n"
    "        display = \"\" + r;\n"
    "        op1 = \"\";\n"
    "        op = \"\";\n"
    "        fresh = true;\n"
    "    }\n"
    "\n"
    "    static void Clear() {\n"
    "        display = \"0\";\n"
    "        op1 = \"\";\n"
    "        op = \"\";\n"
    "        fresh = true;\n"
    "        err = false;\n"
    "        lblE.SetText(\"\");\n"
    "    }\n"
    "\n"
    "    static void Negate() {\n"
    "        if (err) { return; }\n"
    "        if (display == \"0\") { return; }\n"
    "        int v = int.Parse(display);\n"
    "        display = \"\" + (0 - v);\n"
    "    }\n"
    "\n"
    "    static void Back() {\n"
    "        if (err) { return; }\n"
    "        if (fresh) { return; }\n"
    "        display = \"0\";\n"
    "        fresh = true;\n"
    "    }\n"
    "\n"
    "    static void Btn(int c, int r, string t, int bg) {\n"
    "        int x = SX + c * (BW + P);\n"
    "        int y = SY + r * (BH + P);\n"
    "        Gfx.FillRect(x, y, BW, BH, bg);\n"
    "        Gfx.Rect(x, y, BW, BH, 0x444466);\n"
    "        Gfx.DrawText(x + (BW - 8) / 2, y + (BH - 15) / 2, t, 0xFFFFFF);\n"
    "    }\n"
    "\n"
    "    static void WBtn(int c, int r, int s, string t, int bg) {\n"
    "        int x = SX + c * (BW + P);\n"
    "        int y = SY + r * (BH + P);\n"
    "        int w = BW * s + P * (s - 1);\n"
    "        Gfx.FillRect(x, y, w, BH, bg);\n"
    "        Gfx.Rect(x, y, w, BH, 0x444466);\n"
    "        Gfx.DrawText(x + (w - 8) / 2, y + (BH - 15) / 2, t, 0xFFFFFF);\n"
    "    }\n"
    "\n"
    "    static void OnDraw(int w, int h) {\n"
    "        Gfx.Clear(0x1E1E2E);\n"
    "        Gfx.FillRect(8, 6, w - 16, 58, 0x181825);\n"
    "        Gfx.Rect(8, 6, w - 16, 58, 0x333355);\n"
    "        lblE.SetColor(0x888AAA);\n"
    "        lblE.SetPos(16, 12);\n"
    "        lblE.Draw();\n"
    "        if (err) { lblD.SetText(\"Error\"); lblD.SetColor(0xF38BA8); }\n"
    "        else { lblD.SetText(display); lblD.SetColor(0xCDD6F4); }\n"
    "        lblD.SetPos(16, 38);\n"
    "        lblD.Draw();\n"
    "        Btn(0,0,\"C\",0xC0392B); Btn(1,0,\"+/-\",0x3D3D5C); Btn(2,0,\"<\",0x3D3D5C); Btn(3,0,\"/\",0x4A4070);\n"
    "        Btn(0,1,\"7\",0x333355); Btn(1,1,\"8\",0x333355); Btn(2,1,\"9\",0x333355); Btn(3,1,\"x\",0x4A4070);\n"
    "        Btn(0,2,\"4\",0x333355); Btn(1,2,\"5\",0x333355); Btn(2,2,\"6\",0x333355); Btn(3,2,\"-\",0x4A4070);\n"
    "        Btn(0,3,\"1\",0x333355); Btn(1,3,\"2\",0x333355); Btn(2,3,\"3\",0x333355); Btn(3,3,\"+\",0x4A4070);\n"
    "        WBtn(0,4,2,\"0\",0x333355); WBtn(2,4,2,\"=\",0x6C5CE7);\n"
    "    }\n"
    "\n"
    "    static void OnClick(int x, int y) {\n"
    "        int row = (y - SY) / (BH + P);\n"
    "        int col = (x - SX) / (BW + P);\n"
    "        if (row < 0) { return; } if (row > 4) { return; }\n"
    "        if (col < 0) { return; } if (col > 3) { return; }\n"
    "        int bx = SX + col * (BW + P);\n"
    "        int by = SY + row * (BH + P);\n"
    "        if (x < bx) { return; } if (y < by) { return; }\n"
    "        if (y >= by + BH) { return; }\n"
    "        if (row == 4) {\n"
    "            if (x < SX + 2 * (BW + P)) { Digit(\"0\"); return; }\n"
    "            Equals(); return;\n"
    "        }\n"
    "        if (x >= bx + BW) { return; }\n"
    "        int b = row * 4 + col;\n"
    "        if (b==0) { Clear(); } if (b==1) { Negate(); } if (b==2) { Back(); } if (b==3) { DoOp(\"/\"); }\n"
    "        if (b==4) { Digit(\"7\"); } if (b==5) { Digit(\"8\"); } if (b==6) { Digit(\"9\"); } if (b==7) { DoOp(\"x\"); }\n"
    "        if (b==8) { Digit(\"4\"); } if (b==9) { Digit(\"5\"); } if (b==10) { Digit(\"6\"); } if (b==11) { DoOp(\"-\"); }\n"
    "        if (b==12) { Digit(\"1\"); } if (b==13) { Digit(\"2\"); } if (b==14) { Digit(\"3\"); } if (b==15) { DoOp(\"+\"); }\n"
    "    }\n"
    "\n"
    "    static void OnKey(int k) {\n"
    "        if (k==48) { Digit(\"0\"); } if (k==49) { Digit(\"1\"); } if (k==50) { Digit(\"2\"); }\n"
    "        if (k==51) { Digit(\"3\"); } if (k==52) { Digit(\"4\"); } if (k==53) { Digit(\"5\"); }\n"
    "        if (k==54) { Digit(\"6\"); } if (k==55) { Digit(\"7\"); } if (k==56) { Digit(\"8\"); }\n"
    "        if (k==57) { Digit(\"9\"); }\n"
    "        if (k==43) { DoOp(\"+\"); } if (k==45) { DoOp(\"-\"); }\n"
    "        if (k==42) { DoOp(\"x\"); } if (k==47) { DoOp(\"/\"); }\n"
    "        if (k==61) { Equals(); } if (k==13) { Equals(); } if (k==10) { Equals(); }\n"
    "        if (k==27) { Clear(); } if (k==127) { Clear(); }\n"
    "    }\n"
    "}\n"
  );

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
    menu::add(menu::ENTRY_APP, "Calculator", "calculator.ogz");
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
