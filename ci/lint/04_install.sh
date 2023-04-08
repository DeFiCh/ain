#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

PATH=$PATH:~/.local/bin

echo "::group::install-lint-deps"

curl -fsSL -o- https://bootstrap.pypa.io/get-pip.py | python3
pip3 install codespell==2.2.4
pip3 install flake8==6.0.0
pip3 install vulture==2.7

SHELLCHECK_VERSION=v0.7.1
curl -L -s "https://github.com/koalaman/shellcheck/releases/download/${SHELLCHECK_VERSION}/shellcheck-${SHELLCHECK_VERSION}.linux.x86_64.tar.xz" | tar --xz -xf - --directory /tmp/
export PATH="/tmp/shellcheck-${SHELLCHECK_VERSION}:${PATH}"

echo "::endgroup::"
