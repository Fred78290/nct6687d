obj-m += nct6687.o

curpwd := $(shell pwd)
kver   := $(shell uname -r)

build:
	[ -d "${curpwd}/${kver}" ] && rm -rf ${curpwd}/${kver}
	mkdir ${curpwd}/${kver}
	cp ${curpwd}/Makefile ${curpwd}/nct6687.c ${curpwd}/${kver}
	cd ${curpwd}/${kver}
	make -C /lib/modules/${kver}/build M=${curpwd}/${kver} modules

install: build
	sudo cp ${curpwd}/${kver}/nct6687.ko /lib/modules/${kver}/kernel/drivers/hwmon/
	sudo depmod
	sudo modprobe nct6687

clean:
	make -C /lib/modules/${kver}/build M=${curpwd}/${kver} clean
