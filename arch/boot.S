/*
 * OguzOS - ARM64 Boot Code
 * Targets QEMU virt machine (UTM on macOS)
 * PL011 UART at 0x09000000, kernel loaded at 0x40080000
 *
 * If at EL2, configures HCR_EL2 and drops to EL1.
 */

.section .text.boot
.global _start

/* Macro: print a character to UART (clobbers w12, x13) */
.macro UART ch
    mov     w12, #\ch
    mov     x13, #0x09000000
    str     w12, [x13]
.endm

/* Macro: print a 64-bit hex value in x14 to UART (clobbers w12, x13, x15, x16) */
.macro UART_HEX64
    mov     x13, #0x09000000
    mov     x16, #60               /* start from bit 60 (top nibble) */
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

_start:
    /* Only let CPU 0 through, park others */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_cpu
    b       park

primary_cpu:
    /* Print initial exception level */
    UART '['
    UART 'E'
    UART 'L'
    mrs     x0, CurrentEL
    and     x0, x0, #0xC
    lsr     x0, x0, #2
    add     w12, w0, #'0'
    mov     x13, #0x09000000
    str     w12, [x13]
    UART ']'
    UART ' '

    /* Check current exception level */
    cmp     x0, #2
    b.eq    at_el2
    cmp     x0, #3
    b.eq    at_el3
    b       at_el1

at_el3:
    /* EL3 → EL1 */
    mrs     x0, scr_el3
    orr     x0, x0, #(1 << 10)
    orr     x0, x0, #(1 << 0)
    msr     scr_el3, x0
    mov     x0, #0x3c5
    msr     spsr_el3, x0
    adr     x0, at_el1
    msr     elr_el3, x0
    eret

at_el2:
    /* Configure HCR_EL2: only RW=1 (bit 31), all trap bits = 0 */
    movz    x0, #0x8000, lsl #16   /* 0x80000000 */
    msr     hcr_el2, x0

    /* Read back and print HCR_EL2 for verification */
    mrs     x14, hcr_el2
    UART 'H'
    UART 'C'
    UART 'R'
    UART '='
    UART_HEX64
    UART ' '

    /* Minimal EL2 setup */
    msr     sctlr_el2, xzr
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mov     x0, #0x33ff
    msr     cptr_el2, x0

    /* Drop to EL1h via ERET */
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, at_el1
    msr     elr_el2, x0
    eret

at_el1:
    UART 'O'
    UART 'K'
    UART '\n'

    /* Set up stack pointer */
    ldr     x0, =_start
    mov     sp, x0

    /* Zero out BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
bss_loop:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       bss_loop
bss_done:

    /* Install exception vector table */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    /* Jump to C++ kernel entry */
    bl      kernel_main

park:
    wfe
    b       park
