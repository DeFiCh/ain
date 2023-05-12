#!/bin/bash

export LC_ALL=C.UTF-8
set -Eeuo pipefail

main() {
    PATH=$PATH:~/.local/bin

    echo "::group::install-lint-deps"

    curl -fsSL -o- https://bootstrap.pypa.io/get-pip.py | python3
    pip3 install codespell==2.2.4
    pip3 install flake8==6.0.0
    pip3 install vulture==2.7

    SHELLCHECK_VERSION=v0.9.0
    curl -L -s "https://github.com/koalaman/shellcheck/releases/download/${SHELLCHECK_VERSION}/shellcheck-${SHELLCHECK_VERSION}.linux.x86_64.tar.xz" | tar --xz -xf - --directory /tmp/
    export PATH="/tmp/shellcheck-${SHELLCHECK_VERSION}:${PATH}"

    echo "::endgroup::"
}

main "$@"