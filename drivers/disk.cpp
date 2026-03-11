#include "disk.h"
#include "string.h"
#include "uart.h"

namespace {

// QEMU virt machine: virtio-mmio at 0x0a000000, stride 0x200, up to 32 devices
constexpr u64 VIRTIO_BASE = 0x0a000000;
constexpr u64 VIRTIO_STRIDE = 0x200;
constexpr u32 VIRTIO_COUNT = 32;

// Virtio MMIO register offsets (shared between v1 and v2)
constexpr u32 REG_MAGIC = 0x000;
constexpr u32 REG_VERSION = 0x004;
constexpr u32 REG_DEVICE_ID = 0x008;
constexpr u32 REG_DEV_FEATURES = 0x010;
constexpr u32 REG_DRV_FEATURES = 0x020;
constexpr u32 REG_GUEST_PAGE_SIZE = 0x028; // v1 only
constexpr u32 REG_QUEUE_SEL = 0x030;
constexpr u32 REG_QUEUE_NUM_MAX = 0x034;
constexpr u32 REG_QUEUE_NUM = 0x038;
constexpr u32 REG_QUEUE_PFN = 0x040; // v1 only
constexpr u32 REG_QUEUE_NOTIFY = 0x050;
constexpr u32 REG_INT_STATUS = 0x060;
constexpr u32 REG_INT_ACK = 0x064;
constexpr u32 REG_STATUS = 0x070;
constexpr u32 REG_CONFIG = 0x100;

// Status bits
constexpr u32 STATUS_ACK = 1;
constexpr u32 STATUS_DRIVER = 2;
constexpr u32 STATUS_DRIVER_OK = 4;
constexpr u32 STATUS_FEATURES_OK = 8;

// Block request types
constexpr u32 BLK_T_IN = 0;  // read
constexpr u32 BLK_T_OUT = 1; // write

// Descriptor flags
constexpr u16 DESC_F_NEXT = 1;
constexpr u16 DESC_F_WRITE = 2;

constexpr u32 QUEUE_SIZE = 16;
constexpr u32 PAGE_SIZE = 4096;

// Virtqueue descriptor (16 bytes)
struct VirtqDesc {
  u64 addr;
  u32 len;
  u16 flags;
  u16 next;
} __attribute__((packed));

// Available ring
struct VirtqAvail {
  u16 flags;
  u16 idx;
  u16 ring[QUEUE_SIZE];
} __attribute__((packed));

// Used ring element
struct VirtqUsedElem {
  u32 id;
  u32 len;
} __attribute__((packed));

// Used ring
struct VirtqUsed {
  u16 flags;
  u16 idx;
  VirtqUsedElem ring[QUEUE_SIZE];
} __attribute__((packed));

// Block device request header
struct VirtioBlkReq {
  u32 type;
  u32 reserved;
  u64 sector;
} __attribute__((packed));

// Queue memory: contiguous layout for legacy virtio (v1)
// Page 0: descriptors + available ring
// Page 1: used ring
alignas(4096) u8 queue_mem[2 * PAGE_SIZE];

// Pointers into queue_mem
VirtqDesc *descs;
VirtqAvail *avail_ring;
VirtqUsed *used_ring;

// Request buffers
VirtioBlkReq blk_req;
u8 blk_status;

// Driver state
u64 base_addr = 0;
u64 capacity_sectors = 0;
bool initialized = false;
u16 last_used_idx = 0;

volatile u32 &mmio(u32 offset) {
  return *reinterpret_cast<volatile u32 *>(base_addr + offset);
}

void barrier() { asm volatile("dsb sy" ::: "memory"); }

void setup_queue_pointers() {
  // Descriptors at start of queue_mem
  descs = reinterpret_cast<VirtqDesc *>(queue_mem);
  // Available ring right after descriptors
  avail_ring = reinterpret_cast<VirtqAvail *>(queue_mem +
                                              QUEUE_SIZE * sizeof(VirtqDesc));
  // Used ring at next page boundary
  used_ring = reinterpret_cast<VirtqUsed *>(queue_mem + PAGE_SIZE);
}

bool init_device(u64 addr) {
  base_addr = addr;

  u32 magic = mmio(REG_MAGIC);
  if (magic != 0x74726976)
    return false;

  u32 version = mmio(REG_VERSION);
  u32 dev_id = mmio(REG_DEVICE_ID);
  if (dev_id != 2)
    return false;

  // Clear queue memory and set up pointers
  str::memset(queue_mem, 0, sizeof(queue_mem));
  setup_queue_pointers();

  // Reset
  mmio(REG_STATUS) = 0;
  barrier();

  // Acknowledge
  mmio(REG_STATUS) = STATUS_ACK;
  barrier();

  // Driver
  mmio(REG_STATUS) = STATUS_ACK | STATUS_DRIVER;
  barrier();

  // Negotiate features (accept none)
  mmio(REG_DRV_FEATURES) = 0;
  barrier();

  if (version == 1) {
    // Legacy (v1): set guest page size and use QueuePFN

    mmio(REG_GUEST_PAGE_SIZE) = PAGE_SIZE;
    barrier();

    // Select queue 0
    mmio(REG_QUEUE_SEL) = 0;
    barrier();

    u32 max_q = mmio(REG_QUEUE_NUM_MAX);
    if (max_q == 0)
      return false;

    u32 qsz = (max_q < QUEUE_SIZE) ? max_q : QUEUE_SIZE;
    mmio(REG_QUEUE_NUM) = qsz;
    barrier();

    // Tell device the PFN of queue memory
    u64 pfn = reinterpret_cast<u64>(queue_mem) / PAGE_SIZE;
    mmio(REG_QUEUE_PFN) = static_cast<u32>(pfn);
    barrier();

  } else {
    // Modern (v2): set separate desc/avail/used addresses

    mmio(REG_STATUS) = STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK;
    barrier();
    if (!(mmio(REG_STATUS) & STATUS_FEATURES_OK))
      return false;

    mmio(REG_QUEUE_SEL) = 0;
    barrier();

    u32 max_q = mmio(REG_QUEUE_NUM_MAX);
    if (max_q == 0)
      return false;

    u32 qsz = (max_q < QUEUE_SIZE) ? max_q : QUEUE_SIZE;
    mmio(REG_QUEUE_NUM) = qsz;

    u64 d = reinterpret_cast<u64>(descs);
    u64 a = reinterpret_cast<u64>(avail_ring);
    u64 u = reinterpret_cast<u64>(used_ring);

    mmio(0x080) = static_cast<u32>(d);
    mmio(0x084) = static_cast<u32>(d >> 32);
    mmio(0x090) = static_cast<u32>(a);
    mmio(0x094) = static_cast<u32>(a >> 32);
    mmio(0x0a0) = static_cast<u32>(u);
    mmio(0x0a4) = static_cast<u32>(u >> 32);
    barrier();

    mmio(0x044) = 1; // QueueReady
    barrier();
  }

  // Read disk capacity from config space
  u32 cap_lo = *reinterpret_cast<volatile u32 *>(base_addr + REG_CONFIG + 0);
  u32 cap_hi = *reinterpret_cast<volatile u32 *>(base_addr + REG_CONFIG + 4);
  capacity_sectors = (static_cast<u64>(cap_hi) << 32) | cap_lo;

  // Driver OK
  u32 ok_status = STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK;
  if (version == 2)
    ok_status |= STATUS_FEATURES_OK;
  mmio(REG_STATUS) = ok_status;
  barrier();

  last_used_idx = 0;
  initialized = true;
  return true;
}

bool do_blk_io(u32 type, u64 sector, void *buf) {
  if (!initialized)
    return false;

  // Set up request header
  blk_req.type = type;
  blk_req.reserved = 0;
  blk_req.sector = sector;
  blk_status = 0xFF;

  // Descriptor 0: header (device-readable)
  descs[0].addr = reinterpret_cast<u64>(&blk_req);
  descs[0].len = sizeof(VirtioBlkReq);
  descs[0].flags = DESC_F_NEXT;
  descs[0].next = 1;

  // Descriptor 1: data buffer
  descs[1].addr = reinterpret_cast<u64>(buf);
  descs[1].len = 512;
  descs[1].flags =
      DESC_F_NEXT | ((type == BLK_T_IN) ? DESC_F_WRITE : static_cast<u16>(0));
  descs[1].next = 2;

  // Descriptor 2: status byte (device-writable)
  descs[2].addr = reinterpret_cast<u64>(&blk_status);
  descs[2].len = 1;
  descs[2].flags = DESC_F_WRITE;
  descs[2].next = 0;

  barrier();

  // Submit to available ring
  avail_ring->ring[avail_ring->idx % QUEUE_SIZE] = 0;
  barrier();
  avail_ring->idx++;
  barrier();

  // Notify device (queue 0)
  mmio(REG_QUEUE_NOTIFY) = 0;
  barrier();

  // Poll for completion
  for (u32 tries = 0; tries < 10000000; tries++) {
    barrier();
    if (used_ring->idx != last_used_idx)
      break;
  }

  if (used_ring->idx == last_used_idx)
    return false;

  last_used_idx = used_ring->idx;

  // ACK interrupt
  u32 isr = mmio(REG_INT_STATUS);
  mmio(REG_INT_ACK) = isr;
  barrier();

  return blk_status == 0;
}

} // anonymous namespace

