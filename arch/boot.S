/*
 * OguzOS - ARM64 Boot Code
 * Targets QEMU virt machine (UTM on macOS)
 * PL011 UART at 0x09000000, kernel loaded at 0x40080000
 */

.section .text.boot
.global _start

_start:
    /* Only let CPU 0 through, park others */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_cpu
    b       park

primary_cpu:
    /* Set up stack pointer (64KB stack below kernel) */
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

    /* If kernel_main returns, halt */
park:
    wfe
    b       park
