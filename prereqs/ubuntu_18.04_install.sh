#!/bin/bash
# Copyright (c) 2017-2018 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See prereqs/LICENSE for licensing details.

# Likely need git to work with this repo.
sudo apt-get install -y git

# Install the build environments that we need for the IBS driver
sudo apt-get install -y build-essential clang clang-tools cppcheck

# Install Python utilities
sudo apt-get install -y pylint python-pip python-argparse python-parallel python-joblib
