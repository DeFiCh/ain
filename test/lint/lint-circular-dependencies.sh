#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

export LC_ALL=C

EXPECTED_CIRCULAR_DEPENDENCIES=(
    "chain -> chainparams -> masternodes/mn_checks -> index/txindex -> chain"
    "chain -> chainparams -> masternodes/mn_checks -> index/txindex -> txdb -> chain"
    "chain -> chainparams -> masternodes/mn_checks -> masternodes/vaulthistory -> chain"
    "chain -> chainparams -> masternodes/mn_checks -> validation -> chain"
    "chain -> chainparams -> masternodes/mn_checks -> validation -> versionbits -> chain"
    "chain -> chainparams -> masternodes/mn_checks -> validation -> wallet/wallet -> chain"
    "chainparams -> key_io -> chainparams"
    "chainparams -> masternodes/mn_checks -> index/txindex -> index/base -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/consensus/accounts -> masternodes/consensus/txvisitor -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/consensus/smartcontracts -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/customtx -> chainparams"
    "chainparams -> masternodes/mn_checks -> masternodes/vaulthistory -> masternodes/vault -> chainparams"
    "chainparams -> masternodes/mn_checks -> txmempool -> chainparams"
    "chainparams -> masternodes/mn_checks -> validation -> chainparams"
    "chainparams -> masternodes/mn_checks -> validation -> spv/spv_wrapper -> chainparams"
    "chainparams -> masternodes/mn_checks -> validation -> wallet/wallet -> chainparams"
    "chainparamsbase -> util/system -> chainparamsbase"
    "consensus/tx_check -> masternodes/customtx -> consensus/tx_check"
    "consensus/tx_verify -> masternodes/masternodes -> validation -> consensus/tx_verify"
    "consensus/tx_verify -> masternodes/mn_checks -> txmempool -> consensus/tx_verify"
    "index/txindex -> validation -> index/txindex"
    "init -> masternodes/govvariables/attributes -> masternodes/mn_rpc -> wallet/rpcwallet -> init"
    "masternodes/accountshistory -> masternodes/historywriter -> masternodes/accountshistory"
    "masternodes/accountshistory -> masternodes/historywriter -> masternodes/mn_checks -> masternodes/accountshistory"
    "masternodes/accountshistory -> masternodes/masternodes -> masternodes/accountshistory"
    "masternodes/accountshistory -> masternodes/masternodes -> validation -> masternodes/accountshistory"
    "masternodes/anchors -> masternodes/masternodes -> masternodes/anchors"
    "masternodes/anchors -> masternodes/masternodes -> net_processing -> masternodes/anchors"
    "masternodes/anchors -> spv/spv_wrapper -> masternodes/anchors"
    "masternodes/consensus/accounts -> masternodes/consensus/txvisitor -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/accounts"
    "masternodes/consensus/governance -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/governance"
    "masternodes/consensus/icxorders -> masternodes/mn_checks -> masternodes/consensus/icxorders"
    "masternodes/consensus/loans -> masternodes/mn_checks -> masternodes/consensus/loans"
    "masternodes/consensus/masternodes -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/masternodes"
    "masternodes/consensus/oracles -> masternodes/masternodes -> masternodes/mn_checks -> masternodes/consensus/oracles"
    "masternodes/consensus/poolpairs -> masternodes/mn_checks -> masternodes/consensus/poolpairs"
    "masternodes/consensus/proposals -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/proposals"
    "masternodes/consensus/smartcontracts -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/smartcontracts"
    "masternodes/consensus/tokens -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/tokens"
    "masternodes/consensus/vaults -> masternodes/mn_checks -> masternodes/consensus/vaults"
    "masternodes/consensus/xvm -> masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/consensus/xvm"
    "masternodes/consensus/xvm -> masternodes/govvariables/attributes -> masternodes/mn_rpc -> masternodes/consensus/xvm"
    "masternodes/consensus/xvm -> masternodes/masternodes -> masternodes/evm -> masternodes/consensus/xvm"
    "masternodes/govvariables/attributes -> masternodes/gv -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> masternodes/historywriter -> masternodes/loan -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> masternodes/masternodes -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> masternodes/masternodes -> masternodes/poolpairs -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> masternodes/mn_checks -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> masternodes/mn_rpc -> masternodes/govvariables/attributes"
    "masternodes/govvariables/attributes -> validation -> masternodes/govvariables/attributes"
    "masternodes/govvariables/icx_takerfee_per_btc -> masternodes/gv -> masternodes/govvariables/icx_takerfee_per_btc"
    "masternodes/govvariables/loan_daily_reward -> masternodes/gv -> masternodes/govvariables/loan_daily_reward"
    "masternodes/govvariables/loan_liquidation_penalty -> masternodes/gv -> masternodes/govvariables/loan_liquidation_penalty"
    "masternodes/govvariables/loan_splits -> masternodes/gv -> masternodes/govvariables/loan_splits"
    "masternodes/govvariables/lp_daily_dfi_reward -> masternodes/gv -> masternodes/govvariables/lp_daily_dfi_reward"
    "masternodes/govvariables/lp_splits -> masternodes/gv -> masternodes/govvariables/lp_splits"
    "masternodes/govvariables/oracle_block_interval -> masternodes/gv -> masternodes/govvariables/oracle_block_interval"
    "masternodes/govvariables/oracle_deviation -> masternodes/gv -> masternodes/govvariables/oracle_deviation"
    "masternodes/historywriter -> masternodes/masternodes -> masternodes/historywriter"
    "masternodes/historywriter -> masternodes/masternodes -> validation -> masternodes/historywriter"
    "masternodes/loan -> masternodes/masternodes -> masternodes/loan"
    "masternodes/masternodes -> masternodes/mn_checks -> masternodes/masternodes"
    "masternodes/masternodes -> masternodes/oracles -> masternodes/masternodes"
    "masternodes/masternodes -> masternodes/proposals -> masternodes/masternodes"
    "masternodes/masternodes -> masternodes/vaulthistory -> masternodes/masternodes"
    "masternodes/masternodes -> net_processing -> masternodes/masternodes"
    "masternodes/masternodes -> wallet/wallet -> masternodes/masternodes"
    "masternodes/mn_checks -> txmempool -> masternodes/mn_checks"
    "masternodes/mn_checks -> validation -> masternodes/mn_checks"
    "masternodes/mn_checks -> validation -> wallet/wallet -> masternodes/mn_checks"
    "masternodes/mn_rpc -> rpc/resultcache -> masternodes/mn_rpc"
    "masternodes/validation -> validation -> masternodes/validation"
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
