#!/usr/bin/env bash
#
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=x86_64-unknown-linux-gnu
export DOCKER_NAME_TAG=ubuntu:16.04
export PACKAGES="clang llvm python3-zmq qtbase5-dev qttools5-dev-tools libssl-dev libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libprotobuf-dev protobuf-compiler libqrencode-dev"
export NO_DEPENDS=1
export GOAL="install"
export BITCOIN_CONFIG="--enable-zmq --disable-wallet --with-gui=qt5 CPPFLAGS=-DDEBUG_LOCKORDER --with-sanitizers=thread --disable-hardening --disable-asm CC=clang CXX=clang++"
