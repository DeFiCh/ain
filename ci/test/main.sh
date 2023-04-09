#!/bin/bash

export LC_ALL=C
set -Eeuo pipefail

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    
    cd ../../
    ./make.sh test
}

_ensure_script_dir() {
    _WORKING_DIR="$(pwd)"
    local dir
    dir="$(dirname "${BASH_SOURCE[0]}")"
    _SCRIPT_DIR="$(cd "${dir}/" && pwd)"
}

_cleanup() {
    cd "$_WORKING_DIR"
}

main "$@"
