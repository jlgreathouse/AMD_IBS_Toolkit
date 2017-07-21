# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under the Linux kernel's version of the GPLv2
# See driver/LICENSE for more licensing details.

ifneq ($(KERNELRELEASE),)

obj-m := ibs.o
ibs-y := ibs-core.o ibs-fops.o ibs-interrupt.o ibs-utils.o ibs-workarounds.o

EXTRA_CFLAGS += $(CFLAGS)

else

SKIP_PEDANTIC=1
SKIP_WEXTRA=1
include ../make/master.mk

BUILD_THESE=$(DRIVER_DIR)

KDIR ?= /lib/modules/$(shell uname -r)/build
CFLAGS+=-I$(CURDIR)/$(INCLUDE_DIR)

all:
ifneq (,$(findstring ccc-analyzer,$(CC)))
	$(warning Cannot build the IBS Linux driver with scan-build. Linux kernel files fail during the scan.)
else
	$(MAKE) -C $(KDIR) M=$$PWD CFLAGS="$(CFLAGS) -mcmodel=kernel -mno-red-zone -fno-common"
endif

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
endif
