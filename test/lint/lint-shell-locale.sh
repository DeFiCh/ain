#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
#
# Make sure all shell scripts:
# a.) explicitly opt out of locale dependence using
#     "export LC_ALL=C" or "export LC_ALL=C.UTF-8", or LC_ALL=en_US.UTF-8"
# b.) explicitly opt in to locale dependence using the annotation below.

export LC_ALL=C

EXIT_CODE=0

for SHELL_SCRIPT in $(git ls-files -- "*.sh" | grep -vE "src/(secp256k1|univalue)/"); do
    CURRENT_LINE_NO=0
    while IFS= read -r line; do
        if [[ ${line} == "# This script is intentionally locale dependent by not setting \"export LC_ALL=C\"" ]]; then
            continue 2
        fi
        if [[ ${line} =~ ^(#.*)?$ ]]; then
            continue
        fi
        if [[ ${line} == "export LC_ALL=C" ]] ||
            [[ ${line} == "export LC_ALL=C.UTF-8" ]] ||
            [[ ${line} == "export LC_ALL=en_US.UTF-8" ]]; then
            continue 2
        fi
        ((CURRENT_LINE_NO++))
        if [[ ${CURRENT_LINE_NO} > 10 ]]; then break; fi
    done <"$SHELL_SCRIPT"

    echo "Missing \"export LC_ALL=<C|C.UTF-8|en_US.UTF-8>\" (to avoid locale dependence) within first 10 non-empty lines in ${SHELL_SCRIPT}"
    EXIT_CODE=1
done
exit ${EXIT_CODE}
