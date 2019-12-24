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
    "qt/addresstablemodel -> qt/walletmodel -> qt/addresstablemodel"
    "qt/bantablemodel -> qt/clientmodel -> qt/bantablemodel"
    "qt/bitcoingui -> qt/utilitydialog -> qt/bitcoingui"
    "qt/bitcoingui -> qt/walletframe -> qt/bitcoingui"
    "qt/bitcoingui -> qt/walletview -> qt/bitcoingui"
    "qt/clientmodel -> qt/peertablemodel -> qt/clientmodel"
    "qt/paymentserver -> qt/walletmodel -> qt/paymentserver"
    "qt/recentrequeststablemodel -> qt/walletmodel -> qt/recentrequeststablemodel"
    "qt/sendcoinsdialog -> qt/walletmodel -> qt/sendcoinsdialog"
    "qt/transactiontablemodel -> qt/walletmodel -> qt/transactiontablemodel"
    "qt/walletmodel -> qt/walletmodeltransaction -> qt/walletmodel"
    "txmempool -> validation -> txmempool"
    "wallet/coincontrol -> wallet/wallet -> wallet/coincontrol"
    "wallet/fees -> wallet/wallet -> wallet/fees"
    "wallet/wallet -> wallet/walletdb -> wallet/wallet"
    "policy/fees -> txmempool -> validation -> policy/fees"
    "qt/guiutil -> qt/walletmodel -> qt/optionsmodel -> qt/guiutil"
    "txmempool -> validation -> validationinterface -> txmempool"
    "wallet/ismine -> wallet/wallet -> wallet/ismine"
    "chainparams -> key_io -> chainparams"
    "chainparams -> masternodes/masternodes -> chainparams"
    "masternodes/mn_checks -> txmempool -> masternodes/mn_checks"
    "chainparams -> masternodes/masternodes -> validation -> chainparams"
    "consensus/tx_verify -> masternodes/masternodes -> validation -> consensus/tx_verify"
    "masternodes/masternodes -> validation -> masternodes/mn_checks -> masternodes/masternodes"
    "masternodes/masternodes -> validation -> pos -> masternodes/masternodes"
    "masternodes/mn_checks -> txmempool -> validation -> masternodes/mn_checks"
    "chainparams -> masternodes/masternodes -> validation -> pos -> chainparams"
    "masternodes/anchors -> spv/spv_wrapper -> masternodes/anchors"
    "masternodes/anchors -> validation -> masternodes/anchors"
    "net_processing -> validation -> net_processing"
    "validation -> wallet/wallet -> validation"
    "masternodes/masternodes -> validation -> wallet/wallet -> masternodes/masternodes"
    "chainparams -> masternodes/masternodes -> validation -> wallet/wallet -> chainparams"
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
