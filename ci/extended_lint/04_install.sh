#!/usr/bin/env bash
#
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

echo "::group::install-extended-lint-deps"

CPPCHECK_VERSION=2.10
curl -s https://codeload.github.com/danmar/cppcheck/tar.gz/${CPPCHECK_VERSION} | tar -zxf - --directory /tmp/
(cd /tmp/cppcheck-${CPPCHECK_VERSION}/ && make CFGDIR=/tmp/cppcheck-${CPPCHECK_VERSION}/cfg/ > /dev/null)
export PATH="$PATH:/tmp/cppcheck-${CPPCHECK_VERSION}/"

echo "::endgroup::"
