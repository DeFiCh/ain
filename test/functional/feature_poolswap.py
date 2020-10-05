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

from decimal import Decimal

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        #self.nodes[0].generate(100)
        #self.sync_all()
        print("Generating initial chain...")
        self.setup_tokens()
        # Stop node #3 for future revert
        self.stop_node(3)

        # CREATION:
        #========================
        # 1 Getting new addresses and checking coins
        symbolGOLD = "GOLD#" + self.get_id_token("GOLD")
        symbolSILVER = "SILVER#" + self.get_id_token("SILVER")
        idGold = list(self.nodes[0].gettoken(symbolGOLD).keys())[0]
        idSilver = list(self.nodes[0].gettoken(symbolSILVER).keys())[0]
        accountGN0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSN1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGN0, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Initial GOLD AccN0:", initialGold, ", id", idGold)
        print("Initial SILVER AccN1:", initialSilver, ", id", idSilver)

        owner = self.nodes[0].getnewaddress("", "legacy")

        # 2 Transferring SILVER from N1 Account to N0 Account
        self.nodes[1].accounttoaccount(accountSN1, {accountGN0: "1000@" + symbolSILVER})
        self.nodes[1].generate(1)
        # Transferring GOLD from N0 Account to N1 Account
        self.nodes[0].accounttoaccount(accountGN0, {accountSN1: "200@" + symbolGOLD})
        self.nodes[0].generate(1)

        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)
        silverCheckN1 = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)

        # 3 Creating poolpair
        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": symbolSILVER,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].generate(1)

        # only 4 tokens = DFI, GOLD, SILVER, GS
        assert_equal(len(self.nodes[0].listtokens()), 4)

        # check tokens id
        pool = self.nodes[0].getpoolpair("GS")
        idGS = list(self.nodes[0].gettoken("GS").keys())[0]
        assert(pool[idGS]['idTokenA'] == idGold)
        assert(pool[idGS]['idTokenB'] == idSilver)

        # Fail swap: lack of liquidity
        try:
            self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolSILVER,
                "amountFrom": 10,
                "to": accountSN1,
                "tokenTo": symbolGOLD,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Pool is empty!" in errorString)

        #list_pool = self.nodes[0].listpoolpairs()
        #print (list_pool)

        # 4 Adding liquidity
        #list_poolshares = self.nodes[0].listpoolshares()
        #print (list_poolshares)

        self.nodes[0].addpoolliquidity({
            accountGN0: ["100@" + symbolGOLD, "500@" + symbolSILVER]
        }, accountGN0, [])
        self.nodes[0].generate(1)

        self.sync_blocks([self.nodes[0], self.nodes[1]])

        #list_poolshares = self.nodes[0].listpoolshares()
        #print (list_poolshares)

        self.nodes[1].addpoolliquidity({
            accountSN1: ["100@" + symbolGOLD, "500@" + symbolSILVER]
        }, accountSN1, [])
        self.nodes[1].generate(1)

        self.sync_blocks([self.nodes[0], self.nodes[1]])

        #list_poolshares = self.nodes[0].listpoolshares()
        #print (list_poolshares)

        goldCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idGold]
        print("Checking Gold on AccN0:", goldCheckN0, ", id", idGold)
        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)

        # 5 Checking that liquidity is correct
        assert(goldCheckN0 == 700)
        assert(silverCheckN0 == 500)

        list_pool = self.nodes[0].listpoolpairs()
        #print (list_pool)

        assert(list_pool['1']['reserveA'] == 200)  # GOLD
        assert(list_pool['1']['reserveB'] == 1000) # SILVER

        # 6 Trying to poolswap

        self.nodes[0].updatepoolpair({"pool": "GS", "status": False})
        self.nodes[0].generate(1)
        try:
            self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolSILVER,
                "amountFrom": 10,
                "to": accountSN1,
                "tokenTo": symbolGOLD,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("turned off" in errorString)
        self.nodes[0].updatepoolpair({"pool": "GS", "status": True})
        self.nodes[0].generate(1)


        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 10,
            "to": accountSN1,
            "tokenTo": symbolGOLD,
        }, [])
        self.nodes[0].generate(1)

        # 7 Sync
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # 8 Checking that poolswap is correct
        goldCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idGold]
        print("Checking Gold on AccN0:", goldCheckN0, ", id", idGold)
        silverCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)

        goldCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        print("Checking Gold on AccN1:", goldCheckN1, ", id", idGold)
        silverCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)

        list_pool = self.nodes[2].listpoolpairs()
        #print (list_pool)

        self.nodes[0].listpoolshares()
        #print (list_poolshares)

        assert(goldCheckN0 == 700)
        assert(str(silverCheckN0) == "490.49990000") # TODO: calculate "true" values with trading fee!
        assert(list_pool['1']['reserveA'] + goldCheckN1 == 300)
        assert(str(silverCheckN1) == "500.50000000")
        assert(list_pool['1']['reserveB'] == 1009) #1010 - 1 (commission)

        # 9 Fail swap: price higher than indicated
        price = list_pool['1']['reserveA/reserveB']
        try:
            self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolSILVER,
                "amountFrom": 10,
                "to": accountSN1,
                "tokenTo": symbolGOLD,
                "maxPrice": price - Decimal('0.1'),
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Price is higher than indicated." in errorString)

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
