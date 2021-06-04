#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.
"""

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    assert_fee_amount,
    assert_raises_rpc_error,
    connect_nodes_bi,
    wait_until,
)

class JellyfishInitialFundsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # pass -jellyfish_regtest=1 option to enable regtest params required for jellyfish test containers
        self.extra_args = [['-jellyfish_regtest=1']]

    def run_test(self):
        node = self.nodes[0]
        # generate one block
        node.generate(1)

        #check balances. should have 200000100 DFI immature balance
        walletinfo = node.getwalletinfo()
        assert_equal(walletinfo['immature_balance'], 200000100)
        assert_equal(walletinfo['balance'], 0)

if __name__ == '__main__':
    JellyfishInitialFundsTest ().main ()
