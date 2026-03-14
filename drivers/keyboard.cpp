#include "keyboard.h"
#include "string.h"
#include "syslog.h"
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
constexpr u32 QUEUE_SIZE = 16;
constexpr u32 PAGE_SIZE = 4096;

// Virtio-input event structure
struct VirtioInputEvent {
  u16 type;
  u16 code;
  u32 value;
} __attribute__((packed));

// Linux input event types
constexpr u16 EV_KEY = 0x01;
constexpr u16 EV_ABS = 0x03;

// Virtio-input config selectors
constexpr u8 VIRTIO_INPUT_CFG_EV_BITS = 0x11;

// Linux keycodes for arrow keys
constexpr u16 KEY_UP = 103;
constexpr u16 KEY_DOWN = 108;
constexpr u16 KEY_LEFT = 105;
constexpr u16 KEY_RIGHT = 106;

// Modifier keycodes
constexpr u16 KEY_LEFTSHIFT = 42;
constexpr u16 KEY_RIGHTSHIFT = 54;
constexpr u16 KEY_LEFTCTRL = 29;
constexpr u16 KEY_RIGHTCTRL = 97;

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

// Event queue memory
alignas(4096) u8 kb_eq_mem[2 * PAGE_SIZE];
VirtqDesc *kb_eq_descs;
VirtqAvail *kb_eq_avail;
VirtqUsed *kb_eq_used;
u16 kb_eq_last_used_idx = 0;

// Pre-posted event buffers
constexpr u32 NUM_EVT_BUFS = 8;
VirtioInputEvent kb_evt_bufs[NUM_EVT_BUFS];

u64 kb_base_addr = 0;
bool kb_initialized = false;

// Modifier state
bool shift_held = false;
bool ctrl_held = false;

// Key buffer (circular)
constexpr i32 KEY_BUF_SIZE = 32;
char key_buf[KEY_BUF_SIZE];
i32 key_buf_head = 0;
i32 key_buf_tail = 0;

// Arrow buffer (circular)
char arrow_buf[KEY_BUF_SIZE];
i32 arrow_buf_head = 0;
i32 arrow_buf_tail = 0;

// Keycode → ASCII mapping (US layout, unshifted)
// Index = Linux keycode, value = ASCII char (0 = no mapping)
const char keymap_lower[128] = {
    0,    0x1B, '1',  '2',  '3', '4', '5', '6',  // 0-7
    '7',  '8',  '9',  '0',  '-', '=', 0x7F, '\t', // 8-15
    'q',  'w',  'e',  'r',  't', 'y', 'u',  'i',  // 16-23
    'o',  'p',  '[',  ']',  '\r', 0,  'a',  's',  // 24-31
    'd',  'f',  'g',  'h',  'j', 'k', 'l',  ';',  // 32-39
    '\'', '`',  0,    '\\', 'z', 'x', 'c',  'v',  // 40-47
    'b',  'n',  'm',  ',',  '.', '/', 0,    0,    // 48-55
    0,    ' ',  0,    0,    0,   0,   0,    0,    // 56-63
    0,    0,    0,    0,    0,   0,   0,    0,    // 64-71
    0,    0,    0,    0,    0,   0,   0,    0,    // 72-79
    0,    0,    0,    0,    0,   0,   0,    0,    // 80-87
    0,    0,    0,    0,    0,   0,   0,    0,    // 88-95
    0,    0,    0,    0,    0,   0,   0,    0,    // 96-103
    0,    0,    0,    0,    0,   0,   0,    0,    // 104-111
    0,    0,    0,    0,    0,   0,   0,    0,    // 112-119
    0,    0,    0,    0,    0,   0,   0,    0,    // 120-127
};

