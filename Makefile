# Top-level developer-facing Makefile for enfs-dkms.
#
# Real kernel-side build is driven by Kbuild and invoked via the kernel's
# own build system (see the `modules` target). This Makefile orchestrates
# scaffolding tasks (DKMS install/uninstall, Debian packaging, syncing
# sources to the test VM, etc.).

VERSION       ?= 0.1.0
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
DKMS_TREE     ?= /var/lib/dkms
PROJECT       := enfs
SRC_DIR       := $(CURDIR)/src
VENDOR_DIR    := $(CURDIR)/vendor/openeuler
# VM_HOST and VM_PATH are intentionally unset by default — set them in
# your shell, in secrets/local-env.sh (gitignored), or on the command
# line, e.g.:
#   make sync-vm VM_HOST=ubuntu@10.0.0.42 VM_PATH=/home/ubuntu/enfs-dkms
VM_HOST       ?=
VM_PATH       ?= /home/ubuntu/enfs-dkms

# Default: explain. We don't want a bare `make` to start a long kernel
# build before the porting layer in src/ has been wired up.
.PHONY: help
help:
	@echo "enfs-dkms — developer targets:"
	@echo
	@echo "  make port              Run scripts/apply-compat-shims.sh to populate src/"
	@echo "                         from vendor/openeuler/ + compat/ + patches/"
	@echo "  make modules           Out-of-tree kernel build against KDIR=$(KDIR)"
	@echo "  make clean             Remove build artefacts"
	@echo
	@echo "  make dkms-conf         Generate dkms.conf from dkms.conf.in"
	@echo "  make dkms-install      Stage src/ into $(DKMS_TREE)/$(PROJECT)/$(VERSION)/"
	@echo "                         then 'dkms add/build/install'"
	@echo "  make dkms-uninstall    'dkms remove' + delete staged tree"
	@echo
	@echo "  make deb               Build the enfs-dkms .deb package (debian/)"
	@echo
	@echo "  make sync-vm           rsync src/ + vendor/ + scripts/ to $(VM_HOST):$(VM_PATH)"
	@echo "  make build-on-vm       ssh into VM and run 'make modules'"
	@echo "  make smoke-on-vm       ssh into VM, dkms-install, modprobe enfs, dmesg tail"
	@echo
	@echo "Variables: VERSION=$(VERSION) KVER=$(KVER) KDIR=$(KDIR) VM_HOST=$(VM_HOST)"

.PHONY: port
port:
	@scripts/apply-compat-shims.sh "$(VENDOR_DIR)" "$(SRC_DIR)" "$(CURDIR)/compat" "$(CURDIR)/patches"

.PHONY: modules
modules: port
	$(MAKE) -C $(KDIR) M=$(SRC_DIR) modules

.PHONY: clean
clean:
	-$(MAKE) -C $(KDIR) M=$(SRC_DIR) clean 2>/dev/null
	rm -rf $(SRC_DIR)/.build-stamp
	find $(SRC_DIR) -name '*.o' -o -name '*.ko' -o -name '.*.cmd' -o -name '*.mod*' 2>/dev/null | xargs -r rm -f

.PHONY: dkms-conf
dkms-conf:
	sed 's|@VERSION@|$(VERSION)|g' dkms.conf.in > dkms.conf

.PHONY: dkms-install
dkms-install: dkms-conf port
	scripts/dkms-install.sh "$(PROJECT)" "$(VERSION)" "$(SRC_DIR)" "$(CURDIR)/dkms.conf"

.PHONY: dkms-uninstall
dkms-uninstall:
	-sudo dkms remove -m $(PROJECT) -v $(VERSION) --all
	-sudo rm -rf /usr/src/$(PROJECT)-$(VERSION)

.PHONY: deb
deb:
	dpkg-buildpackage -us -uc -b

.PHONY: sync-vm
sync-vm: _check-vm-host
	scripts/deploy-to-vm.sh "$(VM_HOST)" "$(VM_PATH)"

.PHONY: build-on-vm
build-on-vm: sync-vm
	ssh $(VM_HOST) 'cd $(VM_PATH) && make modules'

.PHONY: smoke-on-vm
smoke-on-vm: sync-vm
	ssh $(VM_HOST) 'cd $(VM_PATH) && sudo make dkms-install && sudo modprobe enfs && dmesg | tail -40'

.PHONY: _check-vm-host
_check-vm-host:
	@if [ -z "$(VM_HOST)" ]; then \
	    echo "ERROR: VM_HOST is unset. Set it on the command line, in your shell," ;\
	    echo "       or via 'source secrets/local-env.sh'. Example:" ;\
	    echo "       make sync-vm VM_HOST=ubuntu@10.0.0.42 VM_PATH=/home/ubuntu/enfs-dkms" ;\
	    exit 1 ;\
	fi
