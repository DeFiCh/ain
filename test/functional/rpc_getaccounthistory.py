#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getaccounthistory RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal
)

class TokensRPCGetAccountHistory(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50'],
        ]

    def run_test(self):
        self.nodes[0].generate(101)
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Stop node #2 for future revert
        self.stop_node(2)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx

        # Mint some tokens
        self.nodes[0].minttokens(["300@" + token_a])
        self.nodes[0].generate(1)

        # Get node 0 results
        results = self.nodes[0].listaccounthistory(collateral_a)

        # Expect two sends, two receives and one mint tokens
        assert_equal(len(results), 5)

        # Account history from listaccounthistory and gettaccounthistory must be matched
        for txs in results:
            self.log.info("owner:%s blockHeight:%s txn:%s", txs['owner'], txs['blockHeight'], txs['txn'])
            history = self.nodes[0].getaccounthistory(txs['owner'], txs['blockHeight'], txs['txn'])
            assert_equal('owner' in history, True)
            assert_equal(history['owner'], txs['owner'])
            assert_equal(history['blockHeight'], txs['blockHeight'])
            assert_equal(history['txn'], txs['txn'])
            assert_equal(history['txid'], txs['txid'])

        # Get node 1 results
        results = self.nodes[1].listaccounthistory(collateral_a)

        # Expect one mint token TX
        assert_equal(len(results), 1)

        # Account history from listaccounthistory and gettaccounthistory must be matched
        for txs in results:
            self.log.info("owner:%s blockHeight:%s txn:%s", txs['owner'], txs['blockHeight'], txs['txn'])
            history = self.nodes[1].getaccounthistory(txs['owner'], txs['blockHeight'], txs['txn'])
            assert_equal('owner' in history, True)
            assert_equal(history['owner'], txs['owner'])
            assert_equal(history['blockHeight'], txs['blockHeight'])
            assert_equal(history['txn'], txs['txn'])
            assert_equal(history['txid'], txs['txid'])

if __name__ == '__main__':
    TokensRPCGetAccountHistory().main ()
