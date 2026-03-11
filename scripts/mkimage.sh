#!/bin/bash
# Creates a bootable disk image for OguzOS (ARM64 / UTM)
# This generates oguzos.img which can be imported into UTM

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

KERNEL_BIN="build/oguzos.bin"
DISK_IMG="oguzos.img"
IMG_SIZE_MB=64

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: $KERNEL_BIN not found. Run 'make' first."
    exit 1
fi

echo "=== Creating OguzOS bootable disk image ==="

# Create a raw disk image
echo "[1/4] Creating ${IMG_SIZE_MB}MB raw disk image..."
dd if=/dev/zero of="$DISK_IMG" bs=1m count=$IMG_SIZE_MB 2>/dev/null

# Write the kernel binary at the start of the disk
echo "[2/4] Writing kernel to image..."
dd if="$KERNEL_BIN" of="$DISK_IMG" conv=notrunc 2>/dev/null

# Get kernel size for info
KERN_SIZE=$(wc -c < "$KERNEL_BIN" | tr -d ' ')

echo "[3/4] Image created successfully!"
echo ""
echo "========================================="
echo "  Image: $DISK_IMG"
echo "  Size:  ${IMG_SIZE_MB}MB"
echo "  Kernel: ${KERN_SIZE} bytes"
echo "========================================="
echo ""
echo "[4/4] UTM Setup Instructions:"
echo ""
echo "  1. Open UTM → 'Create a New Virtual Machine'"
echo "  2. Select 'Emulate'"
echo "  3. Select 'Other'"
echo "  4. Skip the boot ISO/image (click 'Continue')"
echo "  5. Storage: skip or set to 0 (we don't need it)"
echo "  6. Name it 'OguzOS' and save"
echo "  7. Right-click the VM → 'Edit'"
echo "  8. In 'System':"
echo "     - Architecture: ARM64 (aarch64)"
echo "     - System: virt"
echo "     - Memory: 128 MB"
echo "  9. In 'QEMU':"
echo "     - Uncheck 'UEFI Boot'"
echo "     - Add this line to QEMU arguments:"
echo ""
echo "       -kernel $PROJECT_DIR/$KERNEL_BIN"
echo ""
echo " 10. In 'Display': change to 'Serial'"
echo " 11. Click 'Save' and boot!"
echo ""
echo " === OR use QEMU directly: ==="
echo "   $PROJECT_DIR/scripts/run.sh"
echo ""
