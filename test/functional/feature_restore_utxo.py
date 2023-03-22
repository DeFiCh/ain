#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test restoration of UTXOs"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, disconnect_nodes


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

        self.nodes[0].minttokens("2@BTC")
        self.nodes[0].generate(1)

        node1_source = self.nodes[1].getnewaddress("", "legacy")

        # Fund UTXO
        self.nodes[0].sendtoaddress(node1_source, 1)

        # Fund account
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {node1_source: "1@BTC"})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check UTXO
        assert_equal(len(self.nodes[1].listunspent()), 1)

        # Create account Tx
        node1_destination = self.nodes[1].getnewaddress("", "legacy")
        self.nodes[1].accounttoaccount(node1_source, {node1_destination: "1@BTC"})

        # Clear mempool
        self.nodes[1].clearmempool()

        # Make sure that UTXO has restored
        assert_equal(len(self.nodes[1].listunspent()), 1)

        # Create another account Tx
        self.nodes[1].accounttoaccount(node1_source, {node1_destination: "1@BTC"})

        # Clear mempool
        self.nodes[1].clearmempool()

        # Set up for rollback tests
        disconnect_nodes(self.nodes[0], 1)
        block = self.nodes[1].getblockcount()
        node1_utxos = len(self.nodes[1].listunspent())

        # Test rollbacks
        for _ in range(2):
            for _ in range(5):
                self.nodes[1].accounttoaccount(node1_source, {node1_source: "1@BTC"})
                self.nodes[1].generate(1)
                self.nodes[1].accounttoaccount(node1_source, {node1_source: "1@BTC"})
                self.nodes[1].generate(1)
            self.rollback_to(block)
            assert_equal(len(self.nodes[1].listunspent()), node1_utxos)


if __name__ == '__main__':
    TestRestoreUTXOs().main()
