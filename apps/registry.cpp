#include "registry.h"
#include "assoc.h"
#include "fs.h"
#include "menu.h"
#include "string.h"
#include "syslog.h"

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

const OgzApp *app_list[apps::MAX_APPS];
i32 app_count = 0;

} // anonymous namespace

namespace apps {

void register_app(const OgzApp *app) {
  if (app_count < MAX_APPS)
    app_list[app_count++] = app;
}

i32 count() { return app_count; }

const OgzApp *get(i32 index) {
  if (index < 0 || index >= app_count)
    return nullptr;
  return app_list[index];
}

const OgzApp *find(const char *id) {
  for (i32 i = 0; i < app_count; i++) {
    if (str::cmp(app_list[i]->id, id) == 0)
      return app_list[i];
  }
  return nullptr;
}

void init() {
  // ── Register native apps ──────────────────────────────────────────────
  register_notepad();
  register_terminal();
  register_taskman();
  register_settings();
  register_browser();
  register_csharp();
  register_csgui();
  syslog::info("apps", "registered %d apps", app_count);

  // ── Ensure key directories exist (even on disk-restored FS) ───────────
  fs::cd("/");
  if (fs::resolve("/bin") < 0)  fs::mkdir("bin");
  if (fs::resolve("/lib") < 0)  fs::mkdir("lib");
  if (fs::resolve("/home") < 0) fs::mkdir("home");
  if (fs::resolve("/home/Desktop") < 0) {
    fs::cd("/home");
    fs::mkdir("Desktop");
    fs::cd("/");
  }

  // ── Install app descriptors into /bin/ ────────────────────────────────
  fs::cd("/bin");
  for (i32 i = 0; i < app_count; i++) {
    const OgzApp *app = app_list[i];
    if (!app) continue;
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
  syslog::info("apps", "installed %d binaries in /bin", app_count);

  // ── Default file associations ─────────────────────────────────────────
  assoc::init();
  assoc::load();
  if (assoc::count() == 0) {
    assoc::set(".txt", "notepad.ogz");
    assoc::set(".md",  "notepad.ogz");
    assoc::set(".log", "notepad.ogz");
    assoc::set(".cfg", "notepad.ogz");
    assoc::set(".conf","notepad.ogz");
    assoc::set(".csv", "notepad.ogz");
    assoc::set(".sh",  "notepad.ogz");
    assoc::set(".json","notepad.ogz");
    assoc::set(".cs",  "csharp.ogz");
    assoc::set(".sln", "csharp.ogz");
    assoc::save();
    syslog::info("apps", "created default /etc/filetypes");
  }

  // ── Default start menu ────────────────────────────────────────────────
  menu::init();
  menu::load();
  if (menu::count() == 0) {
    for (i32 i = 0; i < app_count; i++) {
      const OgzApp *app = app_list[i];
      if (app)
        menu::add(menu::ENTRY_APP, app->name, app->id);
    }
    menu::add(menu::ENTRY_APP, "Calculator", "calculator.cs");
    menu::add(menu::ENTRY_SEP, "---", "");
    menu::add(menu::ENTRY_EXPLORER, "File Explorer", "");
    menu::add(menu::ENTRY_ABOUT, "About OguzOS", "");
    menu::add(menu::ENTRY_SEP, "---", "");
    menu::add(menu::ENTRY_RESTART, "Restart", "");
    menu::add(menu::ENTRY_SHUTDOWN, "Shutdown", "");
    menu::save();
    syslog::info("apps", "created default /etc/menu");
  }
}

} // namespace apps
