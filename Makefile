obj-m += nct6687.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install: all
	sudo cp nct6687.ko /lib/modules/$(shell uname -r)/kernel/drivers/hwmon/
	sudo depmod
	sudo modprobe nct6687

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean