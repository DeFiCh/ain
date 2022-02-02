#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test restoration of UTXOs"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
import time

class TestRestoreUTXOs(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1'],
                           ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1']]

    def clearmempool(self, node: int):
        try:
            self.nodes[node].clearmempool()
        except JSONRPCException as e:
            return False
        return True

    def account_to_account(self, node: int, soure, destination):
        try:
            self.nodes[node].accounttoaccount(soure, {destination: "1@BTC"})
        except JSONRPCException as e:
            return False
        return True

    def account_to_account_loop(self, node: int, soure, destination):
        count = 0
        while not self.account_to_account(node, soure, destination):
            if count == 5:
                return False
            else:
                count += 1
                time.sleep(1)
        return True


    def rollback(self, count):
        block = self.nodes[0].getblockhash(count)
        self.nodes[0].invalidateblock(block)
        self.nodes[1].invalidateblock(block)
        assert(len(self.nodes[0].getrawmempool()) > 0)
        assert(len(self.nodes[1].getrawmempool()) > 0)
        while not self.clearmempool(0):
            pass
        while not self.clearmempool(1):
            pass
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        assert_equal(len(self.nodes[1].getrawmempool()), 0)
        assert_equal(self.nodes[0].getblockcount(), count - 1)
        assert_equal(self.nodes[1].getblockcount(), count - 1)
        self.sync_blocks()

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
        node0_source = self.nodes[0].getnewaddress("", "legacy")
        node0_destination = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {node0_source: "1@BTC"})
        self.nodes[0].generate(1)
        self.sync_blocks()
        block = self.nodes[0].getblockcount() + 1
        node0_utxos = len(self.nodes[0].listunspent())
        node1_utxos = len(self.nodes[1].listunspent())

        # Test rollbacks
        for x in range(50):
            assert(self.account_to_account_loop(0, node0_source, node0_destination))
            self.nodes[0].generate(1)
            self.sync_blocks()
            assert(self.account_to_account_loop(1, node1_source, node1_destination))
            self.nodes[1].generate(1)
            self.sync_blocks()
            self.rollback(block)
            assert_equal(len(self.nodes[0].listunspent()), node0_utxos)
            assert_equal(len(self.nodes[1].listunspent()), node1_utxos)

if __name__ == '__main__':
    TestRestoreUTXOs().main()
