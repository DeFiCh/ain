#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

export LC_ALL=C

EXPECTED_CIRCULAR_DEPENDENCIES=(
    "chain -> chainparams -> dfi/mn_checks -> index/txindex -> chain"
    "chain -> chainparams -> dfi/mn_checks -> index/txindex -> txdb -> chain"
    "chain -> chainparams -> dfi/mn_checks -> dfi/vaulthistory -> chain"
    "chain -> chainparams -> dfi/mn_checks -> validation -> chain"
    "chain -> chainparams -> dfi/mn_checks -> validation -> versionbits -> chain"
    "chain -> chainparams -> dfi/mn_checks -> validation -> wallet/wallet -> chain"
    "chainparams -> key_io -> chainparams"
    "chainparams -> dfi/mn_checks -> index/txindex -> index/base -> chainparams"
    "chainparams -> dfi/mn_checks -> dfi/customtx -> chainparams"
    "chainparams -> dfi/mn_checks -> dfi/vaulthistory -> dfi/vault -> chainparams"
    "chainparams -> dfi/mn_checks -> txmempool -> chainparams"
    "chainparams -> dfi/mn_checks -> validation -> chainparams"
    "chainparams -> dfi/mn_checks -> validation -> spv/spv_wrapper -> chainparams"
    "chainparams -> dfi/mn_checks -> validation -> wallet/wallet -> chainparams"
    "chainparamsbase -> util/system -> chainparamsbase"
    "consensus/tx_verify -> dfi/mn_checks -> txmempool -> consensus/tx_verify"
    "consensus/tx_verify -> validation -> consensus/tx_verify"
    "index/txindex -> validation -> index/txindex"
    "dfi/accountshistory -> dfi/historywriter -> dfi/accountshistory"
    "dfi/accountshistory -> dfi/historywriter -> dfi/mn_checks -> dfi/accountshistory"
    "dfi/accountshistory -> dfi/masternodes -> dfi/accountshistory"
    "dfi/accountshistory -> dfi/masternodes -> validation -> dfi/accountshistory"
    "dfi/accountshistory -> flushablestorage -> dfi/snapshotmanager -> dfi/accountshistory"
    "dfi/anchors -> dfi/masternodes -> dfi/anchors"
    "dfi/anchors -> dfi/masternodes -> net_processing -> dfi/anchors"
    "dfi/anchors -> spv/spv_wrapper -> dfi/anchors"
    "dfi/consensus/accounts -> dfi/mn_checks -> dfi/consensus/accounts"
    "dfi/consensus/governance -> dfi/mn_checks -> dfi/consensus/governance"
    "dfi/consensus/icxorders -> dfi/mn_checks -> dfi/consensus/icxorders"
    "dfi/consensus/loans -> dfi/mn_checks -> dfi/consensus/loans"
    "dfi/consensus/masternodes -> dfi/mn_checks -> dfi/consensus/masternodes"
    "dfi/consensus/oracles -> dfi/mn_checks -> dfi/consensus/oracles"
    "dfi/consensus/poolpairs -> dfi/mn_checks -> dfi/consensus/poolpairs"
    "dfi/consensus/proposals -> dfi/mn_checks -> dfi/consensus/proposals"
    "dfi/consensus/smartcontracts -> dfi/mn_checks -> dfi/consensus/smartcontracts"
    "dfi/consensus/tokens -> dfi/mn_checks -> dfi/consensus/tokens"
    "dfi/consensus/vaults -> dfi/mn_checks -> dfi/consensus/vaults"
    "dfi/consensus/xvm -> dfi/mn_checks -> dfi/consensus/xvm"
    "dfi/consensus/xvm -> dfi/govvariables/attributes -> dfi/evm -> dfi/consensus/xvm"
    "dfi/consensus/xvm -> dfi/govvariables/attributes -> dfi/mn_rpc -> dfi/consensus/xvm"
    "dfi/govvariables/attributes -> dfi/gv -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/historywriter -> dfi/loan -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/masternodes -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/masternodes -> dfi/poolpairs -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/mn_checks -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/mn_checks -> txmempool -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/mn_rpc -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/mn_rpc -> wallet/rpcwallet -> init -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/validation -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/validation -> ffi/ffiexports -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> validation -> dfi/govvariables/attributes"
    "dfi/govvariables/icx_takerfee_per_btc -> dfi/gv -> dfi/govvariables/icx_takerfee_per_btc"
    "dfi/govvariables/loan_daily_reward -> dfi/gv -> dfi/govvariables/loan_daily_reward"
    "dfi/govvariables/loan_liquidation_penalty -> dfi/gv -> dfi/govvariables/loan_liquidation_penalty"
    "dfi/govvariables/loan_splits -> dfi/gv -> dfi/govvariables/loan_splits"
    "dfi/govvariables/lp_daily_dfi_reward -> dfi/gv -> dfi/govvariables/lp_daily_dfi_reward"
    "dfi/govvariables/lp_splits -> dfi/gv -> dfi/govvariables/lp_splits"
    "dfi/govvariables/oracle_block_interval -> dfi/gv -> dfi/govvariables/oracle_block_interval"
    "dfi/govvariables/oracle_deviation -> dfi/gv -> dfi/govvariables/oracle_deviation"
    "dfi/historywriter -> dfi/masternodes -> dfi/historywriter"
    "dfi/historywriter -> dfi/masternodes -> validation -> dfi/historywriter"
    "dfi/loan -> dfi/masternodes -> dfi/loan"
    "dfi/masternodes -> dfi/mn_checks -> dfi/masternodes"
    "dfi/masternodes -> dfi/oracles -> dfi/masternodes"
    "dfi/masternodes -> dfi/proposals -> dfi/masternodes"
    "dfi/masternodes -> dfi/vaulthistory -> dfi/masternodes"
    "dfi/masternodes -> flushablestorage -> dfi/snapshotmanager -> dfi/masternodes"
    "dfi/masternodes -> net_processing -> dfi/masternodes"
    "dfi/masternodes -> wallet/wallet -> dfi/masternodes"
    "dfi/mn_checks -> txmempool -> dfi/mn_checks"
    "dfi/mn_checks -> validation -> dfi/mn_checks"
    "dfi/mn_checks -> validation -> wallet/wallet -> dfi/mn_checks"
    "dfi/mn_rpc -> wallet/rpcwallet -> init -> ffi/ffiexports -> dfi/mn_rpc"
    "dfi/govvariables/attributes -> dfi/mn_rpc -> wallet/rpcwallet -> init -> miner -> dfi/govvariables/attributes"
    "dfi/govvariables/attributes -> dfi/mn_rpc -> wallet/rpcwallet -> init -> rpc/blockchain -> dfi/govvariables/attributes"
    "dfi/mn_rpc -> wallet/rpcwallet -> init -> miner -> dfi/validation -> dfi/mn_rpc"
    "dfi/snapshotmanager -> dfi/vaulthistory -> flushablestorage -> dfi/snapshotmanager"
    "dfi/validation -> validation -> dfi/validation"
    "dfi/validation -> ffi/ffiexports -> dfi/validation"
    "logging -> util/system -> logging"
    "logging -> util/system -> sync -> logging"
    "miner -> wallet/wallet -> policy/fees -> miner"
    "net_processing -> validation -> net_processing"
    "policy/fees -> txmempool -> policy/fees"
    "policy/fees -> validation -> policy/fees"
    "policy/fees -> validation -> wallet/wallet -> policy/fees"
    "policy/fees -> validation -> wallet/wallet -> util/fees -> policy/fees"
    "pos -> pos_kernel -> pos"
    "pos -> validation -> pos"
    "pos -> validation -> txdb -> pos"
    "pos_kernel -> validation -> pos_kernel"
    "pos_kernel -> validation -> txdb -> pos_kernel"
    "pubkey -> script/standard -> pubkey"
    "pubkey -> script/standard -> script/interpreter -> pubkey"
    "rpc/resultcache -> validation -> rpc/resultcache"
    "spv/support/BRAddress -> spv/support/BRBech32 -> spv/support/BRAddress"
    "spv/bitcoin/BRChainParams -> spv/bitcoin/BRMerkleBlock -> spv/support/BRAddress -> spv/bitcoin/BRChainParams"
    "spv/bitcoin/BRChainParams -> spv/bitcoin/BRPeer -> spv/bitcoin/BRTransaction -> spv/support/BRKey -> spv/bitcoin/BRChainParams"
    "txmempool -> validation -> txmempool"
    "txmempool -> validation -> validationinterface -> txmempool"
    "validation -> wallet/wallet -> validation"
    "wallet/coincontrol -> wallet/wallet -> wallet/coincontrol"
    "wallet/fees -> wallet/wallet -> wallet/fees"
    "wallet/ismine -> wallet/wallet -> wallet/ismine"
    "wallet/wallet -> wallet/walletdb -> wallet/wallet"
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

