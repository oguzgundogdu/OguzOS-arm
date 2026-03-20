#include "app.h"
#include "registry.h"

#ifdef USERSPACE
#include "userapi.h"
#else
#include "csharp.h"
#include "fs.h"
#include "graphics.h"
#include "string.h"
#include "syslog.h"
#endif

/*
 * csgui.ogz — C# GUI App Host
 *
 * Hosts a C# program that defines OnDraw/OnClick/OnKey callbacks.
 * The program runs inside its own window with full drawing access.
 */

namespace {

struct CsGuiState {
  char filepath[128];
  bool initialized;
  bool error;
  char error_msg[128];
};

static_assert(sizeof(CsGuiState) <= 4096, "CsGuiState too large");

void csgui_open(u8 *state) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  s->filepath[0] = '\0';
  s->initialized = false;
  s->error = false;
  s->error_msg[0] = '\0';
}

void csgui_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<CsGuiState *>(state);

  if (s->error) {
    gfx::fill_rect(cx, cy, cw, ch, 0x001E1E2E);
    gfx::draw_text(cx + 10, cy + 10, "C# Error:", 0x00F38BA8, 0x001E1E2E);
    gfx::draw_text(cx + 10, cy + 30, s->error_msg, 0x00CDD6F4, 0x001E1E2E);
    return;
  }

  if (!s->initialized) {
    gfx::fill_rect(cx, cy, cw, ch, 0x001E1E2E);
    gfx::draw_text(cx + 10, cy + 10, "No program loaded.", 0x006C7086, 0x001E1E2E);
    return;
  }

  // Call C# OnDraw
  csharp::set_draw_ctx(cx, cy, cw, ch);
  csharp::call_draw();

  // Check for runtime error after drawing
  if (csharp::has_error()) {
    s->error = true;
    const char *emsg = csharp::get_error();
    if (emsg && emsg[0])
      str::ncpy(s->error_msg, emsg, 127);
    else
      str::cpy(s->error_msg, "Runtime error");
  }
}

bool csgui_key(u8 *state, char key) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  if (!s->initialized || s->error) return false;
  bool consumed = csharp::call_key(key);
  if (csharp::has_error()) {
    s->error = true;
    const char *emsg = csharp::get_error();
    if (emsg && emsg[0])
      str::ncpy(s->error_msg, emsg, 127);
    else
      str::cpy(s->error_msg, "Runtime error");
  }
  return consumed;
}

void csgui_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  if (!s->initialized) return;
  csharp::call_arrow(dir);
}

void csgui_close(u8 *) {
  csharp::gui_cleanup();
}

void csgui_click(u8 *state, i32 rx, i32 ry, i32 /*cw*/, i32 /*ch*/) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  if (!s->initialized || s->error) return;
  csharp::call_click(rx, ry);
  if (csharp::has_error()) {
    s->error = true;
    const char *emsg = csharp::get_error();
    if (emsg && emsg[0])
      str::ncpy(s->error_msg, emsg, 127);
    else
      str::cpy(s->error_msg, "Runtime error");
  }
}

void csgui_scroll(u8 *, i32) {}

void csgui_open_file(u8 *state, const char *path, const char *content) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  str::ncpy(s->filepath, path, 127);

  // Load and initialize the C# program
  syslog::info("csgui", "init source len=%d", static_cast<i32>(str::len(content)));
  if (!csharp::init(content)) {
    s->error = true;
    str::cpy(s->error_msg, "Failed to initialize program");
    syslog::error("csgui", "init failed");
    return;
  }

  syslog::info("csgui", "has OnDraw=%d has Main=%d",
               csharp::has_func("OnDraw") ? 1 : 0,
               csharp::has_func("Main") ? 1 : 0);

  if (!csharp::has_func("OnDraw")) {
    s->error = true;
    str::cpy(s->error_msg, "No OnDraw() method found");
    return;
  }

  s->initialized = true;
  syslog::info("csgui", "loaded: %s", path);
}

const OgzApp csgui_app = {
    "C# App",        // name
    "csgui.ogz",     // id
    500,              // default_w
    400,              // default_h
    csgui_open,
    csgui_draw,
    csgui_key,
    csgui_arrow,
    csgui_close,
    csgui_click,
    csgui_scroll,
    nullptr,          // on_mouse_down
    nullptr,          // on_mouse_move
    csgui_open_file,
};

} // anonymous namespace

namespace apps {
void register_csgui() { register_app(&csgui_app); }
} // namespace apps
