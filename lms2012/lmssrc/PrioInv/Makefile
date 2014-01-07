
#main: prioInv

#prioInv: pri-inv.c
#	arm-none-linux-gnueabi-gcc -o priorityInversion -lrt -lpthread -D_GNU_SOURCE -I../../../extra/linux-03.20.00.13/include priorityInversion.c

#install:
#	sudo cp priorityInversion /media/LMS2012_EXT/home/root; \

obj-m := priorityInversion.o
KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules  
