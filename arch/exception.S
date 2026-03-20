/*
 * OguzOS — ARM64 Exception Vectors & Handlers
 *
 * Handles:
 *   - Current EL synchronous: crash recovery (try_enter/try_leave)
 *   - Lower EL synchronous:   SVC dispatch + EL0 fault handling
 */

.section .text

/* ── UART helpers (for debug output from assembly) ──────────────────── */
/* Print one character. Clobbers w12, x13. */
.macro UART ch
    mov     w12, #\ch
    mov     x13, #0x09000000
    str     w12, [x13]
.endm

/* Print 64-bit value in x14 as hex. Clobbers w12, x13, x15, x16. */
.macro UART_HEX64
    mov     x13, #0x09000000
    mov     x16, #60
.Lhex_loop\@:
    lsr     x15, x14, x16
    and     x15, x15, #0xF
    cmp     x15, #10
    b.lt    .Lhex_digit\@
    add     w12, w15, #('a' - 10)
    b       .Lhex_put\@
.Lhex_digit\@:
    add     w12, w15, #'0'
.Lhex_put\@:
    str     w12, [x13]
    cbz     x16, .Lhex_done\@
    sub     x16, x16, #4
    b       .Lhex_loop\@
.Lhex_done\@:
.endm

/* ══════════════════════════════════════════════════════════════════════
 * Exception Vector Table — must be 0x800-byte aligned, 16 entries
 * ══════════════════════════════════════════════════════════════════════ */
.balign 0x800
.global exception_vectors
exception_vectors:

/* Current EL, SP0 (not used) */
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang

/* Current EL, SPx — kernel fault recovery */
.balign 0x80
    b   sync_handler
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang

/* Lower EL, AArch64 — SVC from EL0 + fault handling */
.balign 0x80
    b   lower_sync_handler
.balign 0x80
    b   lower_other_handler    /* IRQ */
.balign 0x80
    b   lower_other_handler    /* FIQ */
.balign 0x80
    b   lower_other_handler    /* SError */

/* Lower EL, AArch32 (not used) */
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang


/* ══════════════════════════════════════════════════════════════════════
 * Current EL Synchronous Handler — crash recovery for EL1 code
 * ══════════════════════════════════════════════════════════════════════ */
