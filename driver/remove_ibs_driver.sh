#!/bin/bash
# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under the Linux kernel's version of the GPLv2
# See driver/LICENSE for more licensing details.
if [ -e /dev/cpu/0/ibs/op ]
then
    if [ -e /dev/ibs_0_op ]
    then
        which nproc &> /dev/null
        if [ $? -eq 0 ]; then
            NUM_CORES=$(expr `nproc` - 1)
        else
            NUM_CORES=$(cat /proc/cpuinfo | grep processor | wc -l)
        fi
        for i in $(seq 0 $NUM_CORES);
        do
            rm -rf /dev/cpu/${i}/ibs/
        done
    fi
    sudo /sbin/rmmod ibs
else
    echo -e "It does not look like the IBS driver is installed."
    echo -e "Can't find /dev/cpu/0/ibs/op, so skipping removal."
    exit 0
fi
