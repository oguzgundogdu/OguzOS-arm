#include "types.h"

/* User stacks for EL0 app execution.
 * 8 windows × 16 KB each = 128 KB total.
 * Placed in .bss.userstacks (user-accessible memory region). */

constexpr i32 MAX_WINDOWS = 8;
constexpr u64 USER_STACK_SIZE = 16384;

__attribute__((section(".userstacks"), aligned(4096)))
u8 user_stacks[MAX_WINDOWS][USER_STACK_SIZE];

/* Return the top of stack for window idx (stack grows downward). */
extern "C" u64 user_stack_top(i32 idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) idx = 0;
    return (u64)&user_stacks[idx][USER_STACK_SIZE];
}
