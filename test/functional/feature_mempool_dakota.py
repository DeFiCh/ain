#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test account mining behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class MempoolDakotaTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
                            ['-txnotokens=0', '-amkheight=50', '-dakotaheight=100'],
                            ['-txnotokens=0', '-amkheight=50', '-dakotaheight=100'],
                            ['-txnotokens=0', '-amkheight=50', '-dakotaheight=100'],
                          ]

    def run_test(self):
        node = self.nodes[0]
        node1 = self.nodes[1]
        node.generate(101)
        self.sync_blocks()

        assert_equal(node.getblockcount(), 101) # Dakota height

        # Get addresses and set up account
        wallet1_addr = node1.getnewaddress("", "legacy")
        node.sendtoaddress(wallet1_addr, "3.1")
        node.generate(1)
        self.sync_blocks()
        collateral = node1.createmasternode(wallet1_addr)
        assert_raises_rpc_error(-26, "collateral-locked", node1.utxostoaccount, {wallet1_addr: "0.09@0"})
        self.sync_mempools()
        node.generate(1)
        self.sync_blocks()
        assert_equal(node1.listmasternodes({}, False)[collateral], "PRE_ENABLED")
        assert_equal(node1.getmasternode(collateral)[collateral]["state"], "PRE_ENABLED")
        node.generate(10)
        assert_equal(node.listmasternodes({}, False)[collateral], "ENABLED")

        node1.utxostoaccount({wallet1_addr: "0.09@0"})
        node.generate(1)
        self.sync_blocks()

if __name__ == '__main__':
    MempoolDakotaTest().main ()
