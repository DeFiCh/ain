#!/bin/bash

export LC_ALL=C
set -Eeuo pipefail

main() {
    echo "::group::install-extended-lint-deps"

    CPPCHECK_VERSION=2.10
    curl -s https://codeload.github.com/danmar/cppcheck/tar.gz/${CPPCHECK_VERSION} | tar -zxf - --directory /tmp/
    (cd /tmp/cppcheck-${CPPCHECK_VERSION}/ && make CFGDIR=/tmp/cppcheck-${CPPCHECK_VERSION}/cfg/ > /dev/null)
    export PATH="$PATH:/tmp/cppcheck-${CPPCHECK_VERSION}/"

    echo "::endgroup::"
}

main "$@"
