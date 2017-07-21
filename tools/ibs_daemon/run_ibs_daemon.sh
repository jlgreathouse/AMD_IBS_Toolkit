#!/bin/bash
# Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See tools/LICENSE for licensing details.
BASE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ ! -f ${BASE_DIR}/ibs_daemon ]; then
    echo -e "${BASE_DIR}/ibs_daemon does not exist. Exiting."
    exit -1
fi

if ldd ${BASE_DIR}/ibs_daemon | grep -q "libibs.so => not found"; then
    echo -e "libibs.so is not in the LD_LIBRARY_PATH. Trying to add it.."
    if [ ! -f ${BASE_DIR}/../../lib/libibs.so ]; then
        echo -e "${BASE_DIR}/../../lib/libibs.so does not exist. Trying to build it.."
        pushd ${BASE_DIR}/../../lib/
        make
        if [ $? -ne 0 ]; then
            echo -e "Failed to build libibs.so. Exiting."
            exit -1
        fi
        popd
    fi
    export LD_LIBRARY_PATH=${BASE_DIR}/../../lib/:$LD_LIBRARY_PATH
fi

if [ ! -f ${BASE_DIR}/../../lib/libibs.so ]; then
    echo -e "Cannot find ${BASE_DIR}/../../lib/libibs.so. Exiting."
    exit -1
else
    ${BASE_DIR}/ibs_daemon $*
fi
