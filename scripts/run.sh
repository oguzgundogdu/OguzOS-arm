#!/bin/bash
# Quick launcher for OguzOS in QEMU (same as UTM uses internally)

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

KERNEL_BIN="build/oguzos.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Building OguzOS..."
    make
fi

echo "Starting OguzOS... (Press Ctrl+A then X to exit)"
echo ""

# Create disk image if it doesn't exist
if [ ! -f "disk.img" ]; then
    echo "Creating 4MB disk image..."
    dd if=/dev/zero of=disk.img bs=1m count=4 2>/dev/null
fi

qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 128M \
    -nographic \
    -kernel "$KERNEL_BIN" \
    -drive file=disk.img,if=none,id=hd0,format=raw \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=net0 \
    -device virtio-net-device,netdev=net0
