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


debian/changelog: FORCE
	git --no-pager log \
		--format='nct6687d-dkms (%ad) unstable; urgency=low%n%n  * %s%n%n -- %aN <%aE>  %aD%n' \
		--date='format:%Y%m%d-%H%M%S' \
		> $@

deb: debian/changelog
	dpkg-buildpackage -b -rfakeroot -us -uc

.PHONY: FORCE
FORCE: