#pragma once

#include "types.h"

/*
 * Shared command implementations for UART shell and GUI terminal.
 *
 * Each command takes an output function pointer so it works with
 * both uart::puts (UART shell) and term_append (GUI terminal).
 */

namespace cmd {

using OutFn = void (*)(void *ctx, const char *text);

// Filesystem commands
void cp(OutFn out, void *ctx, const char *src, const char *dst);
void mv(OutFn out, void *ctx, const char *src, const char *dst);
void head(OutFn out, void *ctx, const char *file, i32 lines);
void tail(OutFn out, void *ctx, const char *file, i32 lines);
void wc(OutFn out, void *ctx, const char *file);
void find(OutFn out, void *ctx, const char *name);
void tree(OutFn out, void *ctx, const char *path, i32 depth);
void df(OutFn out, void *ctx);
void grep(OutFn out, void *ctx, const char *pattern, const char *file);

// C# interpreter
void csrun(OutFn out, void *ctx, const char *filepath);

// System info
void date(OutFn out, void *ctx);
void hostname(OutFn out, void *ctx);
void whoami(OutFn out, void *ctx);
void free_cmd(OutFn out, void *ctx);
void dmesg(OutFn out, void *ctx);
void which(OutFn out, void *ctx, const char *name);
void xxd(OutFn out, void *ctx, const char *file);

} // namespace cmd
