#!/bin/bash

export LC_ALL=C
set -Eeuo pipefail

setup_vars() {
  # Binaries
  DEFID_BIN=${DEFID_BIN:-"./defid"}
  DEFI_CLI_BIN=${DEFI_CLI_BIN:-"./defi-cli"}

  # Files and directories
  DATADIR=${DATADIR:-".defi"}
  DEBUG_FILE="$DATADIR/debug.log"
  CONF_FILE="$DATADIR/defi.conf"
  TMP_LOG=debug-tmp-$STOP_BLOCK.log
  BASE_PATH=https://storage.googleapis.com
  BUCKET=team-drop
  REF_LOG_DIR=master-logs-full
  REF_LOG=debug-$STOP_BLOCK.log
  REF_LOG_PATH=$BASE_PATH/$BUCKET/$REF_LOG_DIR/$REF_LOG

  # Commands
  DEFID_CMD="$DEFID_BIN -datadir=$DATADIR -daemon -debug=accountchange"
  DEFI_CLI_CMD="$DEFI_CLI_BIN -datadir=$DATADIR"
  ACCOUNT_BALANCES_CMD="$DEFI_CLI_CMD logaccountbalances"
  LIST_ANCHORS_CMD="$DEFI_CLI_CMD spv_listanchors"
  GREP="grep"

  BLOCK=0
  ATTEMPTS=0
  START_BLOCK=${START_BLOCK:-0}
  STOP_BLOCK=${STOP_BLOCK:-0}
  MAX_ATTEMPTS=10
  MAX_NODE_RESTARTS=5
  NODE_RESTARTS=0
}

print_info() {
  echo "======== Sync Test Info ==========
  - Block range: ${START_BLOCK} - ${STOP_BLOCK}
  - Base snapshot: https://gcr.io/br-blockchains-dev/datadir-${START_BLOCK}
  - Reference logs:
    - debug.log: $REF_LOG_PATH
  - Commands used:
   - $ACCOUNT_BALANCES_CMD
   - $LIST_ANCHORS_CMD
  - defid cmd: ${DEFI_CLI_CMD}
  - defi.conf:
    $(cat "$CONF_FILE")
  ----------------------------------
  "
}

print_info

# Start defid
start_node () {
  echo "Syncing to block height: $STOP_BLOCK"
  $DEFID_CMD
  sleep 30
  update_tip_node
}

update_tip () {
  CUR_TIP=$($DEFI_CLI_CMD "$DEFI_CLI_CMD" getblockchaininfo | jq .headers || echo "$STOP_BLOCK")
  if [ "$CUR_TIP" -gt "$STOP_BLOCK" ]; then
    echo "New Tip: $STOP_BLOCK"
    STOP_BLOCK=${CUR_TIP:-$STOP_BLOCK}
  fi
}

main() {
  setup_vars
  start_node

  $DEFI_CLI_CMD clearbanned || true

  # Sync to target block height
  while [ "$BLOCK" -lt "$STOP_BLOCK" ]; do
    if [ "$ATTEMPTS" -gt "$MAX_ATTEMPTS" ]; then
      if [ "$NODE_RESTARTS" -lt "$MAX_NODE_RESTARTS" ]; then
        echo "Node Stuck After $ATTEMPTS attempts, restarting node"
        $DEFI_CLI_CMD stop
        sleep 20
        start_node
        NODE_RESTARTS=$((NODE_RESTARTS + 1))
        ATTEMPTS=0
      else
        exit 1
      fi
    fi
    CUR_BLOCK=$($DEFI_CLI_CMD getblockcount || echo $BLOCK)
    if [ "$CUR_BLOCK" -eq "$BLOCK" ]; then
      ATTEMPTS=$((ATTEMPTS + 1))
    else
      ATTEMPTS=0
    fi
    BLOCK=${CUR_BLOCK:-$BLOCK}
    echo "Current block: $BLOCK / $STOP_BLOCK"
    sleep 20
    update_tip
  done

  # Create temporary log file
  $GREP "AccountChange:" "$DEBUG_FILE" | cut -d" " -f2- > "$TMP_LOG"
  $ACCOUNT_BALANCES_CMD >> "$TMP_LOG"
  $LIST_ANCHORS_CMD >> "$TMP_LOG"
  $DEFI_CLI_CMD stop
  cat "$TMP_LOG"
}

main "$@"



