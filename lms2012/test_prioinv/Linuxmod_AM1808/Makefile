ifneq ($(KERNELRELEASE),)

# kbuild part of makefile
obj-m += test_prioinv.o

else

# normal makefile

TARGET = test_prioinv.ko

CONF = Linuxmod
ARCH = AM1808

include ../../open_first/rules.mk

endif

