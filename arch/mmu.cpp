#include "mmu.h"
#include "uart.h"

namespace mmu {

/* ── Page tables ──────────────────────────────────────────────────────
 * L1: 512 entries, each 1 GB.   L1[0] = device block, L1[1] → L2.
 * L2: 512 entries, each 2 MB.   Covers 0x4000_0000 – 0x7FFF_FFFF.
 */
static u64 l1_table[512] __attribute__((aligned(4096)));
static u64 l2_table[512] __attribute__((aligned(4096)));

/* ── Descriptor bits ────────────────────────────────────────────────── */
constexpr u64 VALID       = 0x1;
constexpr u64 TABLE_DESC  = 0x3;        /* valid + table (for L1→L2) */
constexpr u64 BLOCK_DESC  = 0x1;        /* valid + block (L1/L2 block) */

constexpr u64 ATTR_IDX(u64 i) { return (i & 7) << 2; }
constexpr u64 AP_EL1_RW     = (0ULL << 6); /* AP[2:1]=00 */
constexpr u64 AP_EL1_RW_EL0_RW = (1ULL << 6); /* AP[2:1]=01 */
constexpr u64 AP_EL1_RO     = (2ULL << 6); /* AP[2:1]=10 */
constexpr u64 AP_EL1_RO_EL0_RO = (3ULL << 6); /* AP[2:1]=11 */
constexpr u64 SH_OSH  = (2ULL << 8);
constexpr u64 SH_ISH  = (3ULL << 8);
constexpr u64 AF_BIT   = (1ULL << 10);
constexpr u64 PXN_BIT  = (1ULL << 53);
constexpr u64 UXN_BIT  = (1ULL << 54);
constexpr u64 AP_MASK  = (3ULL << 6);

constexpr u64 MAIR_DEVICE = 0;
constexpr u64 MAIR_NORMAL = 1;

constexpr u64 BLOCK_2MB = 0x200000ULL;
constexpr u64 RAM_BASE  = 0x40000000ULL;

/* ── Helpers ────────────────────────────────────────────────────────── */

/* Return L2 index for a physical address in the RAM region. */
static i32 l2_index(u64 addr) {
    if (addr < RAM_BASE) return -1;
    u64 off = addr - RAM_BASE;
    i32 idx = static_cast<i32>(off / BLOCK_2MB);
    return (idx < 512) ? idx : -1;
}

/* ── Public API ─────────────────────────────────────────────────────── */

void init() {
    /* 1. MAIR: Attr0 = Device-nGnRnE, Attr1 = Normal WB-RWARA */
    u64 mair = (0x00ULL << 0) | (0xFFULL << 8);
    asm volatile("msr mair_el1, %0" : : "r"(mair));

    /* 2. TCR: 39-bit VA, 4KB granule, WB-WA walks, Inner Shareable.
       Read PARange from ID_AA64MMFR0_EL1 and set IPS to match. */
    u64 mmfr0;
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    u64 pa_range = mmfr0 & 0xFULL;

    u64 tcr = (25ULL << 0)        /* T0SZ  */
            | (1ULL  << 8)        /* IRGN0 */
            | (1ULL  << 10)       /* ORGN0 */
            | (3ULL  << 12)       /* SH0   */
            | (0ULL  << 14)       /* TG0   */
            | (1ULL  << 23)       /* EPD1  */
            | (pa_range << 32);   /* IPS = PARange */
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));

    /* 3. Build page tables ------------------------------------------- */
    for (int i = 0; i < 512; i++) {
        l1_table[i] = 0;
        l2_table[i] = 0;
    }

    /* L1[0]: 0x0000_0000 – 0x3FFF_FFFF → Device 1 GB block */
    l1_table[0] = 0x00000000ULL | BLOCK_DESC
                | ATTR_IDX(MAIR_DEVICE) | AP_EL1_RW
                | SH_OSH | AF_BIT | PXN_BIT | UXN_BIT;

    /* L1[1]: 0x4000_0000 – 0x7FFF_FFFF → L2 table (2 MB blocks) */
    l1_table[1] = ((u64)l2_table) | TABLE_DESC;

    /* Fill L2: identity-map 512 × 2 MB blocks as Normal cacheable.
       Use AP=00 (EL1 RW only) — AP=01 forces PXN=1 per ARM spec,
       which prevents EL1 instruction fetch.  EL0 access is granted
       later via set_user_accessible() for specific regions. */
    for (int i = 0; i < 512; i++) {
        u64 pa = RAM_BASE + (u64)i * BLOCK_2MB;
        l2_table[i] = pa | BLOCK_DESC
                     | ATTR_IDX(MAIR_NORMAL) | AP_EL1_RW
                     | SH_ISH | AF_BIT;
    }

    /* 4. Install tables and enable MMU */
    asm volatile("dsb sy" ::: "memory");
    asm volatile("msr ttbr0_el1, %0" : : "r"((u64)l1_table));
    asm volatile("isb");

    u64 sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);   /* M  */
    sctlr |= (1ULL << 2);   /* C  */
    sctlr |= (1ULL << 12);  /* I  */
    sctlr &= ~(1ULL << 19); /* WXN clear */
    asm volatile("msr sctlr_el1, %0" : : "r"(sctlr));
    asm volatile("isb");

    uart::puts("[mmu] MMU enabled (L1+L2 identity mapping)\n");
}

void set_user_accessible(u64 vaddr, u64 size, bool writable) {
    u64 end = vaddr + size;
    /* Align down to 2 MB boundary */
    vaddr &= ~(BLOCK_2MB - 1);
    for (u64 a = vaddr; a < end; a += BLOCK_2MB) {
        i32 idx = l2_index(a);
        if (idx < 0) continue;
        l2_table[idx] &= ~AP_MASK;
        if (writable)
            l2_table[idx] |= AP_EL1_RW_EL0_RW;
        else
            l2_table[idx] |= AP_EL1_RO_EL0_RO;
    }
}

void set_kernel_only(u64 vaddr, u64 size) {
    u64 end = vaddr + size;
    vaddr &= ~(BLOCK_2MB - 1);
    for (u64 a = vaddr; a < end; a += BLOCK_2MB) {
        i32 idx = l2_index(a);
        if (idx < 0) continue;
        l2_table[idx] &= ~AP_MASK;
        l2_table[idx] |= AP_EL1_RW;
    }
}

void flush_tlb() {
    /* Toggle MMU off/on to force TLB flush. With identity mapping
       this is safe.  Direct TLBI hangs on QEMU (EL2 trap). */
    u64 sctlr;
    asm volatile("dsb sy"     ::: "memory");
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    asm volatile("msr sctlr_el1, %0" : : "r"(sctlr & ~1ULL));  /* M=0 */
    asm volatile("isb"        ::: "memory");
    asm volatile("msr sctlr_el1, %0" : : "r"(sctlr));           /* M=1 */
    asm volatile("isb"        ::: "memory");
}

} // namespace mmu
