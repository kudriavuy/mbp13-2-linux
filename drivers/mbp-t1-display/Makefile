# appletbdrm with T1 support, built from the source matching the kernel
# (7.x: Linux 7.2.9's driver, 6.x: Linux 6.18.56's). Installs as
# updates/dkms/appletbdrm and so takes precedence over the in-tree module.
ifneq ($(KERNELRELEASE),)
ifeq ($(shell [ $(VERSION) -ge 7 ] && echo y),y)
appletbdrm-y := appletbdrm-7.2.o
else
appletbdrm-y := appletbdrm-6.18.o
endif
obj-m := appletbdrm.o hid-t1-touchbar.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif
