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
    BASE_PATH="https://storage.googleapis.com"
    BUCKET="team-drop"
    REF_LOG="debug-${STOP_BLOCK}.log"
    REF_LOG_PATH="${BASE_PATH}/${BUCKET}/${REF_LOG_DIR}/${REF_LOG}"

    # Commands
    DEFID_CMD="${DEFID_BIN} -datadir=${DATADIR} -daemon -debug=accountchange -ethdebug=1 -spv -checkpoints=0 -interrupt-block=$((STOP_BLOCK + 1))"
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
    https://storage.googleapis.com/team-drop/${BASE_REF}-datadir/datadir-${START_BLOCK}.tar.gz

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
    ${DEFI_CLI_CMD} listgovs
    ${DEFI_CLI_CMD} logevmaccountstates

  - defi.conf:
    $(cat "$CONF_FILE")
  "
}

get_full_log() {
    { $GREP "AccountChange:" "$DEBUG_FILE" || test $? = 1; } | cut -d" " -f2-
    echo "-- logaccountbalances --"
    $DEFI_CLI_CMD logaccountbalances
    echo "-- spv_listanchors --"
    $DEFI_CLI_CMD spv_listanchors
    echo "-- logstoredinterests --"
    $DEFI_CLI_CMD logstoredinterests
    echo "-- listvaults --"
    $DEFI_CLI_CMD listvaults '{"verbose": true}' '{"limit":1000000}'
    echo "-- listtokens --"
    $DEFI_CLI_CMD listtokens '{"limit":1000000}'
    echo "-- getburninfo --"
    $DEFI_CLI_CMD getburninfo
    echo "-- listgovs --"
    $DEFI_CLI_CMD listgovs
    echo "-- logevmaccountstates --"
    $DEFI_CLI_CMD logevmaccountstates
}

rollback_and_log() {
    echo "ROLLBACK_BLOCK : ${ROLLBACK_BLOCK}"
    ROLLBACK_HASH=$($DEFI_CLI_CMD getblockhash $((ROLLBACK_BLOCK)))
    echo "ROLLBACK_HASH : ${ROLLBACK_HASH}"
    $DEFI_CLI_CMD invalidateblock "$ROLLBACK_HASH"
    echo "Rolled back to block : $($DEFI_CLI_CMD getblockcount)"

    get_full_log | grep -v "AccountChange"
}

create_pre_sync_rollback_log() {
    local DATADIR_ROLLBACK="${DATADIR}-rollback"
    local DEFID_CMD="${DEFID_BIN} -datadir=${DATADIR_ROLLBACK} -daemon -debug=accountchange -ethdebug=1 -spv -rpcport=9999 -port=9998 -connect=0 -checkpoints=0 -interrupt-block=$((START_BLOCK + 1))"
    local DEFI_CLI_CMD="${DEFI_CLI_BIN} -datadir=${DATADIR_ROLLBACK} -rpcport=9999"
    local DEBUG_FILE="${DATADIR_ROLLBACK}/debug.log"

    cp -r "$DATADIR" "$DATADIR_ROLLBACK"
    rm -f "$DEBUG_FILE"
    start_node_and_wait "$DATADIR_ROLLBACK"
    rollback_and_log > "$PRE_ROLLBACK_LOG"
    stop_node
}

start_node_and_wait() {
    local data_dir=${1:-${DATADIR}}
    echo "Syncing to block height: ${STOP_BLOCK}"
    $DEFID_CMD
    sleep 90

    # get PID
    PID=$(head -1 "./${data_dir}/defid.pid")
}

stop_node() {
    local ATTEMPTS=0

    # check to ensure defid process stops (50s timeout threshold)
    if [ -n "$PID" ]; then
        $DEFI_CLI_CMD stop
        while  ps -p "$PID" > /dev/null; do
            if [ "$ATTEMPTS" -gt "$MAX_ATTEMPTS" ]; then
                echo "Failed to stop node, exiting"
                exit 1
            else
                ATTEMPTS=$((ATTEMPTS + 1))
                sleep 5
            fi
        done
    fi
}

main() {
    _setup_dir_env
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR"

    setup_vars
    print_info
    create_pre_sync_rollback_log
    start_node_and_wait

    # Sync to target block height
    while [ "$BLOCK" -lt "$STOP_BLOCK" ]; do
        if [ "$ATTEMPTS" -gt "$MAX_ATTEMPTS" ]; then
            if [ "$NODE_RESTARTS" -lt "$MAX_NODE_RESTARTS" ]; then
                echo "Node Stuck After ${ATTEMPTS} attempts, restarting node"
                stop_node
                start_node_and_wait
                NODE_RESTARTS=$((NODE_RESTARTS + 1))
                ATTEMPTS=0
            else
                exit 1
            fi
        fi
        CUR_BLOCK=$($DEFI_CLI_CMD getblockcount || echo "$BLOCK")
        if [ "$CUR_BLOCK" -eq "$BLOCK" ]; then
            ATTEMPTS=$((ATTEMPTS + 1))

            # # Handle odd case where node get stuck on previously invalidated block
            $DEFI_CLI_CMD reconsiderblock "$($DEFI_CLI_CMD getbestblockhash)" || true
        else
            ATTEMPTS=0
        fi
        BLOCK=${CUR_BLOCK:-${BLOCK}}
        echo "Current block: ${BLOCK}"
        sleep 20
    done

    # Create temporary log file
    get_full_log >> "$TMP_LOG"

    # Create rollback log after sync
    rollback_and_log > "$POST_ROLLBACK_LOG"
    stop_node

    # Download reference log file
    echo "Downloading reference log file : ${REF_LOG_PATH}"
    $FETCH "$REF_LOG_PATH"
}

main "$@"
