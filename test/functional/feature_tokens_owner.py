#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token ownership transfer RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

class TokensOwnerTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-cqheight=120']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(101)

        # CREATION:
        #========================
        original_owner = self.nodes[0].getnewaddress("", "legacy")
        new_owner = self.nodes[0].getnewaddress("", "legacy")

        print ("Create token 'GOLD' (128)...")
        createTokenTx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": original_owner
        }, [])

        self.nodes[0].generate(1)

        # Make sure owner as expected
        t128 = self.nodes[0].gettoken(createTokenTx) # Gets ID from <CreationTx> and then by <ID> record
        assert_equal(t128['128']['collateralAddress'], original_owner)
        assert_equal(t128['128']["name"], "shiny gold")
        assert_equal(t128['128']["creationTx"], createTokenTx)

        # Fund original_owner address
        self.nodes[0].sendtoaddress(original_owner, 1)
        self.nodes[0].sendtoaddress(original_owner, 1)
        self.nodes[0].generate(1)

        # Try and change owner pre-CQ
        self.nodes[0].updatetoken("128", {"collateralAddress": new_owner}, [])
        self.nodes[0].generate(1)

        # Make sure owner has not changed
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['collateralAddress'], original_owner)

        # Move past CQ hard fork
        self.nodes[0].generate(120 - self.nodes[0].getblockcount())

        # Update owner post-CQ
        updateTokenTx = self.nodes[0].updatetoken("128", {"collateralAddress": new_owner}, [])
        self.nodes[0].generate(1)

        # Make sure owner records have changed
        t128 = self.nodes[0].gettoken(updateTokenTx) # Gets ID from <CreationTx> and then by <ID> record
        assert_equal(t128['128']['collateralAddress'], new_owner)
        assert_equal(t128['128']["creationTx"], updateTokenTx)

        # Fund original_owner address
        self.nodes[0].sendtoaddress(new_owner, 1)
        self.nodes[0].sendtoaddress(new_owner, 1)
        self.nodes[0].generate(1)

        # Mint tokens using new owner
        self.nodes[0].minttokens(["100000@128"])
        self.nodes[0].generate(1)

        # Check tokens minted using new_owner
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['minted'], 100000.00000000)

        # Change name using new owner
        self.nodes[0].updatetoken("128", {"name": "glossy gold"}, [])
        self.nodes[0].generate(1)

        # Make sure name has udated
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['name'], "glossy gold")

        assert_equal(len(self.nodes[0].listtokens()), 2) # only two tokens == DFI, GOLD

if __name__ == '__main__':
    TokensOwnerTest ().main ()
