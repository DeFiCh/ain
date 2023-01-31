#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify IsMineCached is correct update
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class IsMineCachedTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # We need to enlarge -datacarriersize for allowing for test big OP_RETURN scripts
        # resulting from building AnyAccountsToAccounts msg with many accounts balances
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
        ]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(101)
        self.sync_blocks()

        wallet0_addr = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].utxostoaccount({wallet0_addr: "10@0"})
        self.nodes[0].generate(1)
        self.sync_blocks()

        to = {}
        wallet1_addr = self.nodes[1].getnewaddress("", "legacy")
        to[wallet1_addr] = ["10@0"]

        assert_raises_rpc_error(-5, None, self.nodes[0].sendtokenstoaddress, {}, to)

        self.nodes[0].importprivkey(self.nodes[1].dumpprivkey(wallet1_addr))

        self.nodes[0].sendtokenstoaddress({}, to)

if __name__ == '__main__':
    IsMineCachedTest().main()
