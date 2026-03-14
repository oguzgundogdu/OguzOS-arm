#include "fb.h"
#include "string.h"
#include "uart.h"

namespace {

// QEMU virt machine fw_cfg MMIO
constexpr u64 FWCFG_BASE = 0x09020000;
constexpr u64 FWCFG_DATA = FWCFG_BASE + 0x000;
constexpr u64 FWCFG_SEL = FWCFG_BASE + 0x008;
constexpr u64 FWCFG_DMA = FWCFG_BASE + 0x010;

// Framebuffer lives at a fixed address in guest RAM
constexpr u64 FB_ADDR = 0x46000000;
constexpr u32 FB_WIDTH = 1920;
constexpr u32 FB_HEIGHT = 1080;
constexpr u32 FB_BPP = 4;
constexpr u32 FB_STRIDE = FB_WIDTH * FB_BPP;

constexpr u32 FOURCC_XRGB8888 = 0x34325258;

bool initialized = false;

// ── Byte-swap helpers (ARM64 is LE, fw_cfg DMA structs are BE) ──────────────
u16 to_be16(u16 v) {
  return static_cast<u16>(((v >> 8) & 0xFF) | ((v & 0xFF) << 8));
}

u32 to_be32(u32 v) {
  return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) |
         ((v >> 24) & 0xFF);
}

u64 to_be64(u64 v) {
  return (static_cast<u64>(to_be32(static_cast<u32>(v))) << 32) |
         to_be32(static_cast<u32>(v >> 32));
}

// ── fw_cfg byte-level read (still works in modern QEMU) ─────────────────────
u8 fwcfg_read_byte() {
  return *reinterpret_cast<volatile u8 *>(FWCFG_DATA);
}

void fwcfg_select(u16 selector) {
  *reinterpret_cast<volatile u16 *>(FWCFG_SEL) = to_be16(selector);
}

u16 fwcfg_read_be16() {
  u16 val = static_cast<u16>(fwcfg_read_byte()) << 8;
  val |= fwcfg_read_byte();
  return val;
}

u32 fwcfg_read_be32() {
  u32 val = static_cast<u32>(fwcfg_read_byte()) << 24;
  val |= static_cast<u32>(fwcfg_read_byte()) << 16;
  val |= static_cast<u32>(fwcfg_read_byte()) << 8;
  val |= fwcfg_read_byte();
  return val;
}

// ── fw_cfg DMA write (required for modern QEMU, data port writes are no-ops) ─
// DMA access struct: all fields big-endian in guest memory
struct FWCfgDmaAccess {
  u32 control;
  u32 length;
  u64 address;
} __attribute__((packed));

// FW_CFG_DMA control bits
constexpr u32 FW_CFG_DMA_SELECT = 0x08;
constexpr u32 FW_CFG_DMA_WRITE = 0x10;
constexpr u32 FW_CFG_DMA_ERROR = 0x01;

bool fwcfg_dma_write(u16 selector, const void *data, u32 len) {
  // Prepare DMA access struct (must be in memory visible to QEMU)
  alignas(64) volatile FWCfgDmaAccess dma;
  u32 ctrl = (static_cast<u32>(selector) << 16) | FW_CFG_DMA_SELECT |
             FW_CFG_DMA_WRITE;
  dma.control = to_be32(ctrl);
  dma.length = to_be32(len);
  dma.address = to_be64(reinterpret_cast<u64>(data));

  // Ensure DMA struct and data buffer are fully written to memory
  asm volatile("dsb sy" ::: "memory");

  // Write physical address of DMA struct to the DMA register.
  // The DMA MMIO region is DEVICE_BIG_ENDIAN in QEMU, so QEMU byte-swaps
  // values written by an LE guest. We pre-swap so the handler sees the
  // correct address.
  u64 dma_addr = reinterpret_cast<u64>(&dma);
  volatile u32 *dma_hi = reinterpret_cast<volatile u32 *>(FWCFG_DMA);
  volatile u32 *dma_lo = reinterpret_cast<volatile u32 *>(FWCFG_DMA + 4);

  // High 32 bits first; low 32 bits triggers the DMA transfer
  *dma_hi = to_be32(static_cast<u32>(dma_addr >> 32));
  *dma_lo = to_be32(static_cast<u32>(dma_addr));

  // DMA is synchronous in QEMU — transfer completes before write returns.
  // Barrier to ensure we see QEMU's write-back to dma.control.
  asm volatile("dsb sy" ::: "memory");

  // QEMU clears control to 0 on success, or sets the error bit
  return (dma.control == 0);
}

// ── Search fw_cfg file directory for "etc/ramfb" ────────────────────────────
i32 find_ramfb_selector() {
  fwcfg_select(0x0019); // file directory
  u32 count = fwcfg_read_be32();

  for (u32 i = 0; i < count; i++) {
    u32 size = fwcfg_read_be32();
    u16 select = fwcfg_read_be16();
    fwcfg_read_be16(); // reserved
    (void)size;

    char name[56];
    for (int j = 0; j < 56; j++)
      name[j] = static_cast<char>(fwcfg_read_byte());

    if (str::cmp(name, "etc/ramfb") == 0)
      return static_cast<i32>(select);
  }
  return -1;
}

} // anonymous namespace

namespace fb {

bool init() {
  i32 selector = find_ramfb_selector();
  if (selector < 0) {
    uart::puts("  [fb]   ramfb not found\n");
    return false;
  }

  // Clear framebuffer memory
  str::memset(reinterpret_cast<void *>(FB_ADDR), 0,
              FB_WIDTH * FB_HEIGHT * FB_BPP);

  // Prepare ramfb config struct (28 bytes, all fields big-endian)
  struct RAMFBCfg {
    u64 addr;
    u32 fourcc;
    u32 flags;
    u32 width;
    u32 height;
    u32 stride;
  } __attribute__((packed));

  alignas(64) RAMFBCfg cfg;
  cfg.addr = to_be64(FB_ADDR);
  cfg.fourcc = to_be32(FOURCC_XRGB8888);
  cfg.flags = 0;
  cfg.width = to_be32(FB_WIDTH);
  cfg.height = to_be32(FB_HEIGHT);
  cfg.stride = to_be32(FB_STRIDE);

  // Write config via DMA (byte-by-byte writes removed in QEMU 2.4+)
  if (!fwcfg_dma_write(static_cast<u16>(selector), &cfg, sizeof(cfg))) {
    uart::puts("  [fb]   ramfb DMA write failed\n");
    return false;
  }

  initialized = true;
  uart::puts("  [fb]   ramfb: 1920x1080x32\n");
  return true;
}

bool is_available() { return initialized; }

u32 *buffer() { return reinterpret_cast<u32 *>(FB_ADDR); }

u32 width() { return FB_WIDTH; }

u32 height() { return FB_HEIGHT; }

} // namespace fb
