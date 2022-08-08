#!/bin/bash
set -Eeuo pipefail

# Binaries
DEFID_BIN=./defid
DEFI_CLI_BIN=./defi-cli

# Commands
DEFID_CMD="$DEFID_BIN -daemon -debug=accountchange"
DEFI_CLI_CMD="$DEFI_CLI_BIN"
ACCOUNT_BALANCES_CMD="$DEFI_CLI_CMD logaccountbalances"
LIST_ANCHORS_CMD="$DEFI_CLI_CMD spv_listanchors"
FETCH=wget
GREP=grep

# Files and directories
DEBUG_FILE=".defi/debug.log"
TMP_LOG=debug-tmp-$STOP_BLOCK.log
BASE_PATH=https://storage.googleapis.com
BUCKET=team-drop
REF_LOG_DIR=master-log-full
REF_LOG=debug-$STOP_BLOCK.log
REF_LOG_PATH=$BASE_PATH/$BUCKET/$REF_LOG_DIR/$REF_LOG

# Start defid
echo "Syncing to block height: $STOP_BLOCK"
$DEFID_CMD -interrupt-block=$((STOP_BLOCK + 1))
sleep 30

BLOCK=0
# Sync to target block height
while [ "$BLOCK" -lt "$STOP_BLOCK" ]; do
  BLOCK=$($DEFI_CLI_CMD getblockcount)
  echo "Current block: $BLOCK"
  sleep 20
done

# Create temporary log file
$GREP "AccountChange:" $DEBUG_FILE | cut -d" " -f2- > $TMP_LOG
$ACCOUNT_BALANCES_CMD >> $TMP_LOG
$LIST_ANCHORS_CMD >> $TMP_LOG

# Download reference log file
$FETCH $REF_LOG_PATH

diff $TMP_LOG $REF_LOG
