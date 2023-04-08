#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

echo "::group::check-doc"
test/lint/check-doc.py
echo "::endgroup::"

echo "::group::check-rpc-mappings"
test/lint/check-rpc-mappings.py .
echo "::endgroup::"

test/lint/lint-all.sh