namespace disk {

bool init() {
  for (u32 i = 0; i < VIRTIO_COUNT; i++) {
    u64 addr = VIRTIO_BASE + i * VIRTIO_STRIDE;
    if (init_device(addr)) {
      uart::puts("  [disk] virtio-blk: ");
      uart::put_int(static_cast<i64>(capacity_sectors));
      uart::puts(" sectors (");
      uart::put_int(static_cast<i64>(capacity_sectors / 2));
      uart::puts(" KB)\n");
      return true;
    }
  }
  return false;
}

bool read_sector(u64 sector, void *buf) {
  if (sector >= capacity_sectors)
    return false;
  return do_blk_io(BLK_T_IN, sector, buf);
}

bool write_sector(u64 sector, const void *buf) {
  if (sector >= capacity_sectors)
    return false;
  return do_blk_io(BLK_T_OUT, sector, const_cast<void *>(buf));
}

bool read_sectors(u64 start, u32 count, void *buf) {
  auto *p = static_cast<u8 *>(buf);
  for (u32 i = 0; i < count; i++) {
    if (!read_sector(start + i, p + i * 512))
      return false;
  }
  return true;
}

bool write_sectors(u64 start, u32 count, const void *buf) {
  auto *p = static_cast<const u8 *>(buf);
  for (u32 i = 0; i < count; i++) {
    if (!write_sector(start + i, p + i * 512))
      return false;
  }
  return true;
}

u64 get_capacity() { return capacity_sectors; }

bool is_available() { return initialized; }

} // namespace disk
