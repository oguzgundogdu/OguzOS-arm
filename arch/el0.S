/*
 * OguzOS — EL1 ↔ EL0 Trampoline
 *
 * el0_call(func, a0, a1, a2, a3, a4, user_sp)
 *   Drops to EL0 to execute func(a0..a4), returns to EL1 when done.
 *   Returns 0 on success, 1 on fault.
 */

.section .text

/* ── Globals shared with exception handler ────────────────────────── */
.global _el1_saved_sp
.global _el1_resume_pc

.section .bss
.balign 8
_el1_saved_sp:  .space 8
_el1_resume_pc: .space 8

/* ── el0_call ─────────────────────────────────────────────────────── */
.section .text
.global el0_call
el0_call:
    /* Save callee-saved registers + LR on kernel stack */
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!

    /* Save kernel SP so the exception handler can restore it */
    mov     x19, sp
    adrp    x20, _el1_saved_sp
    add     x20, x20, :lo12:_el1_saved_sp
    str     x19, [x20]

    /* Save resume address */
    adrp    x20, .L_el0_resume
    add     x20, x20, :lo12:.L_el0_resume
    adrp    x19, _el1_resume_pc
    add     x19, x19, :lo12:_el1_resume_pc
    str     x20, [x19]

    /* Set user stack pointer */
    msr     sp_el0, x6

    /* Set return-from-exception destination = func */
    msr     elr_el1, x0

    /* SPSR = EL0t (M[3:0]=0b0000), all exceptions unmasked */
    mov     x9, #0x0
    msr     spsr_el1, x9

    /* Set LR for the callee so "ret" branches to the trampoline */
    adrp    x30, el0_return_trampoline
    add     x30, x30, :lo12:el0_return_trampoline

    /* Shift arguments: x0=a0, x1=a1, x2=a2, x3=a3, x4=a4 */
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    mov     x4, x5

    /* Drop to EL0 */
    eret

    /* ── Resume point (jumped to by exception handler) ──────────── */
.L_el0_resume:
    /* x0 = return value (0=ok, 1=fault) */
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret


/* ── Return trampoline (runs at EL0) ────────────────────────────── */
.section .text
.global el0_return_trampoline
el0_return_trampoline:
    mov     x8, #255        /* SYS_EXIT */
    svc     #0
    b       .
