#!/usr/bin/env bash
#
# Copyright (c) 2019-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8
#export PACKAGE_MANAGER_INSTALL #default
#export MAKEJOBS #default
#export DANGER_RUN_CI_ON_HOST #default
#export TEST_RUNNER_PORT_MIN #default
#export CCACHE_SIZE #default
#export CCACHE_DIR #default


export CONTAINER_NAME=ci_win64
export DOCKER_NAME_TAG=ubuntu:20.04  # Check that Focal can cross-compile to win64
export HOST=x86_64-w64-mingw32
#export CONTAINER_NAME #?????
#export DOCKER_NAME_TAG #?????


#export NO_DEPENDS #default
export DEP_OPTS #default
export GOAL="deploy"
export BGL_CONFIG="--enable-reduce-exports --disable-gui-tests"