sync_handler:
    adrp    x0, _try_active
    add     x0, x0, :lo12:_try_active
    ldr     w1, [x0]
    cbz     w1, el1_unhandled

    /* Deactivate recovery */
    str     wzr, [x0]

    /* Restore callee-saved regs from jmpbuf */
    adrp    x0, _try_jmpbuf
    add     x0, x0, :lo12:_try_jmpbuf
    ldp     x19, x20, [x0, #0]
    ldp     x21, x22, [x0, #16]
    ldp     x23, x24, [x0, #32]
    ldp     x25, x26, [x0, #48]
    ldp     x27, x28, [x0, #64]
    ldp     x29, x30, [x0, #80]
    ldr     x1, [x0, #96]
    mov     sp, x1
    msr     daifclr, #0xF
    mov     x0, #1
    ret

/* Unhandled EL1 exception — print diagnostic and hang */
el1_unhandled:
    UART '\n'
    UART '!'
    UART 'E'
    UART 'L'
    UART '1'
    UART ' '
    UART 'E'
    UART 'S'
    UART 'R'
    UART '='
    mrs     x14, esr_el1
    UART_HEX64
    UART ' '
    UART 'E'
    UART 'L'
    UART 'R'
    UART '='
    mrs     x14, elr_el1
    UART_HEX64
    UART ' '
    UART 'F'
    UART 'A'
    UART 'R'
    UART '='
    mrs     x14, far_el1
    UART_HEX64
    UART '\n'

exc_hang:
    wfe
    b       exc_hang


/* ══════════════════════════════════════════════════════════════════════
 * Lower EL Synchronous Handler — SVC from EL0 + fault recovery
 * ══════════════════════════════════════════════════════════════════════ */
lower_sync_handler:
    /* Save trap frame on EL1 kernel stack */
    sub     sp, sp, #176
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x29, x30, [sp, #96]
    mrs     x9, elr_el1
    mrs     x10, spsr_el1
    stp     x9, x10,  [sp, #112]

    /* Read exception syndrome */
    mrs     x9, esr_el1
    lsr     x10, x9, #26           /* EC = bits [31:26] */

    /* EC == 0x15 → SVC from AArch64 */
    cmp     x10, #0x15
    b.ne    lower_fault

    /* ── SVC path ──────────────────────────────────────────────── */

    /* Check for SYS_EXIT (x8 == 255) */
    ldr     x11, [sp, #64]         /* saved x8 = syscall number */
    cmp     x11, #255
    b.eq    svc_exit

    /* Normal syscall — call C++ dispatcher */
    mov     x0, x11
    mov     x1, sp
    bl      syscall_dispatch

    /* Store return value into the saved x0 slot */
    str     x0, [sp, #0]

    /* Restore trap frame and return to EL0 */
    ldp     x9, x10, [sp, #112]
    msr     elr_el1, x9
    msr     spsr_el1, x10
    ldp     x29, x30, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]
    add     sp, sp, #176
    eret

    /* ── SYS_EXIT: return to EL1 caller of el0_call ──────────── */
svc_exit:
    /* Load saved EL1 SP and resume PC */
    adrp    x9, _el1_saved_sp
    add     x9, x9, :lo12:_el1_saved_sp
    ldr     x10, [x9]
    mov     sp, x10

    adrp    x9, _el1_resume_pc
    add     x9, x9, :lo12:_el1_resume_pc
    ldr     x10, [x9]

    /* Return to EL1 via ERET with SPSR = EL1h (0x5) */
    msr     elr_el1, x10
    mov     x11, #0x5
    msr     spsr_el1, x11
    mov     x0, #0                 /* success */
    eret

    /* ── Fault from EL0 ─────────────────────────────────────────
     * Print diagnostic info then return to el0_call with error.
     */
lower_fault:
    /* Print: "\n!EL0 EC=xx ESR=... ELR=... FAR=...\n" */
    UART '\n'
    UART '!'
    UART 'E'
    UART 'L'
    UART '0'
    UART ' '

    /* Print EC (exception class, 6 bits) */
    UART 'E'
    UART 'C'
    UART '='
    mrs     x14, esr_el1
    lsr     x14, x14, #26
    /* Print as 2-digit hex */
    mov     x13, #0x09000000
    lsr     x15, x14, #4
    and     x15, x15, #0xF
    cmp     x15, #10
    b.lt    1f
    add     w12, w15, #('a' - 10)
    b       2f
1:  add     w12, w15, #'0'
2:  str     w12, [x13]
    and     x15, x14, #0xF
    cmp     x15, #10
    b.lt    3f
    add     w12, w15, #('a' - 10)
    b       4f
3:  add     w12, w15, #'0'
4:  str     w12, [x13]
    UART ' '

    /* Print full ESR */
    UART 'E'
    UART 'S'
    UART 'R'
    UART '='
    mrs     x14, esr_el1
    UART_HEX64
    UART ' '

    /* Print ELR (faulting PC) */
    UART 'E'
    UART 'L'
    UART 'R'
    UART '='
    mrs     x14, elr_el1
    UART_HEX64
    UART ' '

    /* Print FAR (fault address) */
    UART 'F'
    UART 'A'
    UART 'R'
    UART '='
    mrs     x14, far_el1
    UART_HEX64
    UART '\n'

    /* Return error to el0_call */
    adrp    x9, _el1_saved_sp
    add     x9, x9, :lo12:_el1_saved_sp
    ldr     x10, [x9]
    mov     sp, x10

    adrp    x9, _el1_resume_pc
    add     x9, x9, :lo12:_el1_resume_pc
    ldr     x10, [x9]

    msr     elr_el1, x10
    mov     x11, #0x5
    msr     spsr_el1, x11
    mov     x0, #1                 /* fault */
    eret

/* ── Lower EL non-sync (IRQ/FIQ/SError) — print and return error ── */
lower_other_handler:
    UART '\n'
    UART '!'
    UART 'I'
    UART 'R'
    UART 'Q'
    UART '\n'

    /* Return error to el0_call */
    adrp    x9, _el1_saved_sp
    add     x9, x9, :lo12:_el1_saved_sp
    ldr     x10, [x9]
    mov     sp, x10

    adrp    x9, _el1_resume_pc
    add     x9, x9, :lo12:_el1_resume_pc
    ldr     x10, [x9]

    msr     elr_el1, x10
    mov     x11, #0x5
    msr     spsr_el1, x11
    mov     x0, #1
    eret


/* ══════════════════════════════════════════════════════════════════════
 * try_enter / try_leave — crash recovery for EL1 code
 * ══════════════════════════════════════════════════════════════════════ */
.global try_enter
try_enter:
    adrp    x2, _try_jmpbuf
    add     x2, x2, :lo12:_try_jmpbuf
    stp     x19, x20, [x2, #0]
    stp     x21, x22, [x2, #16]
    stp     x23, x24, [x2, #32]
    stp     x25, x26, [x2, #48]
    stp     x27, x28, [x2, #64]
    stp     x29, x30, [x2, #80]
    mov     x3, sp
    str     x3, [x2, #96]
    adrp    x2, _try_active
    add     x2, x2, :lo12:_try_active
    mov     w3, #1
    str     w3, [x2]
    mov     x0, #0
    ret

.global try_leave
try_leave:
    adrp    x0, _try_active
    add     x0, x0, :lo12:_try_active
    str     wzr, [x0]
    ret


/* ── BSS data ──────────────────────────────────────────────────────── */
.section .bss
.balign 8

_try_jmpbuf:
    .space 104

_try_active:
    .space 4
