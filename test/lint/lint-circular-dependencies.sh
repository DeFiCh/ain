#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

export LC_ALL=C

EXPECTED_CIRCULAR_DEPENDENCIES=(
    "chainparamsbase -> util/system -> chainparamsbase"
    "index/txindex -> validation -> index/txindex"
    "policy/fees -> txmempool -> policy/fees"
    "txmempool -> validation -> txmempool"
    "wallet/coincontrol -> wallet/wallet -> wallet/coincontrol"
    "wallet/fees -> wallet/wallet -> wallet/fees"
    "wallet/wallet -> wallet/walletdb -> wallet/wallet"
    "policy/fees -> txmempool -> validation -> policy/fees"
    "txmempool -> validation -> validationinterface -> txmempool"
    "wallet/ismine -> wallet/wallet -> wallet/ismine"
    "chainparams -> key_io -> chainparams"
    "chainparams -> masternodes/mn_checks -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/anchors -> chainparams"
    "chainparams -> masternodes/mn_checks -> txmempool -> validation -> chainparams"
    "chainparams -> masternodes/mn_checks -> txmempool -> validation -> pos -> chainparams"
    "chainparams -> masternodes/mn_checks -> txmempool -> validation -> wallet/wallet -> chainparams"
    "consensus/tx_verify -> masternodes/masternodes -> validation -> consensus/tx_verify"
    "consensus/tx_verify -> masternodes/mn_checks -> txmempool -> consensus/tx_verify"
    "masternodes/criminals -> masternodes/masternodes -> masternodes/criminals"
    "masternodes/criminals -> masternodes/masternodes -> validation -> masternodes/criminals"
    "masternodes/govvariables/lp_daily_dfi_reward -> masternodes/gv -> masternodes/govvariables/lp_daily_dfi_reward"
    "masternodes/govvariables/lp_splits -> masternodes/gv -> masternodes/govvariables/lp_splits"
    "masternodes/govvariables/lp_daily_dfi_reward -> rpc/util -> node/transaction -> validation -> masternodes/govvariables/lp_daily_dfi_reward"
    "masternodes/masternodes -> masternodes/mn_checks -> masternodes/masternodes"
    "masternodes/masternodes -> validation -> pos -> masternodes/masternodes"
    "masternodes/masternodes -> validation -> masternodes/masternodes"
    "masternodes/masternodes -> net_processing -> masternodes/masternodes"
    "masternodes/masternodes -> wallet/wallet -> masternodes/masternodes"
    "masternodes/mn_checks -> txmempool -> masternodes/mn_checks"
    "masternodes/mn_checks -> txmempool -> validation -> masternodes/mn_checks"
    "masternodes/mn_checks -> txmempool -> validation -> wallet/wallet -> masternodes/mn_checks"
    "masternodes/anchors -> spv/spv_wrapper -> masternodes/anchors"
    "masternodes/anchors -> validation -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> masternodes/mn_checks -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> net_processing -> masternodes/anchors"
    "net_processing -> validation -> net_processing"
    "validation -> wallet/wallet -> validation"
    "policy/fees -> txmempool -> validation -> wallet/wallet -> policy/fees"
    "policy/fees -> txmempool -> validation -> wallet/wallet -> util/fees -> policy/fees"
)

EXIT_CODE=0

CIRCULAR_DEPENDENCIES=()

IFS=$'\n'
for CIRC in $(cd src && ../contrib/devtools/circular-dependencies.py {*,*/*,*/*/*}.{h,cpp} | sed -e 's/^Circular dependency: //'); do
    CIRCULAR_DEPENDENCIES+=( "$CIRC" )
    IS_EXPECTED_CIRC=0
    for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_EXPECTED_CIRC} == 0 ]]; then
        echo "A new circular dependency in the form of \"${CIRC}\" appears to have been introduced."
        echo
        EXIT_CODE=1
    fi
done

for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
    IS_PRESENT_EXPECTED_CIRC=0
    for CIRC in "${CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_PRESENT_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_PRESENT_EXPECTED_CIRC} == 0 ]]; then
        echo "Good job! The circular dependency \"${EXPECTED_CIRC}\" is no longer present."
        echo "Please remove it from EXPECTED_CIRCULAR_DEPENDENCIES in $0"
        echo "to make sure this circular dependency is not accidentally reintroduced."
        echo
        EXIT_CODE=1
    fi
done

exit ${EXIT_CODE}
