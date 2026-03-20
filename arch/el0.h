#pragma once

#include "types.h"

/* Drop to EL0, call func(a0,a1,a2,a3,a4), return to EL1 when done.
 * Returns 0 on success, 1 if the app faulted. */
extern "C" i32 el0_call(void *func, u64 a0, u64 a1, u64 a2,
                         u64 a3, u64 a4, u64 user_sp);
