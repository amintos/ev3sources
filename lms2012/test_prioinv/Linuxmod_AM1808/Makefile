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

install
	sudo mount /dev/sdb1 /media/LMS2012; \
	sudo mount /dev/sdb2 /media/LMS2012_EXT; \
        cp test_prioinv.ko /media/LMS2012_EXT/home/root/test_prioinv.ko; \
        sudo umount /dev/sdb1; \
        sudo umount /dev/sdb2; 
