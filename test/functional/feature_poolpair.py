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

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
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

        # 3 Trying to make regular token
        self.nodes[0].generate(1)
        createTokenTx = self.nodes[0].createtoken({
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

        # 7 Creating PoolPair from Foundation -> OK
        self.nodes[0].createpoolpair({
            "tokenA": "PT",
            "tokenB": "GOLD#128",
            "comission": 0.001,
            "status": True,
            "ownerAddress": collateral0,
            "pairSymbol": "PTGOLD"
        }, [])

        self.nodes[0].generate(1)
        # Trying to create the same again and fail
        try:
            self.nodes[0].createpoolpair({
            "tokenA": "PT",
            "tokenB": "GOLD#128",
            "comission": 0.001,
            "status": True,
            "ownerAddress": collateral0,
            "pairSymbol": "PTGD"
        }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Error, there is already a poolpair with same tokens, but different poolId" in errorString)

        # Creating another one
        trPP = self.nodes[0].createpoolpair({
            "tokenA": "DFI",
            "tokenB": "GOLD#128",
            "comission": 0.001,
            "status": True,
            "ownerAddress": collateral0,
            "pairSymbol": "DFGLD"
        }, [])

        # 7+ Checking if it's an automatically created token (collateral unlocked, user's token has collateral locked)
        tx = self.nodes[0].getrawtransaction(trPP)
        decodeTx = self.nodes[0].decoderawtransaction(tx)
        assert_equal(len(decodeTx['vout']), 2)
        #print(decodeTx['vout'][1]['scriptPubKey']['hex'])

        spendTx = self.nodes[0].createrawtransaction([{'txid':decodeTx['txid'], 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        self.nodes[0].generate(1)
        # 8 Creating PoolPair not from Foundation -> Error
        try:
            self.nodes[2].createpoolpair({
            "tokenA": "DFI",
            "tokenB": "GOLD#128",
            "comission": 0.001,
            "status": True,
            "ownerAddress": collateral0,
            "pairSymbol": "DFIGOLD"
        }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Need foundation member authorization" in errorString)

        # 9 Checking pool existence
        p0 = self.nodes[0].getpoolpair("PTGOLD")
        assert_equal(p0['2']['symbol'], "PTGOLD")

        #10 Checking nonexistent pool
        try:
            self.nodes[0].getpoolpair("DFIGOLD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Pool not found" in errorString)

        try:
            self.nodes[2].getpoolpair("PTGOLD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Pool not found" in errorString)

        #11 Checking listpoolpairs
        poolpairsn0 = self.nodes[0].listpoolpairs()
        assert_equal(len(poolpairsn0), 2)

        self.sync_blocks([self.nodes[0], self.nodes[2]])

        poolpairsn2 = self.nodes[2].listpoolpairs()
        #print (poolpairsn2)
        assert_equal(len(poolpairsn2), 2)

        # 12 Checking pool existence after sync
        p1 = self.nodes[2].getpoolpair("PTGOLD")
        #print(p1)
        assert_equal(p1['2']['symbol'], "PTGOLD")
        assert(p1['2']['idTokenA'] == '1')
        assert(p1['2']['idTokenB'] == '128')

        # 13 Change pool status
        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['status'], True)
        self.nodes[0].updatepoolpair({
            "pool": "PTGOLD",
            "status": False,
            "commission": 0.01
        }, [])
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['status'], False)
        assert_equal(str(self.nodes[0].getpoolpair("PTGOLD")['2']['commission']), "0.01000000")
        self.sync_blocks([self.nodes[0], self.nodes[2]])
        assert_equal(self.nodes[2].getpoolpair("PTGOLD")['2']['status'], False)
        assert_equal(str(self.nodes[2].getpoolpair("PTGOLD")['2']['commission']), "0.01000000")

        self.nodes[0].updatepoolpair({"pool": "PTGOLD", "commission": 0.1}, [])
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['status'], False)
        assert_equal(str(self.nodes[0].getpoolpair("PTGOLD")['2']['commission']), "0.10000000")

        try:
            self.nodes[0].updatepoolpair({"pool": "PTGOLD", "commission": 2})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("commission > 100%" in errorString)

        self.nodes[0].updatepoolpair({"pool": "PTGOLD", "status": True}, [])
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['status'], True)
        assert_equal(str(self.nodes[0].getpoolpair("PTGOLD")['2']['commission']), "0.10000000")

        ownerAddress = self.nodes[0].getpoolpair("PTGOLD")['2']['ownerAddress']
        collateral1 = self.nodes[1].getnewaddress("", "legacy")
        self.nodes[0].updatepoolpair({"pool": "PTGOLD", "ownerAddress": collateral1}, [])
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['status'], True)
        assert_equal(str(self.nodes[0].getpoolpair("PTGOLD")['2']['commission']), "0.10000000")
        assert(self.nodes[0].getpoolpair("PTGOLD")['2']['ownerAddress'] != ownerAddress)

        self.nodes[0].updatepoolpair({"pool": "PTGOLD", "ownerAddress": collateral0}, [])
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getpoolpair("PTGOLD")['2']['ownerAddress'], ownerAddress)

        # REVERTING:
        #========================
        print ("Reverting...")
        # Reverting creation!
        self.start_node(3)
        self.nodes[3].generate(30)

        connect_nodes_bi(self.nodes, 0, 3)
        self.sync_blocks()
        assert_equal(len(self.nodes[0].listpoolpairs()), 0)

if __name__ == '__main__':
    PoolPairTest ().main ()
