#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
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
        collateralCupper = self.nodes[0].getnewaddress("", "legacy")

        txid1 = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateralGold
        })
        self.nodes[0].generate(1)
        txid1_blockHeight = self.nodes[0].getblockcount()

        txid2 = self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "collateralAddress": collateralSilver
        })
        self.nodes[0].generate(1)
        txid2_blockHeight = self.nodes[0].getblockcount()

        txid3 = self.nodes[0].createtoken({
            "symbol": "CUPPER",
            "name": "just cupper",
            "collateralAddress": collateralCupper
        })
        self.nodes[0].generate(1)
        txid3_blockHeight = self.nodes[0].getblockcount()

        # At this point, tokens was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 4)

        assert_equal(self.nodes[0].isappliedcustomtx(txid1, txid1_blockHeight), True)
        assert_equal(self.nodes[0].isappliedcustomtx(txid2, txid2_blockHeight), True)
        assert_equal(self.nodes[0].isappliedcustomtx(txid3, txid3_blockHeight), True)
        # Not apllied tx
        assert_equal(self.nodes[0].isappliedcustomtx("b2bb09ffe9f9b292f13d23bafa1225ef26d0b9906da7af194c5738b63839b235", txid2_blockHeight), False)

        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                idGold = idx
            if (token["symbol"] == "SILVER"):
                idSilver = idx
            if (token["symbol"] == "CUPPER"):
                idCupper = idx

        symbolGold = "GOLD#" + idGold
        symbolSilver = "SILVER#" + idSilver
        symbolCupper = "CUPPER#" + idCupper

        self.sync_blocks()

        # MINT:
        #========================
        # Funding auth addresses

        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1, collateralCupper : 1 })
        self.nodes[0].generate(1)
        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1, collateralCupper : 1 })
        self.nodes[0].generate(1)

        # print(self.nodes[0].listunspent())

        self.nodes[0].minttokens(["300@" + symbolGold, "3000@" + symbolSilver, "500@" + symbolCupper])
        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(collateralGold, {}, True)[idGold], 300)
        assert_equal(self.nodes[0].getaccount(collateralSilver, {}, True)[idSilver], 3000)
        assert_equal(self.nodes[0].getaccount(collateralCupper, {}, True)[idCupper], 500)

        alienMintAddr = self.nodes[1].getnewaddress("", "legacy")

        # Checking the number of minted coins
        assert_equal(self.nodes[0].gettoken(symbolGold)[idGold]['minted'], 300)
        assert_equal(self.nodes[0].gettoken(symbolSilver)[idSilver]['minted'], 3000)
        assert_equal(self.nodes[0].gettoken(symbolCupper)[idCupper]['minted'], 500)

        assert_equal(self.nodes[0].gettoken(symbolGold)[idGold]['collateralAddress'], collateralGold)
        assert_equal(self.nodes[0].gettoken(symbolSilver)[idSilver]['collateralAddress'], collateralSilver)

        try:
            self.nodes[0].accounttoutxos(collateralGold, { self.nodes[0].getnewaddress("", "legacy"): "100@" + symbolGold, alienMintAddr: "200@" + symbolGold}, [])
            self.nodes[0].accounttoutxos(collateralSilver, { self.nodes[0].getnewaddress("", "legacy"): "1000@" + symbolSilver, alienMintAddr: "2000@" + symbolSilver}, [])
            self.nodes[0].generate(1)
            self.sync_blocks()
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("only available for DFI transactions" in errorString)

if __name__ == '__main__':
    TokensMintingTest ().main ()
