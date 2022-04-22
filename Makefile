obj-m += nct6687.o

curpwd := $(shell pwd)
kver   := $(shell uname -r)

build:
	rm -rf ${curpwd}/${kver}
	mkdir -p ${curpwd}/${kver}
	cp ${curpwd}/Makefile ${curpwd}/nct6687.c ${curpwd}/${kver}
	cd ${curpwd}/${kver}
	make -C /lib/modules/${kver}/build M=${curpwd}/${kver} modules

install: build
	sudo cp ${curpwd}/${kver}/nct6687.ko /lib/modules/${kver}/kernel/drivers/hwmon/
	sudo depmod
	sudo modprobe nct6687

clean:
	[ -d "${curpwd}/${kver}" ] && make -C /lib/modules/${kver}/build M=${curpwd}/${kver} clean



dkms/build:
	echo ">>> ${curpwd}"
	make -C /lib/modules/${kver}/build M=${curpwd} modules

dkms/install:
	rm -rf ${curpwd}/dkms
	mkdir -p ${curpwd}/dkms
	cp ${curpwd}/dkms.conf ${curpwd}/Makefile ${curpwd}/nct6687.c ${curpwd}/dkms
	[ -d /usr/src/nct6687d-1 ] && sudo rm -rf /usr/src/nct6687d-1
	sudo cp -rT dkms /usr/src/nct6687d-1
	sudo dkms install nct6687d/1
	sudo modprobe nct6687

dkms/clean:
	sudo dkms remove nct6687d/1
	make -C /lib/modules/${kver}/build M=${curpwd} clean



debian/changelog: FORCE
	git --no-pager log \
		--format='nct6687d-dkms (%ad) unstable; urgency=low%n%n  * %s%n%n -- %aN <%aE>  %aD%n' \
		--date='format:%Y%m%d-%H%M%S' \
		> $@

deb: debian/changelog
	dpkg-buildpackage -b -rfakeroot -us -uc

.PHONY: FORCE
FORCE:
