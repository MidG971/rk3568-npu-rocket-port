obj-m := npu_probe.o

# Kernel build dir: prefer the standard symlink, fall back to the in-tree source
# used to build this running kernel (see CLAUDE.md: ~/npu-port/linux-mainline).
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