const char keymap_upper[128] = {
    0,    0x1B, '!',  '@',  '#', '$', '%', '^',  // 0-7
    '&',  '*',  '(',  ')',  '_', '+', 0x7F, '\t', // 8-15
    'Q',  'W',  'E',  'R',  'T', 'Y', 'U',  'I',  // 16-23
    'O',  'P',  '{',  '}',  '\r', 0,  'A',  'S',  // 24-31
    'D',  'F',  'G',  'H',  'J', 'K', 'L',  ':',  // 32-39
    '"',  '~',  0,    '|',  'Z', 'X', 'C',  'V',  // 40-47
    'B',  'N',  'M',  '<',  '>', '?', 0,    0,    // 48-55
    0,    ' ',  0,    0,    0,   0,   0,    0,    // 56-63
    0,    0,    0,    0,    0,   0,   0,    0,    // 64-71
    0,    0,    0,    0,    0,   0,   0,    0,    // 72-79
    0,    0,    0,    0,    0,   0,   0,    0,    // 80-87
    0,    0,    0,    0,    0,   0,   0,    0,    // 88-95
    0,    0,    0,    0,    0,   0,   0,    0,    // 96-103
    0,    0,    0,    0,    0,   0,   0,    0,    // 104-111
    0,    0,    0,    0,    0,   0,   0,    0,    // 112-119
    0,    0,    0,    0,    0,   0,   0,    0,    // 120-127
};

volatile u32 &mmio(u32 offset) {
  return *reinterpret_cast<volatile u32 *>(kb_base_addr + offset);
}

void barrier() { asm volatile("dmb ish" ::: "memory"); }

void push_key(char c) {
  i32 next = (key_buf_head + 1) % KEY_BUF_SIZE;
  if (next != key_buf_tail) {
    key_buf[key_buf_head] = c;
    key_buf_head = next;
  }
}

void push_arrow(char dir) {
  i32 next = (arrow_buf_head + 1) % KEY_BUF_SIZE;
  if (next != arrow_buf_tail) {
    arrow_buf[arrow_buf_head] = dir;
    arrow_buf_head = next;
  }
}

void post_event_buffers() {
  for (u32 i = 0; i < NUM_EVT_BUFS; i++) {
    kb_eq_descs[i].addr = reinterpret_cast<u64>(&kb_evt_bufs[i]);
    kb_eq_descs[i].len = sizeof(VirtioInputEvent);
    kb_eq_descs[i].flags = DESC_F_WRITE;
    kb_eq_descs[i].next = 0;
    kb_eq_avail->ring[(kb_eq_avail->idx + i) % QUEUE_SIZE] =
        static_cast<u16>(i);
  }
  barrier();
  kb_eq_avail->idx += NUM_EVT_BUFS;
  barrier();
  mmio(REG_QUEUE_NOTIFY) = 0;
}

// Check if a virtio-input device at addr is a keyboard (not a tablet).
// Tablets report EV_ABS capability; keyboards don't.
bool is_keyboard_device(u64 addr) {
  volatile u8 *cfg = reinterpret_cast<volatile u8 *>(addr + REG_CONFIG);
  // Select EV_ABS (0x03) event bits
  cfg[0] = VIRTIO_INPUT_CFG_EV_BITS; // select
  cfg[1] = EV_ABS;                    // subsel
  asm volatile("dmb ish" ::: "memory");
  u8 size = cfg[2]; // size of response
  // If size > 0, device supports absolute axes → it's a tablet, not keyboard
  return (size == 0);
}

bool init_device(u64 addr) {
  kb_base_addr = addr;

  u32 magic = mmio(REG_MAGIC);
  if (magic != 0x74726976)
    return false;

  u32 version = mmio(REG_VERSION);
  u32 dev_id = mmio(REG_DEVICE_ID);
  if (dev_id != 18) // 18 = virtio-input
    return false;

  // Check if this is a keyboard (not a tablet)
  if (!is_keyboard_device(addr))
    return false;

  // Clear queue memory and set up pointers
  str::memset(kb_eq_mem, 0, sizeof(kb_eq_mem));
  kb_eq_descs = reinterpret_cast<VirtqDesc *>(kb_eq_mem);
  kb_eq_avail =
      reinterpret_cast<VirtqAvail *>(kb_eq_mem + QUEUE_SIZE * sizeof(VirtqDesc));
  kb_eq_used = reinterpret_cast<VirtqUsed *>(kb_eq_mem + PAGE_SIZE);

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

    u64 pfn = reinterpret_cast<u64>(kb_eq_mem) / PAGE_SIZE;
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

    u64 d = reinterpret_cast<u64>(kb_eq_descs);
    u64 a = reinterpret_cast<u64>(kb_eq_avail);
    u64 u = reinterpret_cast<u64>(kb_eq_used);

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
  kb_eq_last_used_idx = 0;
  post_event_buffers();

  kb_initialized = true;
  syslog::debug("kbd", "virtqueue ready, %d event buffers posted", NUM_EVT_BUFS);
  return true;
}

