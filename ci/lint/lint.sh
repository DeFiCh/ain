#!/bin/bash

export LC_ALL=C
set -Eeuo pipefail

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR/../../"

    echo "::group::check-doc"
    test/lint/check-doc.py
    echo "::endgroup::"

    echo "::group::check-rpc-mappings"
    test/lint/check-rpc-mappings.py .
    echo "::endgroup::"

    test/lint/lint-all.sh
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
