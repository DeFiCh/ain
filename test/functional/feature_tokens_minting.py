#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify simple minting, sendtoaddress, listunspent, balances
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

class TokensMintingTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_blocks()

        self.nodes[0].generate(2) # for 2 matured utxos

        # CREATION:
        #========================
        collateralGold = self.nodes[0].getnewaddress("", "legacy")
        collateralSilver = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateralGold
        })
        self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "collateralAddress": collateralSilver
        })

        self.nodes[0].generate(1)
        # At this point, tokens was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)

        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                idGold = idx
            if (token["symbol"] == "SILVER"):
                idSilver = idx

        symbolGold = "GOLD#" + idGold
        symbolSilver = "SILVER#" + idSilver

        self.sync_blocks()

        # MINT:
        #========================
        # Funding auth addresses

        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1 })
        self.nodes[0].generate(1)
        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1 })
        self.nodes[0].generate(1)

        # print(self.nodes[0].listunspent())

        self.nodes[0].minttokens("300@" + symbolGold)
        self.nodes[0].minttokens("3000@" + symbolSilver)
        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(collateralGold, {}, True)[idGold], 300)
        assert_equal(self.nodes[0].getaccount(collateralSilver, {}, True)[idSilver], 3000)

if __name__ == '__main__':
    TokensMintingTest ().main ()
