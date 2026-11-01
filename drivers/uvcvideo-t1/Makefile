# SPDX-License-Identifier: GPL-2.0-or-later
# Builds build/ (filled by prepare.sh) with the kernel's own uvc Makefile.
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/build modules

clean:
	rm -rf build
