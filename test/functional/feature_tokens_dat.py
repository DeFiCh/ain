#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class TokensBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_all()

        # Stop node #3 for future revert
        self.stop_node(3)

        # CREATION:
        #========================
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].generate(1)

        # 1 Creating DAT token
        self.nodes[0].createtoken([], {
            "symbol": "PT",
            "name": "Platinum",
            "isDAT": True,
            "collateralAddress": collateral0
        })

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # At this point, token was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['1']["symbol"], "PT")

        # check sync:
        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['1']["symbol"], "PT")

        # 2 Trying to make it regular
        try:
            self.nodes[0].updatetoken([], {"token": "PT", "isDAT": False})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token PT is a 'stable coin'" in errorString)

        # Check 'gettoken' output
        t0 = self.nodes[0].gettoken(0)
        assert_equal(t0['0']['symbol'], "DFI")
        assert_equal(self.nodes[0].gettoken("DFI"), t0)
        t1 = self.nodes[0].gettoken(1)
        assert_equal(t1['1']['symbol'], "PT")
        assert_equal(self.nodes[0].gettoken("PT"), t1)

        # 3 Trying to make regular token
        self.nodes[0].generate(1)
        createTokenTx = self.nodes[0].createtoken([], {
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": False,
            "collateralAddress": collateral0
        })
        self.nodes[0].generate(1)
        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)

        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # 4 Trying to make it DAT not from Foundation
        try:
            self.nodes[2].updatetoken([], {"token": "GOLD", "isDAT": True})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect Authorization" in errorString)

        # 5 Making token isDAT from Foundation
        self.nodes[0].updatetoken([], {"token": "GOLD", "isDAT": True})

        self.nodes[0].generate(1)
        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], True)

        # 6 Checking after sync
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], True)

        # 7 Removing DAT
        self.nodes[0].updatetoken([], {"token": "GOLD", "isDAT": False})

        self.nodes[0].generate(1)

        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], False)

        self.nodes[0].generate(1)

        # 8 Creating DAT token
        self.nodes[0].createtoken([], {
            "symbol": "TEST",
            "name": "TEST token",
            "isDAT": True,
            "collateralAddress": collateral0
        })

        self.nodes[0].generate(1)

        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 4)
        assert_equal(tokens['2']["isDAT"], True)
        assert_equal(tokens['2']["symbol"], "TEST")

        # REVERTING:
        #========================
        print ("Reverting...")
        # Reverting creation!
        self.start_node(3)
        self.nodes[3].generate(30)

        connect_nodes_bi(self.nodes, 0, 3)
        self.sync_blocks()
        assert_equal(len(self.nodes[0].listtokens()), 1)

if __name__ == '__main__':
    TokensBasicTest ().main ()
