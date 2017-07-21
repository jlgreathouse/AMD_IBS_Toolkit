# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See lib/LICENSE for licensing details.

include ../make/master.mk

LIB_DIR_CSOURCES=$(shell find ${LIB_DIR} -name "*.c" -type f)
LIB_DIR_COBJECTS=$(LIB_DIR_CSOURCES:.c=.o)
LIB_DIR_CDEPS=$(LIB_DIR_COBJECTS:.o=.d)

BUILD_THESE=$(LIB_DIR)

CFLAGS  += -fPIC

TARGET  = libibs
VERSION = 1

all: $(TARGET).so.$(VERSION)

$(TARGET).so.$(VERSION): $(LIB_DIR_COBJECTS)
	$(CC) -shared $(CFLAGS) -o $@ $^
	ln -f -s $(TARGET).so.$(VERSION) $(LIB_DIR)/$(TARGET).so

clean:
	rm -f $(TARGET).so.$(VERSION) $(TARGET).so $(LIB_DIR_COBJECTS) $(LIB_DIR_CDEPS)

-include $(LIB_DIR_CDEPS)
