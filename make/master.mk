# Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
#
# This file is distributed under the BSD license described in
# make/LICENSE.bsd
# Alternatively, this file may be distributed under the terms of the
# Linux kernel's version of the GPLv2. See make/LICENSE.gpl

##############################################
# AMD Research IBS Toolkit Make Defines
##############################################

HERE := $(dir $(lastword $(MAKEFILE_LIST)))

ifeq ($(CC),cc)
CC=gcc
endif

INCLUDE_DIR=${HERE}../include
DRIVER_DIR=${HERE}../driver
LIB_DIR=${HERE}../lib
TOOLS_DIR=${HERE}../tools
ANNOTATE_DIR=${TOOLS_DIR}/ibs_run_and_annotate
REPO_DIR:=$(HERE)../
REPO_SUBDIRS := $(wildcard $(REPO_DIR)/.)

# Name of all the source files we need for the detector
DRIVER_CSOURCES=$(shell find ${DRIVER_DIR} -name "*.c" -type f)
DRIVER_COBJECTS=$(DRIVER_CSOURCES:.c=.o)
DRIVER_CDEPS=$(DRIVER_COBJECTS:.o=.d)

INCLUDE_FLAGS=-I$(INCLUDE_DIR)
WERROR_FLAG=-Werror

ifneq ($(SKIP_PEDANTIC),1)
ifeq ($(CC),gcc)
GCC_GTE_49 := $(shell expr `gcc -dumpversion | cut -f1,2 -d.` \>= 4.9)
ifeq "$(GCC_GTE_49)" "1"
PEDANTIC_FLAGS=-Wpedantic -pedantic-errors
endif
endif
endif

ifeq ($(CC),gcc)
GCC_GTE_42 := $(shell expr `gcc -dumpversion | cut -f1,2 -d.` \>= 4.2)
ifeq "$(GCC_GTE_42)" "1"
MARCH_FLAGS=-march=native
endif
endif

CPPCHECK_GTE_159 := $(shell which cppcheck > /dev/null; if test $$? -eq  0; then expr `cppcheck --version | awk '{print $$2}'` \>= 1.59; else echo 0; fi)
ifeq "$(CPPCHECK_GTE_159)" "1"
CPPCHECK_WARN_FLAG=warning,
CPPCHECK_STD_FLAGS=--std=c11 --std=c++11
endif

ifneq ($(SKIP_WEXTRA),1)
WEXTRA_FLAGS=-Wextra -Wpacked
endif

WARN_FLAGS=-Wall $(WEXTRA_FLAGS) $(PEDANTIC_FLAGS) -Wundef
CONLY_WARN=-Wold-style-definition

# use the following line for normal operation, or use the second line following this one for debugging/profiling
ifndef DEBUG
C_AND_CXX_FLAGS=-g3 -ggdb $(WERROR_FLAG) $(WARN_FLAGS) $(INCLUDE_FLAGS) -O3 $(MARCH_FLAGS) -DNDEBUG
else
C_AND_CXX_FLAGS=-g3 -ggdb -fno-omit-frame-pointer $(WARN_FLAGS) $(INCLUDE_FLAGS) -O0 -DDEBUG
endif

CFLAGS=$(C_AND_CXX_FLAGS) -std=gnu99 $(CONLY_WARN)
CXXFLAGS=$(C_AND_CXX_FLAGS) -std=c++99 -I$(HERE).. -fno-strict-aliasing -Wformat -Werror=format-security -fwrapv

default: all

check:
	cppcheck --force --enable=$(CPPCHECK_WARN_FLAG)style,performance,portability,information,missingInclude --error-exitcode=-1 -D __refdata=" " $(CPPCHECK_STD_FLAGS) $(BUILD_THESE) -q -j `nproc`
	$(if $(shell which pylint 2>/dev/null), \
	pylint -E $(ANNOTATE_DIR)/ibs_run_and_annotate, \
	@echo "pylint does not exist, skipping.")

%.o: %.c
	$(CC) $(CFLAGS) -c -MMD -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -MMD -o $@ $<

%.d: ;
.PRECIOUS: %.d
