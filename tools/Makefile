# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See lib/LICENSE for licensing details.

include ../make/master.mk

BUILD_THESE := $(wildcard */.)

TARGETS := all clean

$(TARGETS): $(BUILD_THESE)

$(BUILD_THESE):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: $(TARGETS) $(BUILD_THESE)
