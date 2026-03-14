#include "mouse.h"
#include "fb.h"
#include "string.h"
#include "uart.h"

namespace {

// QEMU virt machine virtio-mmio bus
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

constexpr u32 STATUS_ACK = 1;
constexpr u32 STATUS_DRIVER = 2;
constexpr u32 STATUS_DRIVER_OK = 4;
constexpr u32 STATUS_FEATURES_OK = 8;

constexpr u16 DESC_F_WRITE = 2;
constexpr u32 QUEUE_SIZE = 128;
constexpr u32 PAGE_SIZE = 4096;

// Virtio-input event structure
struct VirtioInputEvent {
  u16 type;
  u16 code;
  u32 value;
} __attribute__((packed));

// Linux input event types
constexpr u16 EV_KEY = 0x01;
constexpr u16 EV_REL = 0x02;
constexpr u16 EV_ABS = 0x03;

// Button codes
constexpr u16 BTN_LEFT = 0x110;
constexpr u16 BTN_RIGHT = 0x111;

// Absolute axis codes
constexpr u16 ABS_X = 0x00;
constexpr u16 ABS_Y = 0x01;

// Relative axis codes (scroll wheel)
constexpr u16 REL_WHEEL = 0x08;

// Virtqueue structures
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

// Event queue memory (descs+avail in page 0, used ring in page 1)
alignas(4096) u8 eq_mem[2 * PAGE_SIZE];
VirtqDesc *eq_descs;
VirtqAvail *eq_avail;
VirtqUsed *eq_used;
u16 eq_last_used_idx = 0;

// Pre-posted event buffers
constexpr u32 NUM_EVT_BUFS = 64;
VirtioInputEvent evt_bufs[NUM_EVT_BUFS];

// Virtio-input config selectors
constexpr u8 VIRTIO_INPUT_CFG_ABS_INFO = 0x12;

// Axis range (read from device config)
u32 abs_x_max = 32767;
u32 abs_y_max = 32767;

u64 base_addr = 0;
bool initialized = false;

// Current mouse state
i32 cur_x = 320;
i32 cur_y = 240;
bool btn_left = false;
bool btn_right = false;
i32 scroll_accum = 0; // accumulated scroll delta since last poll

volatile u32 &mmio(u32 offset) {
  return *reinterpret_cast<volatile u32 *>(base_addr + offset);
}

void barrier() { asm volatile("dmb ish" ::: "memory"); }

void post_event_buffers() {
  for (u32 i = 0; i < NUM_EVT_BUFS; i++) {
    eq_descs[i].addr = reinterpret_cast<u64>(&evt_bufs[i]);
    eq_descs[i].len = sizeof(VirtioInputEvent);
    eq_descs[i].flags = DESC_F_WRITE;
    eq_descs[i].next = 0;
    eq_avail->ring[(eq_avail->idx + i) % QUEUE_SIZE] = static_cast<u16>(i);
  }
  barrier();
  eq_avail->idx += NUM_EVT_BUFS;
  barrier();
  mmio(REG_QUEUE_NOTIFY) = 0;
}

// Read the maximum value for an absolute axis from virtio-input config.
// The config returns a linux input_absinfo struct:
//   u32 value, min, max, fuzz, flat, resolution
u32 read_abs_max(u64 addr, u8 axis) {
  volatile u8 *cfg = reinterpret_cast<volatile u8 *>(addr + REG_CONFIG);
  cfg[0] = VIRTIO_INPUT_CFG_ABS_INFO; // select = abs info
  cfg[1] = axis;                       // subsel = axis code
  asm volatile("dmb ish" ::: "memory");
  u8 size = cfg[2];
  if (size < 12)
    return 32767; // fallback if config not available

  // Read max (u32 LE at offset 8 in the union data, which starts at cfg[8])
  // input_absinfo: value(4) + min(4) + max(4)
  // cfg[8..] = data area
  u32 max_val = static_cast<u32>(cfg[8 + 8]) |
                (static_cast<u32>(cfg[8 + 9]) << 8) |
                (static_cast<u32>(cfg[8 + 10]) << 16) |
                (static_cast<u32>(cfg[8 + 11]) << 24);
  return max_val > 0 ? max_val : 32767;
}

// Check if a virtio-input device is a tablet (has EV_ABS capability)
bool is_tablet_device(u64 addr) {
  volatile u8 *cfg = reinterpret_cast<volatile u8 *>(addr + REG_CONFIG);
  cfg[0] = 0x11; // VIRTIO_INPUT_CFG_EV_BITS (select)
  cfg[1] = EV_ABS; // subsel
  asm volatile("dmb ish" ::: "memory");
  u8 size = cfg[2];
  return (size > 0); // Has absolute axes → it's a tablet/mouse
}

bool init_device(u64 addr) {
  base_addr = addr;

  u32 magic = mmio(REG_MAGIC);
  if (magic != 0x74726976)
    return false;

  u32 version = mmio(REG_VERSION);
  u32 dev_id = mmio(REG_DEVICE_ID);
  if (dev_id != 18) // 18 = virtio-input
    return false;

  // Only claim tablet/mouse devices (not keyboards)
  if (!is_tablet_device(addr))
    return false;

  // Read axis ranges from device config
  abs_x_max = read_abs_max(addr, ABS_X);
  abs_y_max = read_abs_max(addr, ABS_Y);

  // Clear queue memory and set up pointers
  str::memset(eq_mem, 0, sizeof(eq_mem));
  eq_descs = reinterpret_cast<VirtqDesc *>(eq_mem);
  eq_avail =
      reinterpret_cast<VirtqAvail *>(eq_mem + QUEUE_SIZE * sizeof(VirtqDesc));
  eq_used = reinterpret_cast<VirtqUsed *>(eq_mem + PAGE_SIZE);

  // Reset
  mmio(REG_STATUS) = 0;
  barrier();

  // Acknowledge
  mmio(REG_STATUS) = STATUS_ACK;
  barrier();

  // Driver
  mmio(REG_STATUS) = STATUS_ACK | STATUS_DRIVER;
  barrier();

  // Accept no features
  mmio(REG_DRV_FEATURES) = 0;
  barrier();

  if (version == 1) {
    mmio(REG_GUEST_PAGE_SIZE) = PAGE_SIZE;
    barrier();

    mmio(REG_QUEUE_SEL) = 0;
    barrier();

    u32 max_q = mmio(REG_QUEUE_NUM_MAX);
    if (max_q == 0)
      return false;

    u32 qsz = (max_q < QUEUE_SIZE) ? max_q : QUEUE_SIZE;
    mmio(REG_QUEUE_NUM) = qsz;
    barrier();

    u64 pfn = reinterpret_cast<u64>(eq_mem) / PAGE_SIZE;
    mmio(REG_QUEUE_PFN) = static_cast<u32>(pfn);
    barrier();
  } else {
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

    u64 d = reinterpret_cast<u64>(eq_descs);
    u64 a = reinterpret_cast<u64>(eq_avail);
    u64 u = reinterpret_cast<u64>(eq_used);

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

  // Driver OK
  u32 ok_status = STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK;
  if (version == 2)
    ok_status |= STATUS_FEATURES_OK;
  mmio(REG_STATUS) = ok_status;
  barrier();

  // Pre-post event buffers
  eq_last_used_idx = 0;
  post_event_buffers();

  initialized = true;
  return true;
}

} // anonymous namespace

namespace mouse {

bool init() {
  for (u32 i = 0; i < VIRTIO_COUNT; i++) {
    u64 addr = VIRTIO_BASE + i * VIRTIO_STRIDE;
    if (init_device(addr)) {
      uart::puts("  [mouse] virtio-tablet\n");
      return true;
    }
  }
  return false;
}

bool is_available() { return initialized; }

bool poll(i32 &out_x, i32 &out_y, bool &out_left, bool &out_right,
          i32 &out_scroll) {
  if (!initialized) {
    out_x = cur_x;
    out_y = cur_y;
    out_left = false;
    out_right = false;
    out_scroll = 0;
    return false;
  }

  bool got_event = false;

  barrier();
  while (eq_used->idx != eq_last_used_idx) {
    u16 used_idx = eq_last_used_idx % QUEUE_SIZE;
    u32 desc_idx = eq_used->ring[used_idx].id;
    eq_last_used_idx++;

    if (desc_idx >= NUM_EVT_BUFS)
      continue;

    VirtioInputEvent &evt = evt_bufs[desc_idx];

    if (evt.type == EV_ABS) {
      u32 sw = fb::width();
      u32 sh = fb::height();
      if (evt.code == ABS_X) {
        cur_x = static_cast<i32>(static_cast<u64>(evt.value) * sw / abs_x_max);
        if (cur_x < 0)
          cur_x = 0;
        if (cur_x >= static_cast<i32>(sw))
          cur_x = static_cast<i32>(sw) - 1;
      } else if (evt.code == ABS_Y) {
        cur_y = static_cast<i32>(static_cast<u64>(evt.value) * sh / abs_y_max);
        if (cur_y < 0)
          cur_y = 0;
        if (cur_y >= static_cast<i32>(sh))
          cur_y = static_cast<i32>(sh) - 1;
      }
    } else if (evt.type == EV_REL) {
      if (evt.code == REL_WHEEL)
        scroll_accum += static_cast<i32>(evt.value);
    } else if (evt.type == EV_KEY) {
      if (evt.code == BTN_LEFT)
        btn_left = (evt.value != 0);
      else if (evt.code == BTN_RIGHT)
        btn_right = (evt.value != 0);
    }

    got_event = true;

    // Re-post this event buffer
    eq_descs[desc_idx].addr = reinterpret_cast<u64>(&evt_bufs[desc_idx]);
    eq_descs[desc_idx].len = sizeof(VirtioInputEvent);
    eq_descs[desc_idx].flags = DESC_F_WRITE;
    eq_descs[desc_idx].next = 0;
    barrier();
    eq_avail->ring[eq_avail->idx % QUEUE_SIZE] = static_cast<u16>(desc_idx);
    barrier();
    eq_avail->idx++;
    barrier();
    mmio(REG_QUEUE_NOTIFY) = 0;
  }

  // ACK any pending interrupts
  u32 isr = mmio(REG_INT_STATUS);
  if (isr)
    mmio(REG_INT_ACK) = isr;

  out_x = cur_x;
  out_y = cur_y;
  out_left = btn_left;
  out_right = btn_right;
  out_scroll = scroll_accum;
  scroll_accum = 0;

  return got_event;
}

} // namespace mouse
