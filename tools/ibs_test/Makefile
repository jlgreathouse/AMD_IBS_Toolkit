# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See tools/LICENSE for licensing details.

THIS_TOOL_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
THIS_TOOL_NAME := ibs_test
TOOL_CFLAGS+=-I $(LIB_DIR)
TOOL_LDFLAGS+=-L $(LIB_DIR) -libs

include $(THIS_TOOL_DIR)../common.mk
