obj-m += nct6687.o

curpwd      := $(shell pwd)
kver        ?= $(shell uname -r)
commitcount := $(shell git rev-list --all --count)
commithash  := $(shell git rev-parse --short HEAD)
fedoraver   := $(shell sed -n 's/.*Fedora release \([^ ]*\).*/\1/p' /etc/fedora-release 2>/dev/null || echo 0)


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


akmod/build:
	@if [ $(fedoraver) -gt 40 ]; then \
		echo "Fedora version $(fedoraver) is greater than 40, using dnf5 command"; \
		sudo dnf install -y @development-tools; \
	else \
		sudo dnf groupinstall -y "Development Tools"; \
	fi
	sudo dnf install -y rpmdevtools kmodtool
	mkdir -p ${curpwd}/.tmp/nct6687d-1.0.${commitcount}/nct6687d
	cp LICENSE Makefile nct6687.c ${curpwd}/.tmp/nct6687d-1.0.${commitcount}/nct6687d
	cd .tmp && tar -czvf nct6687d-1.0.${commitcount}.tar.gz nct6687d-1.0.${commitcount} && cd -
	mkdir -p ${curpwd}/.tmp/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp ${curpwd}/.tmp/nct6687d-1.0.${commitcount}.tar.gz ${curpwd}/.tmp/rpmbuild/SOURCES/
	echo 'nct6687' | tee ${curpwd}/.tmp/rpmbuild/SOURCES/nct6687.conf
	cp fedora/*.spec ${curpwd}/.tmp/rpmbuild/SPECS/
	sed -i "s/MAKEFILE_PKGVER/${commitcount}/g" ${curpwd}/.tmp/rpmbuild/SPECS/*
	sed -i "s/MAKEFILE_COMMITHASH/${commithash}/g" ${curpwd}/.tmp/rpmbuild/SPECS/*
	rpmbuild -ba --define "_topdir ${curpwd}/.tmp/rpmbuild" ${curpwd}/.tmp/rpmbuild/SPECS/nct6687d.spec
	rpmbuild -ba --define "_topdir ${curpwd}/.tmp/rpmbuild" ${curpwd}/.tmp/rpmbuild/SPECS/nct6687d-kmod.spec
akmod/install: akmod/build
	sudo dnf install ${curpwd}/.tmp/rpmbuild/RPMS/*/*.rpm
akmod/clean:
	sudo dnf remove nct6687d
	rm -rf .tmp
akmod: akmod/install


dkms/build:
	make -C /lib/modules/${kver}/build M=${curpwd} modules

dkms/install:
	rm -rf ${curpwd}/dkms
	mkdir -p ${curpwd}/dkms
	cp ${curpwd}/dkms.conf ${curpwd}/Makefile ${curpwd}/nct6687.c ${curpwd}/dkms
	sudo rm -rf /usr/src/nct6687d-1
	sudo cp -rT dkms /usr/src/nct6687d-1
	sudo dkms install nct6687d/1
	sudo modprobe nct6687

dkms/clean:
	sudo dkms remove nct6687d/1 --all
	make -C /lib/modules/${kver}/build M=${curpwd} clean

debian/changelog: FORCE
	git --no-pager log \
		--format='nct6687d-dkms (%ad) unstable; urgency=low%n%n  * %s%n%n -- %aN <%aE>  %aD%n' \
		--date='format:%Y%m%d-%H%M%S' \
		> $@

deb: debian/changelog
	sudo apt install -y debhelper dkms dh-dkms
	@if apt-cache show dh-dkms > /dev/null 2>&1; then \
		sudo apt-get install -y dh-dkms; \
	fi
	dpkg-buildpackage -b -rfakeroot -us -uc

.PHONY: FORCE
FORCE:
