#pragma once

#include "types.h"

/*
 * Mini C# Interpreter for OguzOS
 *
 * Console mode: run() executes source and writes output to a buffer.
 * GUI mode:     init() loads source, then call_draw/click/key per frame.
 *
 * Built-in APIs:
 *   Console.WriteLine(expr), Console.Write(expr)
 *   Gfx.Clear(color), Gfx.FillRect(x,y,w,h,color), Gfx.Rect(x,y,w,h,color)
 *   Gfx.DrawText(x,y,text,color), Gfx.Pixel(x,y,color), Gfx.Line(x1,y1,x2,y2,color)
 *   App.Close(), App.Width(), App.Height()
 */

namespace csharp {

// ── Console mode ────────────────────────────────────────────────────────────
bool run(const char *source, char *out_buf, i32 out_size);

// ── GUI mode ────────────────────────────────────────────────────────────────
// Initialize: tokenize, find functions, execute Main() if present for init
bool init(const char *source);

// Check if a function exists (e.g. "OnDraw", "OnClick")
bool has_func(const char *name);

// Set drawing context (called by host before call_draw)
void set_draw_ctx(i32 cx, i32 cy, i32 cw, i32 ch);

// Call GUI event handlers
void call_draw();               // OnDraw(width, height)
void call_click(i32 x, i32 y); // OnClick(x, y)
bool call_key(char key);        // OnKey(key) → returns true if consumed
void call_arrow(char dir);      // OnArrow(dir)  0=up 1=down 2=right 3=left

// Check if App.Close() was called
bool should_close();

// Check if a runtime error occurred (and get the error message)
bool has_error();
const char *get_error();

// Clean up GUI state
void gui_cleanup();

} // namespace csharp
