#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test multi-account listaccounthistory query."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal


class MultiAccountListAccountHistory(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50',
             '-grandcentralheight=51'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50',
             '-grandcentralheight=51'],
        ]

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_blocks()
        self.nodes[1].generate(101)
        self.sync_blocks()

        collateral_a = self.nodes[0].getnewaddress("", "legacy")
        collateral_b = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].createtoken({
            "symbol": "T1",
            "name": "T1",
            "collateralAddress": collateral_a
        })
        self.nodes[1].createtoken({
            "symbol": "T2",
            "name": "T2",
            "collateralAddress": collateral_b
        })
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()

        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if token["symbol"] == "T1":
                token1 = idx
            if token["symbol"] == "T2":
                token2 = idx

        self.nodes[0].minttokens(["300@" + token1])
        self.nodes[1].minttokens(["500@" + token2])
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].minttokens(["300@" + token1])
        self.nodes[1].minttokens(["500@" + token2])
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()

        combined = self.nodes[0].listaccounthistory([collateral_a, collateral_b])
        combined_count = self.nodes[0].accounthistorycount([collateral_a, collateral_b])
        a = self.nodes[0].listaccounthistory([collateral_a])
        b = self.nodes[0].listaccounthistory([collateral_b])

        assert_equal(len(combined), combined_count)
        assert_equal(len(combined), len(a) + len(b))


if __name__ == '__main__':
    MultiAccountListAccountHistory().main()
