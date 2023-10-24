#!/bin/bash

export LC_ALL=C.UTF-8
set -Eeuo pipefail

setup_vars() {
    # Binaries
    DEFID_BIN=${DEFID_BIN:-"./defid"}
    DEFI_CLI_BIN=${DEFI_CLI_BIN:-"./defi-cli"}

    # Files and directories
    DATADIR=${DATADIR:-".defi"}
    DEBUG_FILE="${DATADIR}/debug.log"
    CONF_FILE="${DATADIR}/defi.conf"
    TMP_LOG="debug-tmp-${STOP_BLOCK}.log"
    PRE_ROLLBACK_LOG="debug-pre-rollback.log"
    POST_ROLLBACK_LOG="debug-post-rollback.log"
    BASE_REF=${BASE_REF:-"master"}
    BASE_PATH=${BASE_PATH:-"https://storage.googleapis.com"}
    BUCKET=${BUCKET:-"team-drop"}
    REF_LOG="debug-${STOP_BLOCK}.log"
    REF_LOG_PATH="${BASE_PATH}/${BUCKET}/${BASE_REF}-datadir/log/${REF_LOG}"

    # Commands
    DEFID_CMD="${DEFID_BIN} -datadir=${DATADIR} -daemon -debug=accountchange -spv -checkpoints=0 -interrupt-block=$((STOP_BLOCK + 1))"
    DEFI_CLI_CMD="${DEFI_CLI_BIN} -datadir=${DATADIR}"
    FETCH="aria2c -x16 -s16"
    GREP="grep"

    ROLLBACK_BLOCK="${START_BLOCK}"
    BLOCK=0
    ATTEMPTS=0
    MAX_ATTEMPTS=10
    MAX_NODE_RESTARTS=5
    NODE_RESTARTS=0
    PID=""
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

print_info() {
    echo "======== Sync Test Info ==========
  - Block range: ${START_BLOCK} - ${STOP_BLOCK}

  - Reference log:
    ${REF_LOG_PATH}

  - snapshot:
   ${BASE_PATH}/${BUCKET}/${BASE_REF}-datadir/datadir-${START_BLOCK}.tar.gz

  - defid:
    ${DEFID_CMD}

  - defi-cli:
    ${DEFI_CLI_CMD}

  - Create log commands:
    ${GREP} \"AccountChange:\" \"${DEBUG_FILE}\" | cut -d\" \" -f2-
    ${DEFI_CLI_CMD} logaccountbalances
    ${DEFI_CLI_CMD} spv_listanchors
    ${DEFI_CLI_CMD} logstoredinterests
    ${DEFI_CLI_CMD} listvaults '{\"verbose\": true}' '{\"limit\":1000000}'
    ${DEFI_CLI_CMD} listtokens '{\"limit\":1000000}'
    ${DEFI_CLI_CMD} getburninfo

  - defi.conf:
    $(cat "$CONF_FILE")
  "
}
