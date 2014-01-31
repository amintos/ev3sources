ARCH = AM1808

BASE = ..

KERNEL = $(BASE)/../extra/linux-03.20.00.13
UBOOT = $(BASE)/../extra/uboot-03.20.00.13

KERNEL_CONF = LEGOBoard.config

default: help

include rules.mk

init:
	unlink ~/projects;
	ln -s /home/patrick/Documents/studium/eos/projects ~/projects;

#
# lms2012 program.
#

LIBS = c_com c_input c_memory c_output c_sound c_ui

lms2012:
	$(MAKE) -C $(BASE)/lms2012/Linux_$(ARCH)

lms2012.clean: $(LIBS:%=%.clean)
	$(MAKE) -C $(BASE)/lms2012/Linux_$(ARCH) clean uninstall

$(LIBS:%=%.clean): %.clean:
	$(MAKE) -C $(BASE)/$*/Linuxlib_$(ARCH) clean uninstall

.PHONY: lms2012 lms2012.clean $(LIBS:%=%.clean)

#
# mars rover control programs.
#

LIBS = c_com c_input c_memory c_output c_sound c_ui

control:
	$(MAKE) -C $(BASE)/motor_control/Linux_$(ARCH)
	$(MAKE) -C $(BASE)/sensor_control/Linux_$(ARCH)
	$(MAKE) -C $(BASE)/sound_control/Linux_$(ARCH)

control.clean: $(LIBS:%=%.clean)
	$(MAKE) -C $(BASE)/motor_control/Linux_$(ARCH) clean uninstall
	$(MAKE) -C $(BASE)/sensor_control/Linux_$(ARCH) clean uninstall
	$(MAKE) -C $(BASE)/sound_control/Linux_$(ARCH) clean uninstall

.PHONY: motor_control motor_control.clean $(LIBS:%=%.clean)

#
# lms2012 modules.
#

MODULES = mars_rover d_analog d_bt d_iic d_power d_pwm d_sound d_uart d_ui  \
	  test_prioinv d_usbdev d_usbhost

modules: $(MODULES)

$(MODULES):
	$(MAKE) -C $(BASE)/$@/Linuxmod_$(ARCH)

modules.clean: $(MODULES:%=%.clean)

$(MODULES:%=%.clean): %.clean:
	$(MAKE) -C $(BASE)/$*/Linuxmod_$(ARCH) clean uninstall

.PHONY: modules $(MODULES) modules.clean $(MODULES:%=%.clean)

#
# Programs.
#

programs:
	$(MAKE) -C $(BASE)/lmssrc

programs.clean:
	$(MAKE) -C $(BASE)/lmssrc clean uninstall

#
# Kernel.
#

kernel: uImage init

$(KERNEL)/.config: $(KERNEL_CONF)
	cp $< $@

kernel.prepare: $(KERNEL)/include/generated/bounds.h
$(KERNEL)/include/generated/bounds.h: $(KERNEL)/.config
	$(MAKE) -j2 -C $(KERNEL) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) oldconfig modules_prepare

$(KERNEL)/arch/arm/boot/uImage: kernel.prepare | $(MKIMAGE_CHECK)
	$(MAKE) -j2 -C $(KERNEL) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) uImage

uImage: $(KERNEL)/arch/arm/boot/uImage
	cp $< $@

kernel.clean:
	$(MAKE) -C $(KERNEL) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: kernel kernel.prepare kernel.clean

#
# U-Boot.
#

u-boot: uBoot

$(UBOOT)/u-boot.bin:
	$(MAKE) -C $(UBOOT) CROSS_COMPILE=$(CROSS_COMPILE) da850_omapl138_evm_config
	$(MAKE) -C $(UBOOT) CROSS_COMPILE=$(CROSS_COMPILE) all

uBoot: $(UBOOT)/u-boot.bin
	cp $< $@

u-boot.clean:
	$(MAKE) -C $(UBOOT) CROSS_COMPILE=$(CROSS_COMPILE) distclean

.PHONY: u-boot u-boot.clean

#
# Doc.
#

DOC_BROWSER = x-www-browser

doc:
	cd ../lms2012/doc && \
		doxygen Doxyfile && \
		cp graphics/LEGO.jpg html

doc.view: doc
	$(DOC_BROWSER) ../lms2012/doc/html/index.html

doc.clean:
	rm -rf ../lms2012/doc/html

.PHONY: doc doc.view doc.clean

#
# Install
#

build_image: kernel modules
	sudo ./make_image.sh; 

install_mars_rover: mars_rover control test_prioinv
	./mounty.sh; \
    sudo cp ../motor_control/Linux_AM1808/motor_control /media/LMS2012_EXT/home/root; \
    sudo cp ../sensor_control/Linux_AM1808/sensor_control /media/LMS2012_EXT/home/root; \
    sudo cp ../sound_control/Linux_AM1808/sound_control /media/LMS2012_EXT/home/root; \
    sudo cp ../mars_rover/Linuxmod_AM1808/mars_rover.ko /media/LMS2012_EXT/home/root; \
    sudo cp ../test_prioinv/Linuxmod_AM1808/test_prioinv.ko /media/LMS2012_EXT/home/root; \
	./umounty.sh;

install_kernel: build_image
	./mounty.sh; \
	sudo ./update_sdcard.sh; \
    sudo cp ./lmsInit /media/LMS2012_EXT/etc/init.d/lms; \
    sudo chmod a+rwx /media/LMS2012_EXT/etc/init.d/lms; \
    ./umounty.sh; 

#
# Misc.
#

help:
	@echo 'Execute "make TARGET" where TARGET is:'
	@echo ' lms2012: to build the lms2012 program and its libraries'
	@echo ' modules: to build lms2012 kernel modules'
	@echo ' programs: to build bytecode programs and their data files'
	@echo ' kernel: to build Linux kernel'
	@echo ' u-boot: to build u-Boot'
	@echo ' doc: to build documentation'
	@echo ' install_kernel: to write kernel to the sdcard specified via [u]mounty'
	@echo ' install_mars: to write mars rover executables to the sdcard specified via [u]mounty'
	@echo ' build_image: create a sdcard image'
	@echo
	@echo 'Execute "make doc.view" to view the documentation in a web'
	@echo 'browser'
	@echo 
	@echo 'Execute "make TARGET.clean" to clean an individual target'
	@echo 'or "make clean" to clean everything'
	@echo
	@echo 'Add the ARCH=X86 argument to run for X86 platform (some targets'
	@echo 'are then meaningless).'

clean: lms2012.clean modules.clean programs.clean kernel.clean u-boot.clean \
	doc.clean

.PHONY: help clean
