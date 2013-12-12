
main: hello task prioInv

hello: hello.c
	arm-none-linux-gnueabi-gcc -o hello -lrt -I../../../extra/linux-03.20.00.13/include hello.c

task: task.c
	arm-none-linux-gnueabi-gcc -o task -lrt -I../../../extra/linux-03.20.00.13/include task.c

prioInv: pri-inv.c
	arm-none-linux-gnueabi-gcc -o prioInv -lrt -lpthread -D_GNU_SOURCE -I../../../extra/linux-03.20.00.13/include pri-inv.c
