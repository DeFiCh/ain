#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

class TokensBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main
        # node1: revert of destroy
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50'],
            ['-txnotokens=0', '-amkheight=50'],
            ['-txnotokens=0', '-amkheight=50']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_blocks()

        # Stop node #2 for future revert
        self.stop_node(2)

        # CREATION:
        #========================
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Fail to create: Insufficient funds (not matured coins)
        try:
            createTokenTx = self.nodes[0].createtoken({
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": collateral0
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        self.nodes[0].generate(1)

        # Fail to create: use # in symbol
        try:
            self.nodes[0].createtoken({
                "symbol": "GOLD#1",
                "name": "shiny gold",
                "collateralAddress": collateral0
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("token symbol should not contain '#'" in errorString)

        print ("Create token 'GOLD' (128)...")
        createTokenTx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateral0
        }, [])

        # Create and sign (only) collateral spending tx
        spendTx = self.nodes[0].createrawtransaction([{'txid':createTokenTx, 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        # Try to spend collateral of mempooled creattoken tx
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked-in-mempool," in errorString)

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1]])

        # At this point, token was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)

        # check sync:
        tokens = self.nodes[1].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)

        # Check 'gettoken' output
        t0 = self.nodes[0].gettoken(0)
        assert_equal(t0['0']['symbol'], "DFI")
        assert_equal(self.nodes[0].gettoken("DFI"), t0)
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['symbol'], "GOLD")
        assert_equal(self.nodes[0].gettoken("GOLD#128"), t128)
        assert_equal(self.nodes[0].gettoken(createTokenTx), t128)

        # Token not found, because not DAT
        try:
            self.nodes[0].gettoken("GOLD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token not found" in errorString)

        # Stop node #1 for future revert
        self.stop_node(1)

        # Try to spend locked collateral again
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked," in errorString)

        # Create new GOLD token, mintable and tradable by default
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateral0
        })
        self.nodes[0].generate(1)

        # Get token by SYMBOL#ID
        t129 = self.nodes[0].gettoken("GOLD#129")
        assert_equal(t129['129']['symbol'], "GOLD")
        assert_equal(t129['129']['mintable'], True)
        assert_equal(t129['129']['tradeable'], True)
        assert_equal(self.nodes[0].gettoken("GOLD#129"), t129)

        # Funding auth address for resigning
        self.nodes[0].sendtoaddress(collateral0, 1)
        self.nodes[0].generate(1)

        assert_equal(sorted(self.nodes[0].getrawmempool()), sorted([]))
        assert_equal(self.nodes[0].listtokens()['128']['destructionHeight'], -1)
        assert_equal(self.nodes[0].listtokens()['128']['destructionTx'], '0000000000000000000000000000000000000000000000000000000000000000')

        # Create new neither mintable nor tradable token
        self.nodes[0].createtoken({
            "symbol": "WK",
            "name": "weak",
            "mintable": False,
            "tradeable": False,
            "collateralAddress": collateral0
        })
        self.nodes[0].generate(1)

        # Get token by SYMBOL#ID
        t130 = self.nodes[0].gettoken("WK#130")
        assert_equal(t130['130']['symbol'], "WK")
        assert_equal(t130['130']['mintable'], False)
        assert_equal(t130['130']['tradeable'], False)

if __name__ == '__main__':
    TokensBasicTest ().main ()
