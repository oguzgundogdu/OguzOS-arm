#include "netdev.h"
#include "string.h"
#include "uart.h"

namespace {

// QEMU virt machine: virtio-mmio at 0x0a000000, stride 0x200, up to 32 devices
constexpr u64 VIRTIO_BASE = 0x0a000000;
constexpr u64 VIRTIO_STRIDE = 0x200;
constexpr u32 VIRTIO_COUNT = 32;

// Virtio MMIO register offsets
constexpr u32 REG_MAGIC = 0x000;
constexpr u32 REG_VERSION = 0x004;
constexpr u32 REG_DEVICE_ID = 0x008;
constexpr u32 REG_DEV_FEATURES = 0x010;
constexpr u32 REG_DRV_FEATURES = 0x020;
constexpr u32 REG_GUEST_PAGE_SIZE = 0x028;
constexpr u32 REG_QUEUE_SEL = 0x030;
constexpr u32 REG_QUEUE_NUM_MAX = 0x034;
constexpr u32 REG_QUEUE_NUM = 0x038;
constexpr u32 REG_QUEUE_PFN = 0x040;
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

// Virtio net feature bits
constexpr u32 VIRTIO_NET_F_MAC = (1 << 5);

// Descriptor flags
constexpr u16 DESC_F_NEXT = 1;
constexpr u16 DESC_F_WRITE = 2;

constexpr u32 QUEUE_SIZE = 16;
constexpr u32 PAGE_SIZE = 4096;

// Virtio-net header (legacy, without mergeable rx buffers)
struct VirtioNetHdr {
  u8 flags;
  u8 gso_type;
  u16 hdr_len;
  u16 gso_size;
  u16 csum_start;
  u16 csum_offset;
} __attribute__((packed));

constexpr u32 NET_HDR_SIZE = sizeof(VirtioNetHdr);

// Virtqueue descriptor
struct VirtqDesc {
  u64 addr;
  u32 len;
  u16 flags;
  u16 next;
} __attribute__((packed));

struct VirtqAvail {
  u16 flags;
  u16 idx;
  u16 ring[QUEUE_SIZE];
} __attribute__((packed));

struct VirtqUsedElem {
  u32 id;
  u32 len;
} __attribute__((packed));

struct VirtqUsed {
  u16 flags;
  u16 idx;
  VirtqUsedElem ring[QUEUE_SIZE];
} __attribute__((packed));

// RX queue memory and state
alignas(4096) u8 rx_queue_mem[2 * PAGE_SIZE];
VirtqDesc *rx_descs;
VirtqAvail *rx_avail;
VirtqUsed *rx_used;
u16 rx_last_used_idx = 0;

// TX queue memory and state
alignas(4096) u8 tx_queue_mem[2 * PAGE_SIZE];
VirtqDesc *tx_descs;
VirtqAvail *tx_avail;
VirtqUsed *tx_used;
u16 tx_last_used_idx = 0;
u16 tx_next_desc = 0;

// Packet buffers
constexpr u32 PKT_BUF_SIZE = 2048;
constexpr u32 NUM_RX_BUFS = 8;
u8 rx_bufs[NUM_RX_BUFS][PKT_BUF_SIZE];
u8 tx_bufs[QUEUE_SIZE][PKT_BUF_SIZE];

// Device state
u64 base_addr = 0;
u8 mac_addr[6];
bool initialized = false;

volatile u32 &mmio(u32 offset) {
  return *reinterpret_cast<volatile u32 *>(base_addr + offset);
}

void barrier() { asm volatile("dmb ish" ::: "memory"); }

void setup_queue_pointers(u8 *mem, VirtqDesc *&descs, VirtqAvail *&avail,
                          VirtqUsed *&used) {
  descs = reinterpret_cast<VirtqDesc *>(mem);
  avail =
      reinterpret_cast<VirtqAvail *>(mem + QUEUE_SIZE * sizeof(VirtqDesc));
  used = reinterpret_cast<VirtqUsed *>(mem + PAGE_SIZE);
}

bool setup_queue_v1(u32 queue_idx, u8 *mem, VirtqDesc *&descs,
                    VirtqAvail *&avail, VirtqUsed *&used) {
  str::memset(mem, 0, 2 * PAGE_SIZE);
  setup_queue_pointers(mem, descs, avail, used);

  mmio(REG_QUEUE_SEL) = queue_idx;
  barrier();

  u32 max_q = mmio(REG_QUEUE_NUM_MAX);
  if (max_q == 0)
    return false;

  u32 qsz = (max_q < QUEUE_SIZE) ? max_q : QUEUE_SIZE;
  mmio(REG_QUEUE_NUM) = qsz;
  barrier();

  u64 pfn = reinterpret_cast<u64>(mem) / PAGE_SIZE;
  mmio(REG_QUEUE_PFN) = static_cast<u32>(pfn);
  barrier();

  return true;
}

bool setup_queue_v2(u32 queue_idx, u8 *mem, VirtqDesc *&descs,
                    VirtqAvail *&avail, VirtqUsed *&used) {
  str::memset(mem, 0, 2 * PAGE_SIZE);
  setup_queue_pointers(mem, descs, avail, used);

  mmio(REG_QUEUE_SEL) = queue_idx;
  barrier();

  u32 max_q = mmio(REG_QUEUE_NUM_MAX);
  if (max_q == 0)
    return false;

  u32 qsz = (max_q < QUEUE_SIZE) ? max_q : QUEUE_SIZE;
  mmio(REG_QUEUE_NUM) = qsz;

  u64 d = reinterpret_cast<u64>(descs);
  u64 a = reinterpret_cast<u64>(avail);
  u64 u = reinterpret_cast<u64>(used);

  mmio(0x080) = static_cast<u32>(d);
  mmio(0x084) = static_cast<u32>(d >> 32);
  mmio(0x090) = static_cast<u32>(a);
  mmio(0x094) = static_cast<u32>(a >> 32);
  mmio(0x0a0) = static_cast<u32>(u);
  mmio(0x0a4) = static_cast<u32>(u >> 32);
  barrier();

  mmio(0x044) = 1; // QueueReady
  barrier();

  return true;
}

void post_rx_buffers() {
  for (u32 i = 0; i < NUM_RX_BUFS; i++) {
    rx_descs[i].addr = reinterpret_cast<u64>(rx_bufs[i]);
    rx_descs[i].len = PKT_BUF_SIZE;
    rx_descs[i].flags = DESC_F_WRITE;
    rx_descs[i].next = 0;
    rx_avail->ring[(rx_avail->idx + i) % QUEUE_SIZE] = static_cast<u16>(i);
  }
  barrier();
  rx_avail->idx += NUM_RX_BUFS;
  barrier();
  mmio(REG_QUEUE_NOTIFY) = 0;
}

bool init_device(u64 addr) {
  base_addr = addr;

  u32 magic = mmio(REG_MAGIC);
  if (magic != 0x74726976)
    return false;

  u32 version = mmio(REG_VERSION);
  u32 dev_id = mmio(REG_DEVICE_ID);
  if (dev_id != 1) // 1 = network device
    return false;

  // Reset
  mmio(REG_STATUS) = 0;
  barrier();

  // Acknowledge
  mmio(REG_STATUS) = STATUS_ACK;
  barrier();

  // Driver
  mmio(REG_STATUS) = STATUS_ACK | STATUS_DRIVER;
  barrier();

  // Negotiate features: accept MAC feature
  u32 dev_features = mmio(REG_DEV_FEATURES);
  u32 drv_features = dev_features & VIRTIO_NET_F_MAC;
  mmio(REG_DRV_FEATURES) = drv_features;
  barrier();

  bool ok;
  if (version == 1) {
    mmio(REG_GUEST_PAGE_SIZE) = PAGE_SIZE;
    barrier();

    // Setup RX queue (queue 0)
    ok = setup_queue_v1(0, rx_queue_mem, rx_descs, rx_avail, rx_used);
    if (!ok)
      return false;

    // Setup TX queue (queue 1)
    ok = setup_queue_v1(1, tx_queue_mem, tx_descs, tx_avail, tx_used);
    if (!ok)
      return false;
  } else {
    mmio(REG_STATUS) = STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK;
    barrier();
    if (!(mmio(REG_STATUS) & STATUS_FEATURES_OK))
      return false;

    ok = setup_queue_v2(0, rx_queue_mem, rx_descs, rx_avail, rx_used);
    if (!ok)
      return false;

    ok = setup_queue_v2(1, tx_queue_mem, tx_descs, tx_avail, tx_used);
    if (!ok)
      return false;
  }

  // Read MAC address from config space
  for (int i = 0; i < 6; i++) {
    mac_addr[i] =
        *reinterpret_cast<volatile u8 *>(base_addr + REG_CONFIG + i);
  }

  // Driver OK
  u32 ok_status = STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK;
  if (version == 2)
    ok_status |= STATUS_FEATURES_OK;
  mmio(REG_STATUS) = ok_status;
  barrier();

  // Pre-post RX buffers
  rx_last_used_idx = 0;
  tx_last_used_idx = 0;
  tx_next_desc = 0;
  post_rx_buffers();

  initialized = true;
  return true;
}

} // anonymous namespace

