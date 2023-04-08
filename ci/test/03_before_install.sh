#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

# Add llvm-symbolizer directory to PATH. Needed to get symbolized stack traces from the sanitizers.
PATH=$PATH:/usr/lib/llvm-15/bin/
export PATH

BEGIN_FOLD () {
  echo ""
  CURRENT_FOLD_NAME=$1
  # GitHub CI group
  echo "::group::${CURRENT_FOLD_NAME}"
}

END_FOLD () {
  RET=$?
  echo "::endgroup::"
  if [ $RET != 0 ]; then
    echo "${CURRENT_FOLD_NAME} failed with status code ${RET}"
  fi
}

