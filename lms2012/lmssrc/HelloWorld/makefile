
main: hello task prioInv

hello: hello.c
	arm-none-linux-gnueabi-gcc -o hello -lrt -I../../../extra/linux-03.20.00.13/include hello.c

task: task.c
	arm-none-linux-gnueabi-gcc -o task -lrt -I../../../extra/linux-03.20.00.13/include task.c

prioInv: pri-inv.c
	arm-none-linux-gnueabi-gcc -o prioInv -lrt -lpthread -D_GNU_SOURCE -I../../../extra/linux-03.20.00.13/include pri-inv.c

lcdTask: lcdTask.c
	arm-none-linux-gnueabi-gcc -o lcdTask -lrt -lpthread -D_GNU_SOURCE -I../../../extra/linux-03.20.00.13/include lcdTask.c

install:
	sudo cp lcdTask /media/LMS2012_EXT/home/root; \
	sudo cp prioInv /media/LMS2012_EXT/home/root; \
	sudo cp hello /media/LMS2012_EXT/home/root; \
	sudo cp task /media/LMS2012_EXT/home/root; 

