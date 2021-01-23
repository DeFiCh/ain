#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
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

        print("Generating initial chain...")
        tokens = [
            {
                "wallet": self.nodes[0],
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 30
            },
        ]
        # inside this function "tokenId" and "symbolId" will be assigned for each token obj
        self.setup_tokens(tokens)

        token0_symbol = tokens[0]["symbolId"]

        to = {}
        wallet1_addr1 = self.nodes[1].getnewaddress("", "legacy")
        to[wallet1_addr1] = ["10@" + token0_symbol, "10@0"]

        assert_raises_rpc_error(-5, None, self.nodes[0].sendtokenstoaddress, {}, to)

        self.nodes[0].importprivkey(self.nodes[1].dumpprivkey(wallet1_addr1))

        #self.nodes[0].sendtokenstoaddress({}, to)

        self.nodes[0].unloadwallet('')
        self.nodes[0].loadwallet('')

        self.nodes[0].sendtokenstoaddress({}, to)


if __name__ == '__main__':
    IsMineCachedTest().main()
