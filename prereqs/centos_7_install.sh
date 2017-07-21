#!/bin/bash
# Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
#
# This file is made available under a 3-clause BSD license.
# See prereqs/LICENSE for licensing details.

# Likely need git to work with this repo.
sudo yum install -y git

# Install the build environments that we need for the IBS driver
sudo yum install -y clang
sudo yum install -y clang-analyzer
sudo yum groupinstall -y "Development Tools"
sudo yum install -y wget
wget http://dl.fedoraproject.org/pub/epel/7/x86_64/e/epel-release-7-9.noarch.rpm
sudo rpm -ivh epel-release-7-9.noarch.rpm
rm -f epel-release-7-9.noarch.rpm
sudo yum install -y "kernel-devel-uname-r == $(uname -r)"
sudo yum install -y cppcheck
sudo yum install -y pylint
sudo yum install -y python-pip
sudp pip install joblib
sudo pip install argparse
