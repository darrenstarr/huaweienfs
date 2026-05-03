# Top-level developer-facing Makefile for enfs-dkms.
#
# Real kernel-side build is driven by Kbuild and invoked via the kernel's
# own build system (see the `modules` target). This Makefile orchestrates
# scaffolding tasks (DKMS install/uninstall, Debian packaging, syncing
# sources to the test VM, etc.).

VERSION       ?= 0.1.5
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
DKMS_TREE     ?= /var/lib/dkms
PROJECT       := enfs
SRC_DIR       := $(CURDIR)/src

# Pick the right vendor + patches set for the kernel we're building
# against. Override via `make port TARGET=ubuntu-6.8` etc. The default
# is auto-detected from KVER's major.minor.
ifndef TARGET
ifeq ($(filter 7.0.%,$(KVER)),$(KVER))
TARGET := ubuntu-7.0
else ifeq ($(filter 6.14.%,$(KVER)),$(KVER))
TARGET := ubuntu-6.14
else ifeq ($(filter 6.11.%,$(KVER)),$(KVER))
TARGET := ubuntu-6.11
else ifeq ($(filter 6.8.%,$(KVER)),$(KVER))
TARGET := ubuntu-6.8
else
TARGET := ubuntu-7.0
endif
endif

UBUNTU_VENDOR_DIR := $(CURDIR)/vendor/$(TARGET)
OE_VENDOR_DIR     := $(CURDIR)/vendor/openeuler
PATCHES_DIR       := $(CURDIR)/patches/$(TARGET)
COMPAT_DIR        := $(CURDIR)/compat
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
	@echo "  make port              Run scripts/build-src-tree.sh to populate src/"
	@echo "                         from vendor/ubuntu-7.0/ + patches/ubuntu-7.0/"
	@echo "                         + vendor/openeuler/ (OE-only files) + compat/"
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
	@echo "Variables: VERSION=$(VERSION) KVER=$(KVER) KDIR=$(KDIR) TARGET=$(TARGET) VM_HOST=$(VM_HOST)"
	@echo
	@echo "Supported targets (override with TARGET=...):"
	@echo "  ubuntu-7.0   Ubuntu 26.04 LTS GA kernel"
	@echo "  ubuntu-6.14  Ubuntu 24.04.2+ HWE kernel"
	@echo "  ubuntu-6.11  Ubuntu 24.04.1 HWE kernel"
	@echo "  ubuntu-6.8   Ubuntu 24.04 LTS GA kernel"

.PHONY: fetch-vendor
fetch-vendor:
	@scripts/fetch-vendor-ubuntu.sh "$(TARGET)"

.PHONY: port
port: fetch-vendor
	@scripts/build-src-tree.sh \
	    "$(UBUNTU_VENDOR_DIR)" \
	    "$(OE_VENDOR_DIR)" \
	    "$(PATCHES_DIR)" \
	    "$(COMPAT_DIR)" \
	    "$(SRC_DIR)"

.PHONY: modules
modules: port
	$(MAKE) -C $(KDIR) M=$(SRC_DIR) modules

.PHONY: manpage
manpage: docs/man/enfs.7

docs/man/enfs.7: docs/man/enfs.7.md
	pandoc -s -t man $< -o $@

.PHONY: clean
clean:
	-rm -f docs/man/enfs.7
	-$(MAKE) -C $(KDIR) M=$(SRC_DIR) clean 2>/dev/null
	rm -rf $(SRC_DIR)/.build-stamp
	find $(SRC_DIR) -name '*.o' -o -name '*.ko' -o -name '.*.cmd' -o -name '*.mod*' 2>/dev/null | xargs -r rm -f

.PHONY: dkms-conf
dkms-conf:
	sed 's|@VERSION@|$(VERSION)|g' dkms.conf.in > dkms.conf

.PHONY: dkms-install
dkms-install: dkms-conf
	scripts/dkms-install.sh "$(PROJECT)" "$(VERSION)" "$(CURDIR)" "$(CURDIR)/dkms.conf"

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
