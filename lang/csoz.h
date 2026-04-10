#pragma once

#include "types.h"

/*
 * CSOZ — C# Interpreter for OguzOS
 *
 * Console mode: run() executes source and writes output to a buffer.
 * GUI mode:     init() loads source, then call_draw/click/key per frame.
 *
 * Built-in APIs:
 *   Console.WriteLine(expr), Console.Write(expr)
 *   Gfx.Clear/FillRect/Rect/DrawText/Pixel/Line/HLine
 *   Canvas.Create/Clear/SetPixel/Line/FillRect/Rect/Brush/Draw
 *   App.Close(), App.Width(), App.Height()
 *   UI.CreateLabel/CreateButton/CreateTextBox/CreateCheckBox/CreatePanel
 */

namespace csoz {

// ── Console mode ────────────────────────────────────────────────────────────
bool run(const char *source, char *out_buf, i32 out_size);

// ── GUI mode ────────────────────────────────────────────────────────────────
// Initialize: tokenize, find functions, execute Main() if present for init
bool init(const char *source);

// Check if source contains a class deriving from Window (GUI app)
bool is_window_app(const char *source);

// Check if a function exists (e.g. "OnDraw", "OnClick")
bool has_func(const char *name);

// Set drawing context (called by host before call_draw)
void set_draw_ctx(i32 cx, i32 cy, i32 cw, i32 ch);

// Call GUI event handlers
void call_draw();               // OnDraw(width, height)
void call_click(i32 x, i32 y); // OnClick(x, y)
bool call_key(char key);        // OnKey(key) → returns true if consumed
void call_arrow(char dir);      // OnArrow(dir)  0=up 1=down 2=right 3=left
void call_mouse_down(i32 x, i32 y); // OnMouseDown(x, y)
void call_mouse_move(i32 x, i32 y); // OnMouseMove(x, y)

// Check if App.Close() was called
bool should_close();

// Check if a runtime error occurred (and get the error message)
bool has_error();
const char *get_error();

// Clean up GUI state
void gui_cleanup();

} // namespace csoz