void handle_key_event(u16 code, u32 value) {
  // Track modifier state
  if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
    shift_held = (value != 0);
    return;
  }
  if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
    ctrl_held = (value != 0);
    return;
  }

  // Only handle key press (value=1) and repeat (value=2), not release (value=0)
  if (value == 0)
    return;

  // Arrow keys
  if (code == KEY_UP) { push_arrow('A'); return; }
  if (code == KEY_DOWN) { push_arrow('B'); return; }
  if (code == KEY_RIGHT) { push_arrow('C'); return; }
  if (code == KEY_LEFT) { push_arrow('D'); return; }

  // Map keycode to ASCII
  if (code >= 128)
    return;

  char c;
  if (ctrl_held) {
    // Ctrl+letter → control character (e.g. Ctrl+S = 0x13)
    c = keymap_lower[code];
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 1);
    else
      return; // ignore non-letter ctrl combos
  } else {
    c = shift_held ? keymap_upper[code] : keymap_lower[code];
  }

  if (c != 0) {
    syslog::debug("kbd", "key code=%d -> '%c' (0x%x)", static_cast<int>(code),
                  static_cast<int>(c), static_cast<unsigned int>(c));
    push_key(c);
  }
}

} // anonymous namespace

namespace keyboard {

bool init() {
  for (u32 i = 0; i < VIRTIO_COUNT; i++) {
    u64 addr = VIRTIO_BASE + i * VIRTIO_STRIDE;
    if (init_device(addr)) {
      uart::puts("  [keyboard] virtio-keyboard\n");
      return true;
    }
  }
  return false;
}

bool is_available() { return kb_initialized; }

void poll() {
  if (!kb_initialized)
    return;

  barrier();
  while (kb_eq_used->idx != kb_eq_last_used_idx) {
    u16 used_idx = kb_eq_last_used_idx % QUEUE_SIZE;
    u32 desc_idx = kb_eq_used->ring[used_idx].id;
    kb_eq_last_used_idx++;

    if (desc_idx >= NUM_EVT_BUFS)
      continue;

    VirtioInputEvent &evt = kb_evt_bufs[desc_idx];

    if (evt.type == EV_KEY)
      handle_key_event(evt.code, evt.value);

    // Re-post this event buffer
    kb_eq_descs[desc_idx].addr = reinterpret_cast<u64>(&kb_evt_bufs[desc_idx]);
    kb_eq_descs[desc_idx].len = sizeof(VirtioInputEvent);
    kb_eq_descs[desc_idx].flags = DESC_F_WRITE;
    kb_eq_descs[desc_idx].next = 0;
    barrier();
    kb_eq_avail->ring[kb_eq_avail->idx % QUEUE_SIZE] =
        static_cast<u16>(desc_idx);
    barrier();
    kb_eq_avail->idx++;
    barrier();
    mmio(REG_QUEUE_NOTIFY) = 0;
  }

  // ACK any pending interrupts
  u32 isr = mmio(REG_INT_STATUS);
  if (isr)
    mmio(REG_INT_ACK) = isr;
}

bool get_key(char &key) {
  if (key_buf_tail == key_buf_head)
    return false;
  key = key_buf[key_buf_tail];
  key_buf_tail = (key_buf_tail + 1) % KEY_BUF_SIZE;
  return true;
}

bool get_arrow(char &dir) {
  if (arrow_buf_tail == arrow_buf_head)
    return false;
  dir = arrow_buf[arrow_buf_tail];
  arrow_buf_tail = (arrow_buf_tail + 1) % KEY_BUF_SIZE;
  return true;
}

} // namespace keyboard
