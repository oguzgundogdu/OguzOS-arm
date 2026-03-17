#include "app.h"
#include "csharp.h"
#include "fs.h"
#include "graphics.h"
#include "registry.h"
#include "string.h"
#include "syslog.h"

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
}

bool csgui_key(u8 *state, char key) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  if (!s->initialized) return false;
  return csharp::call_key(key);
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
  if (!s->initialized) return;
  csharp::call_click(rx, ry);
}

void csgui_scroll(u8 *, i32) {}

void csgui_open_file(u8 *state, const char *path, const char *content) {
  auto *s = reinterpret_cast<CsGuiState *>(state);
  str::ncpy(s->filepath, path, 127);

  // Load and initialize the C# program
  if (!csharp::init(content)) {
    s->error = true;
    str::cpy(s->error_msg, "Failed to initialize program");
    return;
  }

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
