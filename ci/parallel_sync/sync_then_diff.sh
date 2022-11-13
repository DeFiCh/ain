#!/bin/bash

# TODO: Resolve shellcheck errors

export LC_ALL=C
set -Eeuo pipefail

setup_vars() {
  # Binaries
  DEFID_BIN=${DEFID_BIN:-"./defid"}
  DEFI_CLI_BIN=${DEFI_CLI_BIN:-"./defi-cli"}

  # Files and directories
  DATADIR=${DATADIR:-".defi"}
  DEBUG_FILE="$DATADIR/debug.log"
  TMP_LOG=debug-tmp-$STOP_BLOCK.log
  BASE_PATH=https://storage.googleapis.com
  BUCKET=team-drop
  REF_LOG=debug-$STOP_BLOCK.log
  REF_LOG_PATH=$BASE_PATH/$BUCKET/$REF_LOG_DIR/$REF_LOG

  # Commands
  DEFID_CMD="$DEFID_BIN -datadir=$DATADIR -daemon -debug=accountchange -spv"
  DEFI_CLI_CMD="$DEFI_CLI_BIN -datadir=$DATADIR"
  FETCH="wget -q"
  GREP="grep"

  BLOCK=0
  ATTEMPTS=0
  MAX_ATTEMPTS=10
  MAX_NODE_RESTARTS=5
  NODE_RESTARTS=0
}

create_log_file () {
    echo "Output log to $TMP_LOG file"
    {
    $GREP "AccountChange:" $DEBUG_FILE | cut -d" " -f2-
    $DEFI_CLI_CMD logaccountbalances
    $DEFI_CLI_CMD spv_listanchors
    $DEFI_CLI_CMD logstoredinterests
    $DEFI_CLI_CMD listvaults '{"verbose": true}' '{"limit":1000000}'
    $DEFI_CLI_CMD listtokens '{"limit":1000000}'
    $DEFI_CLI_CMD getburninfo
    } >> $TMP_LOG
}

# Start defid
start_node () {
  echo "Syncing to block height: $STOP_BLOCK"
  $DEFID_CMD -interrupt-block=$((STOP_BLOCK + 1))
  sleep 30
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

      # Handle odd case where node get stuck on previously invalidated block
      $DEFI_CLI_CMD reconsiderblock "$($DEFI_CLI_CMD getbestblockhash)" || true
    else
      ATTEMPTS=0
    fi
    BLOCK=${CUR_BLOCK:-$BLOCK}
    echo "Current block: $BLOCK"
    sleep 20
  done

  # Create temporary log file
  create_log_file

  $DEFI_CLI_CMD stop
  # Download reference log file
  echo "Downloading reference log file : $REF_LOG_PATH"
  $FETCH $REF_LOG_PATH

  echo "diff $TMP_LOG $REF_LOG"
  diff $TMP_LOG $REF_LOG
}

main "$@"



