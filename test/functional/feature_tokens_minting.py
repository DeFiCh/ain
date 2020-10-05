#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify simple minting, sendtoaddress, listunspent, balances
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

class TokensMintingTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50'], ['-txnotokens=0', '-amkheight=50']]

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
        }, [])
        self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "collateralAddress": collateralSilver
        }, [])

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

        alienMintAddr = self.nodes[1].getnewaddress("", "legacy")

        self.nodes[0].minttokens("300@" + symbolGold, [])
        self.nodes[0].minttokens("3000@" + symbolSilver, [])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Checking the number of minted coins
        assert_equal(self.nodes[0].gettoken(symbolGold)[idGold]['minted'], 300)
        assert_equal(self.nodes[0].gettoken(symbolSilver)[idSilver]['minted'], 3000)

        try:
            self.nodes[0].accounttoutxos(collateralGold, { self.nodes[0].getnewaddress("", "legacy"): "100@" + symbolGold, alienMintAddr: "200@" + symbolGold}, [])
            self.nodes[0].accounttoutxos(collateralSilver, { self.nodes[0].getnewaddress("", "legacy"): "1000@" + symbolSilver, alienMintAddr: "2000@" + symbolSilver}, [])
            self.nodes[0].generate(1)
            self.sync_blocks()
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("AccountToUtxos only available for DFI transactions" in errorString)

if __name__ == '__main__':
    TokensMintingTest ().main ()
