#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

DEFI_CONFIG_ALL="--disable-dependency-tracking --prefix=$BASE_BUILD_DIR/depends/$HOST --bindir=$BASE_OUTDIR/bin --libdir=$BASE_OUTDIR/lib"
if [ -z "$NO_DEPENDS" ]; then
  DOCKER_EXEC ccache --max-size=$CCACHE_SIZE
fi

BEGIN_FOLD autogen
if [ -n "$CONFIG_SHELL" ]; then
  DOCKER_EXEC "$CONFIG_SHELL" -c "./autogen.sh"
else
  DOCKER_EXEC ./autogen.sh
fi
END_FOLD

mkdir -p build
cd build || (echo "could not enter build directory"; exit 1)

BEGIN_FOLD configure
DOCKER_EXEC ../configure --cache-file=config.cache $DEFI_CONFIG_ALL $DEFI_CONFIG || ( cat config.log && false)
END_FOLD

BEGIN_FOLD distdir
DOCKER_EXEC make distdir VERSION=$HOST
DOCKER_EXEC cp -r ../share  "defi-$HOST/"
DOCKER_EXEC cp -r ../contrib  "defi-$HOST/"
END_FOLD

cd "defi-$HOST" || (echo "could not enter distdir defi-$HOST"; exit 1)

BEGIN_FOLD configure
DOCKER_EXEC ./configure --cache-file=../config.cache $DEFI_CONFIG_ALL $DEFI_CONFIG || ( cat config.log && false)
END_FOLD

set -o errtrace
trap 'DOCKER_EXEC "cat ${BASE_BUILD_DIR}/sanitizer-output/* 2> /dev/null"' ERR

BEGIN_FOLD build
DOCKER_EXEC make $MAKEJOBS $GOAL || ( echo "Build failure. Verbose build follows." && DOCKER_EXEC make $GOAL V=1 ; false )
END_FOLD

cd ${BASE_BUILD_DIR} || (echo "could not enter travis build dir $BASE_BUILD_DIR"; exit 1)
