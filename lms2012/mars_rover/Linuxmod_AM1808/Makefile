ifneq ($(KERNELRELEASE),)

# kbuild part of makefile
obj-m += mars_rover.o

else

# normal makefile
TARGET = mars_rover.ko

CONF = Linuxmod
ARCH = AM1808

include ../../open_first/rules.mk

CFLAGS += -msoft-float

endif

