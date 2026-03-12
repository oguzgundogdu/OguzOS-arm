/*
 * OguzOS — ARM64 Exception Vectors & Crash Recovery
 *
 * Provides a minimal exception vector table and setjmp/longjmp-style
 * recovery mechanism so that app faults don't crash the entire OS.
 */

.section .text

/* ── Exception Vector Table ──────────────────────────────────────────
 * Must be 0x800 (2048) byte aligned. 16 entries, 0x80 bytes each.
 * We only handle "Current EL, SPx, Synchronous" (offset 0x200).
 * Everything else hangs.
 */
.balign 0x800
.global exception_vectors
exception_vectors:

/* Current EL, SP0 */
    b   exc_hang            /* Synchronous  */
.balign 0x80
    b   exc_hang            /* IRQ          */
.balign 0x80
    b   exc_hang            /* FIQ          */
.balign 0x80
    b   exc_hang            /* SError       */

/* Current EL, SPx — this is what we actually use */
.balign 0x80
    b   sync_handler        /* Synchronous  */
.balign 0x80
    b   exc_hang            /* IRQ          */
.balign 0x80
    b   exc_hang            /* FIQ          */
.balign 0x80
    b   exc_hang            /* SError       */

/* Lower EL, AArch64 */
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang

/* Lower EL, AArch32 */
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang
.balign 0x80
    b   exc_hang


/* ── Synchronous Exception Handler ───────────────────────────────── */
sync_handler:
    /* Check if crash recovery is active */
    adrp    x0, _try_active
    add     x0, x0, :lo12:_try_active
    ldr     w1, [x0]
    cbz     w1, exc_hang

    /* Deactivate recovery */
    str     wzr, [x0]

    /* Restore callee-saved registers from jmpbuf */
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

    /* Unmask exceptions (clear DAIF) so future faults are caught */
    msr     daifclr, #0xF

    /* Return 1 to the try_enter() call site */
    mov     x0, #1
    ret

exc_hang:
    wfe
    b       exc_hang


/* ── try_enter: save recovery point ──────────────────────────────── *
 * Saves callee-saved registers (x19–x30) + SP to _try_jmpbuf,
 * sets _try_active = 1, returns 0.
 * On crash recovery, returns 1 (set by sync_handler above).
 */
.global try_enter
try_enter:
    /* Save registers to jmpbuf */
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

    /* Set _try_active = 1 */
    adrp    x2, _try_active
    add     x2, x2, :lo12:_try_active
    mov     w3, #1
    str     w3, [x2]

    /* Return 0 (normal path) */
    mov     x0, #0
    ret


/* ── try_leave: clear recovery point ─────────────────────────────── */
.global try_leave
try_leave:
    adrp    x0, _try_active
    add     x0, x0, :lo12:_try_active
    str     wzr, [x0]
    ret


/* ── BSS data ────────────────────────────────────────────────────── */
.section .bss
.balign 8

_try_jmpbuf:
    .space 104          /* 13 × 8 bytes: x19–x30 + SP */

_try_active:
    .space 4            /* u32 flag */
