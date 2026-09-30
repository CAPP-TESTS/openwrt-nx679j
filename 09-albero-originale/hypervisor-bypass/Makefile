obj-m += kexec_injector.o

KDIR ?= /home/user/nx679j-stock/kernel-patch-test/linux-6.6.60
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
BUILD_DIR ?= build
USER_CC := $(CROSS_COMPILE)gcc
USER_CFLAGS ?= -O2 -Wall -Wextra
USER_LDFLAGS ?= -static

.PHONY: all module userspace build-all check clean

all: module

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

userspace: $(BUILD_DIR)/hyp_attack

$(BUILD_DIR)/hyp_attack: hyp_attack.c
	mkdir -p $(BUILD_DIR)
	$(USER_CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

build-all: module userspace

check:
	test -d "$(KDIR)"
	test -x "$$(command -v "$(USER_CC)")"

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -rf "$(BUILD_DIR)"
