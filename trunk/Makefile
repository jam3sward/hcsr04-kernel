obj-m += hcsr04.o
hcsr04-objs := hcsr04-main.o

all:
	make ARCH=arm CROSS_COMPILE=${CCPREFIX} -C ../linux M=$(PWD) modules

clean:
	make ARCH=arm CROSS_COMPILE=${CCPREFIX} -C ../linux M=$(PWD) clean
