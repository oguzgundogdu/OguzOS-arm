#pragma once

#include "types.h"

/*
 * Mini C# Interpreter for OguzOS
 *
 * Supports a subset of C#:
 *   - using System; (recognized, ignored)
 *   - class with static methods
 *   - Types: int, string, bool
 *   - Console.WriteLine() / Console.Write()
 *   - if/else, while, for, return
 *   - Arithmetic, comparison, logical, string concatenation
 *   - Functions with parameters and return values
 *   - // comments
 */

namespace csharp {

// Run a C# program. Returns true on success, false on error.
// Output (including errors) is written to out_buf.
bool run(const char *source, char *out_buf, i32 out_size);

} // namespace csharp
