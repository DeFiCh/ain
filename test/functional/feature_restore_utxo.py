#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test restoration of UTXOs"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

class TestRestoreUTXOs(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1'],
                           ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1']]

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_blocks()

        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "Bitcoin",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("1@BTC")
        self.nodes[0].generate(1)

        source = self.nodes[1].getnewaddress("", "legacy")

        # Fund UTXO
        self.nodes[0].sendtoaddress(source, 1)

        # Fund account
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {source: "1@BTC"})
        self.nodes[0].generate(1)

        # Check UTXO
        assert_equal(len(self.nodes[1].listunspent()), 1)

        # Create account Tx
        destination = self.nodes[1].getnewaddress("", "legacy")
        self.nodes[1].accounttoaccount(source, {destination: "1@BTC"})

        # Clear mempool
        self.nodes[1].clearmempool()

        # Make sure that UTXO has restored
        assert_equal(len(self.nodes[1].listunspent()), 1)

        # Create another account Tx
        self.nodes[1].accounttoaccount(source, {destination: "1@BTC"})

if __name__ == '__main__':
    TestRestoreUTXOs().main()
