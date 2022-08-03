#!/usr/bin/env python3
# Copyright (c) 2014-2020 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify before fork, not allow create token, after fork can create token
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error

class TokensForkTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=120']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(102)

        # Try to create token before AMK fork height but will fail:
        #========================
        collateralGold = self.nodes[0].getnewaddress("", "legacy")
        collateralSilver = self.nodes[0].getnewaddress("", "legacy")
        try:
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
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("before AMK height" in errorString)

        self.nodes[0].generate(1)
        # Before fork, create should fail, so now only have default token
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 1)

        # Try to mint token before AMK fork height but will fail:
        #========================
        # Minting can't be checked here on rpc level cause token doesn't exist and it's impossible to create it
        # we'll check it at the end with a trick

        # try:
        #     self.nodes[0].minttokens("300@GOLD", [])
        # except JSONRPCException as e:
        #     errorString = e.error['message']
        # assert("Token tx before AMK" in errorString)

        self.nodes[0].generate(17)

        # Now at AMK height 120
        # Now create again, it should pass
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateralGold
        })
        self.nodes[0].generate(1)

        txid = self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "collateralAddress": collateralSilver
        })
        self.nodes[0].generate(1)

        # Get token ID
        id_silver  = list(self.nodes[0].gettoken('SILVER#129').keys())[0]

        # Check rollback of token
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Make sure token not found
        assert_raises_rpc_error(-5, "Token not found", self.nodes[0].gettoken, txid)
        assert_raises_rpc_error(-5, "Token not found", self.nodes[0].gettoken, id_silver)
        assert_raises_rpc_error(-5, "Token not found", self.nodes[0].gettoken, 'SILVER#129')

        # Create token again
        self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "collateralAddress": collateralSilver
        })
        self.nodes[0].generate(1)

        # Check the same ID was provided, not an increment of the last one
        assert_equal(id_silver, list(self.nodes[0].gettoken('SILVER#129').keys())[0])

        # After fork, create should pass, so now only have 3 kind of tokens
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

        # MINT:
        #========================
        # Funding auth addresses

        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1 })
        self.nodes[0].generate(1)
        self.nodes[0].sendmany("", { collateralGold : 1, collateralSilver : 1 })
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("300@" + symbolGold, [])
        self.nodes[0].minttokens("3000@" + symbolSilver, [])
        self.nodes[0].generate(1)

        # synthetically check for minting. restart w/o reindex and amk (so, token exists, but minting should fail)
        self.stop_node(0)
        self.start_node(0, ['-txnotokens=0'])
        try:
            self.nodes[0].minttokens("300@128", [])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("before AMK height" in errorString)

if __name__ == '__main__':
    TokensForkTest ().main ()
