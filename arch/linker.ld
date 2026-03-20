/*
 * OguzOS Linker Script
 * For QEMU virt machine (ARM64)
 * Kernel loaded at 0x40080000
 */

ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    __text_start = .;
    .text : {
        *(.text.boot)    /* Boot code first */
        *(.text .text.*)
    }
    __text_end = .;

    /* Align to 2 MB so .rodata lands in a separate L2 block from .text.
       L2[0] (containing .text) stays AP=00 (EL1 executable).
       L2[1+] (rodata/data/bss) gets AP=01 (EL0 data-accessible). */
    . = ALIGN(0x200000);
    __rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    }
    __rodata_end = .;

    . = ALIGN(4096);
    __data_start = .;
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    /* User-accessible regions — NOT zeroed by boot (QEMU RAM is zero).
     * Use section names that DON'T match .bss.* to avoid being
     * captured by the main .bss glob above. */
    . = ALIGN(4096);
    __userbuf_start = .;
    .userbuf (NOLOAD) : {
        *(.userbuf)
    }
    __userbuf_end = .;

    . = ALIGN(4096);
    __userstacks_start = .;
    .userstacks (NOLOAD) : {
        *(.userstacks)
    }
    __userstacks_end = .;

    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