namespace netdev {

bool init() {
  for (u32 i = 0; i < VIRTIO_COUNT; i++) {
    u64 addr = VIRTIO_BASE + i * VIRTIO_STRIDE;
    if (init_device(addr)) {
      uart::puts("  [net]  virtio-net: MAC ");
      for (int j = 0; j < 6; j++) {
        if (j > 0)
          uart::putc(':');
        u8 hi = mac_addr[j] >> 4;
        u8 lo = mac_addr[j] & 0xF;
        const char *hex = "0123456789abcdef";
        uart::putc(hex[hi]);
        uart::putc(hex[lo]);
      }
      uart::putc('\n');
      return true;
    }
  }
  return false;
}

bool is_available() { return initialized; }

void get_mac(u8 mac[6]) { str::memcpy(mac, mac_addr, 6); }

bool send(const void *data, u32 len) {
  if (!initialized || len + NET_HDR_SIZE > PKT_BUF_SIZE)
    return false;

  // Keep at least one free descriptor; if queue is full, try to reap used slots.
  u16 outstanding = static_cast<u16>(tx_next_desc - tx_last_used_idx);
  if (outstanding >= QUEUE_SIZE) {
    for (u32 tries = 0; tries < 20000; tries++) {
      barrier();
      if (tx_used->idx != tx_last_used_idx) {
        tx_last_used_idx = tx_used->idx;
        break;
      }
      asm volatile("yield");
    }
    outstanding = static_cast<u16>(tx_next_desc - tx_last_used_idx);
    if (outstanding >= QUEUE_SIZE)
      return false;
  }

  u16 desc_idx = static_cast<u16>(tx_next_desc % QUEUE_SIZE);
  tx_next_desc++;
  u8 *tx_buf = tx_bufs[desc_idx];

  // Prepare: virtio-net header (all zeros) + frame data
  str::memset(tx_buf, 0, NET_HDR_SIZE);
  str::memcpy(tx_buf + NET_HDR_SIZE, data, len);

  u32 total = NET_HDR_SIZE + len;

  tx_descs[desc_idx].addr = reinterpret_cast<u64>(tx_buf);
  tx_descs[desc_idx].len = total;
  tx_descs[desc_idx].flags = 0; // device-readable
  tx_descs[desc_idx].next = 0;

  barrier();
  tx_avail->ring[tx_avail->idx % QUEUE_SIZE] = desc_idx;
  barrier();
  tx_avail->idx++;
  barrier();

  // Notify TX queue (queue 1)
  mmio(REG_QUEUE_NOTIFY) = 1;
  barrier();

  // Opportunistically reap completions without blocking.
  barrier();
  if (tx_used->idx != tx_last_used_idx)
    tx_last_used_idx = tx_used->idx;

  u32 isr = mmio(REG_INT_STATUS);
  mmio(REG_INT_ACK) = isr;
  barrier();

  return true;
}

i32 recv(void *buf, u32 buf_size) {
  if (!initialized)
    return -1;

  barrier();
  if (rx_used->idx == rx_last_used_idx)
    return -1;

  u16 used_idx = rx_last_used_idx % QUEUE_SIZE;
  u32 desc_idx = rx_used->ring[used_idx].id;
  u32 pkt_len = rx_used->ring[used_idx].len;
  rx_last_used_idx++;

  // Bounds check on descriptor index
  if (desc_idx >= NUM_RX_BUFS)
    return -1;

  // ACK interrupt
  u32 isr = mmio(REG_INT_STATUS);
  mmio(REG_INT_ACK) = isr;
  barrier();

  // Copy data if we have a real packet (skip virtio-net header)
  i32 result = -1;
  if (pkt_len > NET_HDR_SIZE) {
    u32 data_len = pkt_len - NET_HDR_SIZE;
    if (data_len > buf_size)
      data_len = buf_size;
    str::memcpy(buf, rx_bufs[desc_idx] + NET_HDR_SIZE, data_len);
    result = static_cast<i32>(data_len);
  }

  // Re-post this RX buffer
  rx_descs[desc_idx].addr = reinterpret_cast<u64>(rx_bufs[desc_idx]);
  rx_descs[desc_idx].len = PKT_BUF_SIZE;
  rx_descs[desc_idx].flags = DESC_F_WRITE;
  rx_descs[desc_idx].next = 0;
  barrier();
  rx_avail->ring[rx_avail->idx % QUEUE_SIZE] = static_cast<u16>(desc_idx);
  barrier();
  rx_avail->idx++;
  barrier();
  mmio(REG_QUEUE_NOTIFY) = 0;

  return result;
}

} // namespace netdev
