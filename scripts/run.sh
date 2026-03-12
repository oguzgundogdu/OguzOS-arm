#!/bin/bash
# Quick launcher for OguzOS in QEMU (same as UTM uses internally)
# Usage: ./run.sh        — text-only mode
#        ./run.sh gui    — graphical mode (ramfb + mouse, type 'gui' in shell)

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

KERNEL_BIN="build/oguzos.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Building OguzOS..."
    make
fi

# Create disk image if it doesn't exist
if [ ! -f "disk.img" ]; then
    echo "Creating 4MB disk image..."
    dd if=/dev/zero of=disk.img bs=1m count=4 2>/dev/null
fi

COMMON_ARGS=(
    -machine virt
    -cpu cortex-a72
    -m 256M
    -kernel "$KERNEL_BIN"
    -drive file=disk.img,if=none,id=hd0,format=raw
    -device virtio-blk-device,drive=hd0
    -netdev user,id=net0
    -device virtio-net-device,netdev=net0
)

if [ "$1" = "gui" ]; then
    echo "Starting OguzOS (GUI mode)... Type 'gui' in the shell to launch desktop."
    echo ""
    qemu-system-aarch64 \
        "${COMMON_ARGS[@]}" \
        -serial stdio \
        -device ramfb \
        -device virtio-tablet-device \
        -device virtio-keyboard-device
else
    echo "Starting OguzOS... (Press Ctrl+A then X to exit)"
    echo ""
    qemu-system-aarch64 \
        "${COMMON_ARGS[@]}" \
        -nographic
fi
