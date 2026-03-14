# OguzOS - ARM64 Minimal Operating System
# Targets QEMU virt machine / UTM on macOS

# Detect cross-compiler prefix
# On macOS with Homebrew: aarch64-elf-* or aarch64-none-elf-*
# On Linux: aarch64-linux-gnu-* or aarch64-none-elf-*
CROSS ?= aarch64-elf-

# Check if the cross compiler exists, try alternatives
ifeq ($(shell which $(CROSS)g++ 2>/dev/null),)
  CROSS := aarch64-none-elf-
endif
ifeq ($(shell which $(CROSS)g++ 2>/dev/null),)
  CROSS := aarch64-linux-gnu-
endif

CC      = $(CROSS)gcc
CXX     = $(CROSS)g++
AS      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

# Directory layout
ARCH_DIR    = arch
KERNEL_DIR  = kernel
DRIVERS_DIR = drivers
FS_DIR      = fs
SHELL_DIR   = shell
LIB_DIR     = lib
NET_DIR     = net
GUI_DIR     = gui
APPS_DIR    = apps
BUILD_DIR   = build

# Include paths (so #include "file.h" works across directories)
INCLUDES = -I$(ARCH_DIR) -I$(KERNEL_DIR) -I$(DRIVERS_DIR) -I$(FS_DIR) \
           -I$(SHELL_DIR) -I$(LIB_DIR) -I$(NET_DIR) -I$(GUI_DIR) -I$(APPS_DIR)

# Flags for freestanding C++ (no standard library)
COMMON_FLAGS = -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only \
               -mstrict-align -Wall -Wextra -O2

ASFLAGS = $(COMMON_FLAGS)
CXXFLAGS = $(COMMON_FLAGS) $(INCLUDES) -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -fno-use-cxa-atexit -std=c++17

LDFLAGS = -T $(ARCH_DIR)/linker.ld -nostdlib

# Object files (listed explicitly to avoid basename collisions)
OBJS = $(BUILD_DIR)/boot.o \
       $(BUILD_DIR)/exception.o \
       $(BUILD_DIR)/uart.o \
       $(BUILD_DIR)/disk.o \
       $(BUILD_DIR)/netdev.o \
       $(BUILD_DIR)/string.o \
       $(BUILD_DIR)/syslog.o \
       $(BUILD_DIR)/fs.o \
       $(BUILD_DIR)/netstack.o \
       $(BUILD_DIR)/fb.o \
       $(BUILD_DIR)/mouse.o \
       $(BUILD_DIR)/keyboard.o \
       $(BUILD_DIR)/graphics.o \
       $(BUILD_DIR)/gui.o \
       $(BUILD_DIR)/registry.o \
       $(BUILD_DIR)/notepad.o \
       $(BUILD_DIR)/terminal.o \
       $(BUILD_DIR)/taskman.o \
       $(BUILD_DIR)/settingsapp.o \
       $(BUILD_DIR)/settings.o \
       $(BUILD_DIR)/env.o \
       $(BUILD_DIR)/assoc.o \
       $(BUILD_DIR)/menu.o \
       $(BUILD_DIR)/shell.o \
       $(BUILD_DIR)/kernel.o

TARGET = oguzos
KERNEL_ELF = $(BUILD_DIR)/$(TARGET).elf
KERNEL_BIN = $(BUILD_DIR)/$(TARGET).bin

.PHONY: all clean run gui debug dump distclean

all: $(KERNEL_BIN)

$(KERNEL_ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  OguzOS built successfully!"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "  Size: $$(wc -c < $(KERNEL_BIN)) bytes"
	@echo "========================================="
	@echo ""

# Assembly rules
$(BUILD_DIR)/boot.o: $(ARCH_DIR)/boot.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: $(ARCH_DIR)/exception.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

# C++ rules
$(BUILD_DIR)/uart.o: $(DRIVERS_DIR)/uart.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/disk.o: $(DRIVERS_DIR)/disk.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/netdev.o: $(DRIVERS_DIR)/net.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/fb.o: $(DRIVERS_DIR)/fb.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/mouse.o: $(DRIVERS_DIR)/mouse.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: $(LIB_DIR)/string.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/syslog.o: $(LIB_DIR)/syslog.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/fs.o: $(FS_DIR)/fs.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/netstack.o: $(NET_DIR)/net.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: $(DRIVERS_DIR)/keyboard.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/graphics.o: $(GUI_DIR)/graphics.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/gui.o: $(GUI_DIR)/gui.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/registry.o: $(APPS_DIR)/registry.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/notepad.o: $(APPS_DIR)/notepad.ogz.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal.o: $(APPS_DIR)/terminal.ogz.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/taskman.o: $(APPS_DIR)/taskman.ogz.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/settingsapp.o: $(APPS_DIR)/settings.ogz.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/settings.o: $(LIB_DIR)/settings.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/env.o: $(LIB_DIR)/env.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/assoc.o: $(LIB_DIR)/assoc.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/menu.o: $(LIB_DIR)/menu.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: $(SHELL_DIR)/shell.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(KERNEL_DIR)/kernel.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Create disk image if it doesn't exist
DISK_IMG = disk.img
DISK_SIZE_MB = 4

$(DISK_IMG):
	dd if=/dev/zero of=$@ bs=1m count=$(DISK_SIZE_MB) 2>/dev/null
	@echo "Created $(DISK_IMG) ($(DISK_SIZE_MB) MB)"

# Run with QEMU — text-only mode (no GUI)
run: $(KERNEL_BIN) $(DISK_IMG)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 512M \
		-nographic \
		-kernel $(KERNEL_BIN) \
		-drive file=$(DISK_IMG),if=none,id=hd0,format=raw \
		-device virtio-blk-device,drive=hd0 \
		-netdev user,id=net0 \
		-device virtio-net-device,netdev=net0

# Run with QEMU — graphical mode (ramfb + mouse, type 'gui' in shell)
gui: $(KERNEL_BIN) $(DISK_IMG)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 512M \
		-serial stdio \
		-kernel $(KERNEL_BIN) \
		-drive file=$(DISK_IMG),if=none,id=hd0,format=raw \
		-device virtio-blk-device,drive=hd0 \
		-netdev user,id=net0 \
		-device virtio-net-device,netdev=net0 \
		-device ramfb \
		-device virtio-tablet-device \
		-device virtio-keyboard-device

# Run with QEMU and GDB server
debug: $(KERNEL_BIN)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 512M \
		-nographic \
		-kernel $(KERNEL_BIN) \
		-drive file=$(DISK_IMG),if=none,id=hd0,format=raw \
		-device virtio-blk-device,drive=hd0 \
		-netdev user,id=net0 \
		-device virtio-net-device,netdev=net0 \
		-S -s

# Dump disassembly
dump: $(KERNEL_ELF)
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD_DIR)

# Remove disk image too
distclean: clean
	rm -f $(DISK_IMG)
