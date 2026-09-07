KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
INSTALL_MOD_DIR ?= updates
commitcount := $(shell git rev-list --count HEAD 2>/dev/null)
commithash := $(shell git rev-parse --short HEAD 2>/dev/null)

# Detect if the kernel was built with clang/LLVM and use the same compiler
KERNEL_CC := $(shell grep -qs CONFIG_CC_IS_CLANG=y $(KDIR)/.config && echo clang)
ifeq ($(KERNEL_CC),clang)
  LLVM_FLAGS := LLVM=1
endif

default: modules
build: modules
modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAGS) modules
install: modules
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAGS) \
		INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) modules_install
	# Remove the legacy module installed by previous releases
	rm -f -- /lib/modules/$(KVER)/kernel/drivers/hwmon/nct6687.ko*
	depmod $(KVER)
uninstall:
	@test -n "$(KVER)" || { echo "Unable to determine kernel release from $(KDIR)" >&2; exit 1; }
	rm -f -- /lib/modules/$(KVER)/$(INSTALL_MOD_DIR)/nct6687.ko*
	depmod $(KVER)
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAGS) clean

akmod/build:
	sudo dnf install -y akmods buildsys-build-rpmfusion
	mkdir -p $(CURDIR)/.tmp/nct6687d-1.0.${commitcount}/nct6687d
	cp LICENSE Kbuild Makefile nct6687.c $(CURDIR)/.tmp/nct6687d-1.0.${commitcount}/nct6687d
	cd .tmp && tar -czvf nct6687d-1.0.${commitcount}.tar.gz nct6687d-1.0.${commitcount} && cd -
	mkdir -p $(CURDIR)/.tmp/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(CURDIR)/.tmp/nct6687d-1.0.${commitcount}.tar.gz $(CURDIR)/.tmp/rpmbuild/SOURCES/
	cp LICENSE $(CURDIR)/.tmp/rpmbuild/SOURCES/
	echo 'nct6687' | tee $(CURDIR)/.tmp/rpmbuild/SOURCES/nct6687.conf
	cp fedora/*.spec $(CURDIR)/.tmp/rpmbuild/SPECS/
	sed -i "s/MAKEFILE_PKGVER/${commitcount}/g" $(CURDIR)/.tmp/rpmbuild/SPECS/*
	sed -i "s/MAKEFILE_COMMITHASH/${commithash}/g" $(CURDIR)/.tmp/rpmbuild/SPECS/*
	rpmbuild -ba --define "_topdir $(CURDIR)/.tmp/rpmbuild" $(CURDIR)/.tmp/rpmbuild/SPECS/nct6687d.spec
	rpmbuild -ba --define "_topdir $(CURDIR)/.tmp/rpmbuild" $(CURDIR)/.tmp/rpmbuild/SPECS/nct6687d-kmod.spec
akmod/install: akmod/build
	sudo dnf install $(CURDIR)/.tmp/rpmbuild/RPMS/*/*.rpm
akmod/remove:
	sudo dnf remove nct6687d
	rm -rf .tmp
akmod: akmod/install

dkms/install:
	rm -rf $(CURDIR)/dkms
	mkdir -p $(CURDIR)/dkms
	cp $(CURDIR)/dkms.conf $(CURDIR)/Kbuild $(CURDIR)/Makefile $(CURDIR)/nct6687.c $(CURDIR)/dkms
	if sudo dkms status nct6687d/1 2>/dev/null | grep -q '^nct6687d/1'; then \
		sudo dkms remove nct6687d/1 --all; \
	fi
	sudo dkms install $(CURDIR)/dkms
dkms/remove:
	sudo dkms remove nct6687d/1 --all
	rm -rf $(CURDIR)/dkms

debian/changelog: FORCE
	git --no-pager log \
		--format='nct6687d-dkms (%ad) unstable; urgency=low%n%n  * %s%n%n -- %aN <%aE>  %aD%n' \
		--date='format:%Y%m%d-%H%M%S' \
		> $@
deb: debian/changelog
	sudo apt install -y debhelper dkms
	@if apt-cache show dh-dkms > /dev/null 2>&1; then \
		sudo apt-get install -y dh-dkms; \
	fi
	dpkg-buildpackage -b -rfakeroot -us -uc

.PHONY: default build modules install uninstall clean \
	akmod akmod/build akmod/install akmod/remove \
	dkms/install dkms/remove \
	deb FORCE
FORCE:
