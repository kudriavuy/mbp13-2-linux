# Out-of-tree build of the in-kernel Touch Bar drivers (hid-appletb-kbd,
# hid-appletb-bl) with T1 (iBridge 05ac:8600) support, plus apple-ibridge-pm
# (T1 sleep/wake notifications through ACPI APP7777.SOCW).
obj-m += hid-appletb-kbd.o hid-appletb-bl.o apple-ibridge-pm.o

KVERSION ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVERSION)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
