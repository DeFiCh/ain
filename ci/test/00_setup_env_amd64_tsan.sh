#!/usr/bin/env bash
#
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=x86_64-unknown-linux-gnu
export PACKAGES="python3-zmq libssl-dev libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-test-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev"
export NO_DEPENDS=1
export GOAL="install"
export DEFI_CONFIG="--enable-zmq --disable-wallet CPPFLAGS=-DDEBUG_LOCKORDER --with-sanitizers=thread --disable-hardening --disable-asm CC=clang-15 CXX=clang++-15"
