#pragma once

#include "types.h"

namespace mmu {

/* Initialise L1+L2 page tables and enable the MMU with identity mapping.
   Must be called after uart::init(). */
void init();

/* Mark a region as EL0-accessible.  Operates on 2 MB block granularity.
   If writable is true, EL0 gets RW; otherwise EL0 gets RO. */
void set_user_accessible(u64 vaddr, u64 size, bool writable);

/* Revert a region to EL1-only (AP=00).  2 MB granularity. */
void set_kernel_only(u64 vaddr, u64 size);

/* Invalidate TLB after changing page table entries. */
void flush_tlb();

} // namespace mmu
