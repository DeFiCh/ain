#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

DOCKER_EXEC echo \> \$HOME/.defi  # Make sure default datadir does not exist and is never read by creating a dummy file

# Note this still downloads and extract in source-tree which is not ideal, but we'll 
# we'll keep this for now as this workflow is usually only run on the CI
# and later, move to out-of-tree
SRC_DEPENDS_DIR=$BASE_ROOT_DIR/depends
DEPENDS_DIR=$BASE_BUILD_DIR/depends

mkdir -p $DEPENDS_DIR/SDKs 

OSX_SDK_BASENAME="Xcode-${XCODE_VERSION}-${XCODE_BUILD_ID}-extracted-SDK-with-libcxx-headers"
OSX_SDK_TAR_FILE="${OSX_SDK_BASENAME}.tar.gz"
OSX_SDK_PATH="${DEPENDS_DIR}/SDKs/${OSX_SDK_BASENAME}"

if [ -n "$XCODE_VERSION" ] && [ ! -f "$OSX_SDK_PATH" ]; then
  BEGIN_FOLD osx-sdk-download
  DOCKER_EXEC curl --location --fail "${SDK_URL}/${OSX_SDK_TAR_FILE}" -o "$OSX_SDK_TAR_FILE"
  DOCKER_EXEC tar -C "$DEPENDS_DIR/SDKs" -xf "$OSX_SDK_TAR_FILE"
  END_FOLD
fi
if [[ $HOST = *-mingw32 ]]; then
  DOCKER_EXEC update-alternatives --set $HOST-gcc \$\(which $HOST-gcc-posix\)
  DOCKER_EXEC update-alternatives --set $HOST-g++ \$\(which $HOST-g++-posix\)
fi
if [ -z "$NO_DEPENDS" ]; then
  BEGIN_FOLD build-deps
  DOCKER_EXEC CONFIG_SHELL= make $MAKEJOBS -C $SRC_DEPENDS_DIR HOST=$HOST $DEP_OPTS DESTDIR=$DEPENDS_DIR
  END_FOLD
fi
