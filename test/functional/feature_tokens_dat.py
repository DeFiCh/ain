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
from test_framework.util import assert_equal, \
    connect_nodes_bi

class TokensBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50'],
            ['-txnotokens=0', '-amkheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_blocks()

        # Stop node #3 for future revert
        self.stop_node(3)

        # CREATION:
        #========================
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].generate(1)

        # 1 Creating DAT token
        self.nodes[0].createtoken({
            "symbol": "PT",
            "name": "Platinum",
            "isDAT": True,
            "collateralAddress": collateral0
        }, [])

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
        # try:
        #     self.nodes[0].updatetoken({"token": "PT", "isDAT": False}, [])
        # except JSONRPCException as e:
        #     errorString = e.error['message']
        # assert("Token PT is a 'stable coin'" in errorString)

        # Check 'gettoken' output
        t0 = self.nodes[0].gettoken(0)
        assert_equal(t0['0']['symbol'], "DFI")
        assert_equal(self.nodes[0].gettoken("DFI"), t0)
        t1 = self.nodes[0].gettoken(1)
        assert_equal(t1['1']['symbol'], "PT")
        assert_equal(self.nodes[0].gettoken("PT"), t1)

        # 3 Trying to make regular token
        self.nodes[0].generate(1)
        createTokenTx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": False,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        }, [])
        self.nodes[0].generate(1)
        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)

        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        self.stop_node(1) # for future test
        connect_nodes_bi(self.nodes, 0, 2)

        # 4 Trying to make it DAT not from Foundation
        try:
            self.nodes[2].updatetoken("GOLD#128", {"isDAT": True}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Need foundation member authorization" in errorString)

        # 4.1 Trying to set smth else
        try:
            self.nodes[2].updatetoken("GOLD#128", {"symbol": "G"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("before Bayfront fork" in errorString)

        # 5 Making token isDAT from Foundation
        self.nodes[0].updatetoken("GOLD#128", {"isDAT": True}, [])
        self.nodes[0].generate(1)
        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], True)

        # Get token
        assert_equal(self.nodes[0].gettoken("GOLD")['128']["isDAT"], True)

        # 6 Checking that it will not sync
        self.sync_blocks([self.nodes[0], self.nodes[2]])
        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], False) # not synced cause new tx type (from node 0)

        # 6.1 Restart with proper height and retry
        self.stop_node(2)
        self.start_node(2, ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-reindex-chainstate']) # warning! simple '-reindex' not works!
        # it looks like node can serve rpc in async while reindexing... wait:
        self.sync_blocks([self.nodes[0], self.nodes[2]])
        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], True) # synced now

        connect_nodes_bi(self.nodes, 0, 2) # for final sync at "REVERT"

        # 7 Removing DAT
        self.nodes[0].updatetoken("GOLD", {"isDAT": False}, [])
        self.nodes[0].generate(1)
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["isDAT"], False)

        # 6.2 Once again as 6.1, but opposite: make DAT on "old" node with "old" tx
        self.start_node(1, ['-txnotokens=0', '-amkheight=50'])
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks([self.nodes[0], self.nodes[1]])
        assert_equal(self.nodes[0].gettoken('128')['128']["isDAT"], False)
        assert_equal(self.nodes[1].gettoken('128')['128']["isDAT"], False)

        self.nodes[1].importprivkey(self.nodes[0].get_genesis_keys().ownerPrivKey) # there is no need to import the key, cause node#1 is founder itself... but it has no utxos for auth
        self.nodes[1].updatetoken('128', {"isDAT": True})
        self.nodes[1].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1]])
        assert_equal(self.nodes[0].gettoken('128')['128']["isDAT"], False) # tx not applyed cause "old"
        assert_equal(self.nodes[1].gettoken('128')['128']["isDAT"], True)


        # 8. changing token's symbol:
        self.nodes[0].updatetoken("GOLD#128", {"symbol":"gold"})
        self.nodes[0].generate(1)
        token = self.nodes[0].gettoken('128')
        assert_equal(token['128']["symbol"], "gold")
        assert_equal(token['128']["symbolKey"], "gold#128")
        assert_equal(token['128']["isDAT"], False)
        assert_equal(self.nodes[0].gettoken('gold#128'), token)

        # changing token's symbol AND DAT at once:
        self.nodes[0].updatetoken("128", {"symbol":"goldy", "isDAT":True})
        self.nodes[0].generate(1)
        token = self.nodes[0].gettoken('128')
        assert_equal(token['128']["symbol"], "goldy")
        assert_equal(token['128']["symbolKey"], "goldy")
        assert_equal(token['128']["isDAT"], True)
        assert_equal(self.nodes[0].gettoken('goldy'), token) # can do it w/o '#'' cause it should be DAT

        # changing other properties:
        self.nodes[0].updatetoken("128", {"name":"new name", "tradeable": False, "mintable": False, "finalize": True})
        self.nodes[0].generate(1)
        token = self.nodes[0].gettoken('128')
        assert_equal(token['128']["name"], "new name")
        assert_equal(token['128']["mintable"], False)
        assert_equal(token['128']["tradeable"], False)
        assert_equal(token['128']["finalized"], True)

        # try to change finalized token:
        try:
            self.nodes[0].updatetoken("128", {"tradable": True})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("can't alter 'Finalized' tokens" in errorString)

        # Fail get token
        try:
            self.nodes[0].gettoken("GOLD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token not found" in errorString)

        self.nodes[0].generate(1)

        # 8 Creating DAT token
        self.nodes[0].createtoken({
            "symbol": "TEST",
            "name": "TEST token",
            "isDAT": True,
            "collateralAddress": collateral0
        }, [])

        self.nodes[0].generate(1)

        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 4)
        assert_equal(tokens['2']["isDAT"], True)
        assert_equal(tokens['2']["symbol"], "TEST")

        # 9 Fail to create: there can be only one DAT token
        try:
            self.nodes[0].createtoken({
                "symbol": "TEST",
                "name": "TEST token",
                "isDAT": True,
                "collateralAddress": collateral0
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("already exists" in errorString)

        # 10 Fail to update
        self.nodes[0].createtoken({
            "symbol": "TEST",
            "name": "TEST token copy",
            "isDAT": False,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress # !from founders!!
        }, [])

        self.nodes[0].generate(1)

        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 5)
        assert_equal(tokens['129']["symbol"], "TEST")
        assert_equal(tokens['129']["name"], "TEST token copy")
        assert_equal(tokens['129']["isDAT"], False)

        try:
            self.nodes[0].updatetoken("TEST#129", {"isDAT": True})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("already exists" in errorString)


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
