# IP Display Driver Makefile

obj-m += ipdisp.o
ipdisp-objs := ipdisp_main.o ipdisp_drm.o ipdisp_network.o ipdisp_encoder.o

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Build flags
ccflags-y := -DDEBUG -g -Wall -Wextra

# Default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Install target
install: all
	sudo insmod ipdisp.ko

# Uninstall target
uninstall:
	sudo rmmod ipdisp

# Test target
test: install
	@echo "Testing IP Display Driver..."
	@ls -la /dev/dri/ | grep -E "card[0-9]+" || echo "No DRM devices found"
	@dmesg | tail -n 20 | grep ipdisp || echo "No kernel messages"

# Debug target
debug: install
	@echo "=== IP Display Driver Debug Info ==="
	@echo "Loaded modules:"
	@lsmod | grep ipdisp
	@echo "\nDRM devices:"
	@ls -la /dev/dri/
	@echo "\nKernel messages:"
	@dmesg | grep ipdisp | tail -n 10
	@echo "\nModule info:"
	@modinfo ipdisp.ko

# Development helpers
reload: uninstall install

check:
	@echo "Checking kernel headers..."
	@ls $(KDIR) > /dev/null && echo "Kernel headers found" || echo "ERROR: Kernel headers not found"
	@echo "Checking required symbols..."
	@grep -q "CONFIG_DRM=" $(KDIR)/.config && echo "DRM support enabled" || echo "WARNING: DRM support may not be enabled"

.PHONY: all clean install uninstall test debug reload check
