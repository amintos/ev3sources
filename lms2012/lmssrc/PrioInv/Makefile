ARCH = AM1808
BASE = /home/patrick/projects/lms2012
KERNEL = $(BASE)/../extra/linux-03.20.00.13
UBOOT = $(BASE)/../extra/uboot-03.20.00.13

default: modules

KDIR ?= $(BASE)/../extra/linux-03.20.00.13
include $(BASE)/open_first/rules.mk

obj-m := priorityInversion.o
PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules  
