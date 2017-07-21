# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See tools/LICENSE for licensing details.

COMMON_MK_DIR:= $(dir $(lastword $(MAKEFILE_LIST)))

include $(COMMON_MK_DIR)../make/master.mk

THIS_TOOL_DIR_CSOURCES=$(shell find ${THIS_TOOL_DIR} -name "*.c" -type f)
THIS_TOOL_DIR_COBJECTS=$(THIS_TOOL_DIR_CSOURCES:.c=.o)
THIS_TOOL_DIR_CDEPS=$(THIS_TOOL_DIR_COBJECTS:.o=.d)

BUILD_THESE=$(THIS_TOOL_DIR)

CFLAGS+=$(TOOL_CFLAGS)
LDFLAGS+=$(TOOL_LDFLAGS)

.PHONY: all
all: $(THIS_TOOL_DIR)/$(THIS_TOOL_NAME)

$(THIS_TOOL_DIR)/$(THIS_TOOL_NAME): $(THIS_TOOL_DIR_COBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(THIS_TOOL_DIR)$(THIS_TOOL_NAME) $(THIS_TOOL_DIR_COBJECTS) $(THIS_TOOL_DIR_CDEPS) $(THIS_TOOL_DIR)*.ibs $(THIS_TOOL_DIR)*.out

-include $(THIS_TOOL_DIR_CDEPS)
