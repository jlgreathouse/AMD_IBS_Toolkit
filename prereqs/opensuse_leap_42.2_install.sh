#!/bin/bash
# Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See prereqs/LICENSE for licensing details.

# Likely need git to work with this repo.
sudo zypper install -y git

# Install the build environments that we need for the IBS driver
sudo zypper install -y -t pattern devel_basis devel_C_C++ devel_kernel
sudo zypper instal -y clang
sudo zypper install -y cppcheck
sudo zypper install -y pylint
sudo zypper install -y python-pip python-setuptools
sudo pip install joblib
sudo pip install argparse
