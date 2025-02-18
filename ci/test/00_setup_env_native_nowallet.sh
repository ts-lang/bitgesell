#!/usr/bin/env bash
#
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8
#export PACKAGE_MANAGER_INSTALL #default
#export MAKEJOBS #default
#export DANGER_RUN_CI_ON_HOST #default
#export TEST_RUNNER_PORT_MIN #default
#export CCACHE_SIZE #default
#export CCACHE_DIR #default

export CONTAINER_NAME=ci_native_nowallet
export DOCKER_NAME_TAG=ubuntu:18.04  # Use bionic to have one config run the tests in python3.6, see doc/dependencies.md
export PACKAGES="python3-zmq clang-7 llvm-7 libc++abi-7-dev libc++-7-dev"  # Use clang-7 to test C++17 compatibility, see doc/dependencies.md
export DEP_OPTS="NO_WALLET=1 CC=clang-7 CXX='clang++-7 -stdlib=libc++'"
export GOAL="install"
export BGL_CONFIG="--enable-reduce-exports CC=clang-7 CXX='clang++-7 -stdlib=libc++'"
