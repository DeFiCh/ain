#!/bin/bash

export LC_ALL=C.UTF-8
set -Eeuo pipefail

main() {
    _setup_dir_env
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR/../../"
    
    test/lint/extended-lint-all.sh
}

_setup_dir_env() {
    _WORKING_DIR="$(pwd)"
    local dir
    dir="$(dirname "${BASH_SOURCE[0]}")"
    _SCRIPT_DIR="$(cd "${dir}/" && pwd)"
}

_cleanup() {
    cd "$_WORKING_DIR"
}

main "$@"


