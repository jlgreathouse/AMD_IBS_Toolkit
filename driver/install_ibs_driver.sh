#!/bin/bash
# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under the Linux kernel's version of the GPLv2
# See driver/LICENSE for more licensing details.
BASE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ ! -e /dev/cpu/0/ibs/op ]
then
    if [ ! -f ${BASE_DIR}/ibs.ko ]; then
        echo -e "${BASE_DIR}/ibs.ko does not exist. Trying to build it."
        cd ${BASE_DIR}
        make
        if [ $? -ne 0 ]; then
            echo -e "Failed to build. Quitting."
            exit -1
        fi
    fi
    sudo /sbin/insmod ${BASE_DIR}/ibs.ko

    # Set all of the IBS devices accessible by anyone.
    # Note that you should change this if you care about security!
    CORES=$(cat /proc/cpuinfo | grep processor | awk '{print $3}')
    for i in ${CORES};
    do
        # We are on some old kernel where the driver cannot create the
        # IBS stuff inside /dev/cpu. Manually do this with the installer.
        if [ -e /dev/ibs_${i}_op ]
        then
            mkdir /dev/cpu/${i}/ibs/
            ln -s /dev/ibs_${i}_op /dev/cpu/${i}/ibs/op
            ln -s /dev/ibs_${i}_fetch /dev/cpu/${i}/ibs/fetch
        fi
        # Some older kernels don't properly handle setting of DEVMODE
        # permissions, so let's make the driver RW forcibly
        if [ -e /dev/cpu/${i}/ibs/op ]
        then
            sudo chmod g+rw /dev/cpu/${i}/ibs/op
            sudo chmod o+rw /dev/cpu/${i}/ibs/op
        fi
        if [ -e /dev/cpu/${i}/ibs/fetch ]
        then
            sudo chmod g+rw /dev/cpu/${i}/ibs/fetch
            sudo chmod o+rw /dev/cpu/${i}/ibs/fetch
        fi
    done
else
    echo -e "/dev/cpu/0/ibs/op exists, so it looks like the IBS module is already installed."
    exit 0
fi
