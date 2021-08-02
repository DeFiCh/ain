#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
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
    "chainparams -> masternodes/mn_checks -> validation -> chainparams"
    "chainparams -> masternodes/mn_checks -> index/txindex -> index/base -> chainparams"
    "chainparams -> masternodes/mn_checks -> validation -> spv/spv_wrapper -> chainparams"
    "chainparams -> masternodes/mn_checks -> validation -> wallet/wallet -> chainparams"
    "chainparams -> masternodes/mn_checks -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/anchors -> chainparams"
    "consensus/tx_verify -> masternodes/masternodes -> validation -> consensus/tx_verify"
    "consensus/tx_verify -> masternodes/mn_checks -> txmempool -> consensus/tx_verify"
    "masternodes/govvariables/lp_daily_dfi_reward -> masternodes/gv -> masternodes/govvariables/lp_daily_dfi_reward"
    "masternodes/govvariables/lp_daily_dfi_reward -> masternodes/masternodes -> validation -> masternodes/govvariables/lp_daily_dfi_reward"
    "masternodes/govvariables/lp_splits -> masternodes/gv -> masternodes/govvariables/lp_splits"
    "masternodes/govvariables/icx_takerfee_per_btc -> masternodes/gv -> masternodes/govvariables/icx_takerfee_per_btc"
    "masternodes/masternodes -> masternodes/mn_checks -> masternodes/masternodes"
    "masternodes/masternodes -> validation -> masternodes/masternodes"
    "masternodes/masternodes -> net_processing -> masternodes/masternodes"
    "masternodes/masternodes -> wallet/wallet -> masternodes/masternodes"
    "masternodes/mn_checks -> txmempool -> masternodes/mn_checks"
    "masternodes/mn_checks -> validation -> masternodes/mn_checks"
    "masternodes/mn_checks -> validation -> wallet/wallet -> masternodes/mn_checks"
    "masternodes/anchors -> spv/spv_wrapper -> masternodes/anchors"
    "masternodes/anchors -> validation -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> masternodes/mn_checks -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> net_processing -> masternodes/anchors"
    "masternodes/accountshistory -> masternodes/masternodes -> masternodes/mn_checks -> masternodes/accountshistory"
    "masternodes/accountshistory -> masternodes/masternodes -> validation -> masternodes/accountshistory"
    "net_processing -> validation -> net_processing"
    "validation -> wallet/wallet -> validation"
    "policy/fees -> txmempool -> validation -> wallet/wallet -> policy/fees"
    "policy/fees -> txmempool -> validation -> wallet/wallet -> util/fees -> policy/fees"
    "chainparams -> masternodes/mn_checks -> txmempool -> chainparams"
    "pos_kernel -> validation -> pos_kernel"
    "pos -> validation -> pos"
    "pos -> validation -> txdb -> pos"
    "pos_kernel -> validation -> txdb -> pos_kernel"
    "pos -> pos_kernel -> pos"
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
