#!/bin/bash

export LC_ALL=C
set -Eeuo pipefail

# Binaries
DEFID_BIN=${DEFID_BIN:-"./defid"}
DEFI_CLI_BIN=${DEFI_CLI_BIN:-"./defi-cli"}

# Files and directories
DATADIR=${DATADIR:-".defi"}
DEBUG_FILE="$DATADIR/debug.log"
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
FETCH="wget -q"
GREP="grep"

# Start defid
START_NODE () {
  echo "Syncing to block height: $STOP_BLOCK"
  $DEFID_CMD -interrupt-block=$((STOP_BLOCK + 1))
  sleep 30
}

START_NODE

$DEFI_CLI_CMD clearbanned || true

BLOCK=0
ATTEMPTS=0
MAX_ATTEMPTS=10
MAX_NODE_RESTARTS=5
NODE_RESTARTS=0

# Sync to target block height
while [ "$BLOCK" -lt "$STOP_BLOCK" ]; do
  if [ "$ATTEMPTS" -gt "$MAX_ATTEMPTS" ]; then
    if [ "$NODE_RESTARTS" -lt "$MAX_NODE_RESTARTS" ]; then
      echo "Node Stuck After $ATTEMPTS attempts, restarting node"
      $DEFI_CLI_CMD stop
      sleep 20
      START_NODE
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
  echo "Current block: $BLOCK"
  sleep 20
done

# Create temporary log file
$GREP "AccountChange:" $DEBUG_FILE | cut -d" " -f2- > $TMP_LOG
$ACCOUNT_BALANCES_CMD >> $TMP_LOG
$LIST_ANCHORS_CMD >> $TMP_LOG

$DEFI_CLI_CMD stop
# Download reference log file
echo "Downloading reference log file : $REF_LOG_PATH"
$FETCH $REF_LOG_PATH

echo "diff $TMP_LOG $REF_LOG"
diff $TMP_LOG $REF_LOG
