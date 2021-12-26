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
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    disconnect_nodes,
    assert_raises_rpc_error,
)

from decimal import Decimal

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-acindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-acindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163',],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163',]]


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
        assert_equal(pool[idGS]['idTokenA'], idGold)
        assert_equal(pool[idGS]['idTokenB'], idSilver)

        # Fail swap: lack of liquidity
        assert_raises_rpc_error(-32600, "Lack of liquidity", self.nodes[0].poolswap, {
                "from": accountGN0,
                "tokenFrom": symbolSILVER,
                "amountFrom": 10,
                "to": accountSN1,
                "tokenTo": symbolGOLD,
            }
        )

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
        assert_equal(goldCheckN0, 700)
        assert_equal(silverCheckN0, 500)

        list_pool = self.nodes[0].listpoolpairs()
        #print (list_pool)

        assert_equal(list_pool['1']['reserveA'], 200)  # GOLD
        assert_equal(list_pool['1']['reserveB'], 1000) # SILVER

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

        print("Before swap")
        print("Checking Gold on AccN0:", goldCheckN0, ", id", idGold)
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)
        goldCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        print("Checking Gold on AccN1:", goldCheckN1, ", id", idGold)
        silverCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)

        testPoolSwapRes =  self.nodes[0].testpoolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 10,
            "to": accountSN1,
            "tokenTo": symbolGOLD,
        })

        # this acc will be
        goldCheckPS = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        print("goldCheckPS:", goldCheckPS)
        print("testPoolSwapRes:", testPoolSwapRes)

        testPoolSwapSplit = str(testPoolSwapRes).split("@", 2)

        psTestAmount = testPoolSwapSplit[0]
        psTestTokenId = testPoolSwapSplit[1]
        assert_equal(psTestTokenId, idGold)

        testPoolSwapVerbose =  self.nodes[0].testpoolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 10,
            "to": accountSN1,
            "tokenTo": symbolGOLD,
        }, "direct", True)

        assert_equal(testPoolSwapVerbose["path"], "direct")
        assert_equal(testPoolSwapVerbose["pools"][0], idGS)
        assert_equal(testPoolSwapVerbose["amount"], testPoolSwapRes)

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
        print("After swap")
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

        assert_equal(goldCheckN0, 700)
        assert_equal(str(silverCheckN0), "490.49999997") # TODO: calculate "true" values with trading fee!
        assert_equal(list_pool['1']['reserveA'] + goldCheckN1 , 300)
        assert_equal(Decimal(goldCheckPS) + Decimal(psTestAmount), Decimal(goldCheckN1))
        assert_equal(str(silverCheckN1), "500.50000000")
        assert_equal(list_pool['1']['reserveB'], 1009) #1010 - 1 (commission)

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

        # Visual test for listaccounthistory
        print("mine@0:", self.nodes[0].listaccounthistory())
        print("mine@1:", self.nodes[1].listaccounthistory())
        print("all@0", self.nodes[0].listaccounthistory("all"))
        print("accountGN0@0", self.nodes[0].listaccounthistory(accountGN0))
        print("mine@0, depth=3:", self.nodes[0].listaccounthistory("mine", {"depth":3}))
        print("mine@0, height=158 depth=2:", self.nodes[0].listaccounthistory("mine", {"maxBlockHeight":158, "depth":2}))
        print("all@0, height=158 depth=2:", self.nodes[0].listaccounthistory("all", {"maxBlockHeight":158, "depth":2}))

        # activate max price protection
        maxPrice = self.nodes[0].listpoolpairs()['1']['reserveB/reserveA']
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 200,
            "to": accountGN0,
            "tokenTo": symbolGOLD,
            "maxPrice": maxPrice,
        })
        assert_raises_rpc_error(-26, 'Price is higher than indicated',
                                self.nodes[0].poolswap, {
                                    "from": accountGN0,
                                    "tokenFrom": symbolSILVER,
                                    "amountFrom": 200,
                                    "to": accountGN0,
                                    "tokenTo": symbolGOLD,
                                    "maxPrice": maxPrice,
                                }
        )
        self.nodes[0].generate(1)

        maxPrice = self.nodes[0].listpoolpairs()['1']['reserveB/reserveA']
        # exchange tokens each other should work
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 200,
            "to": accountGN0,
            "tokenTo": symbolGOLD,
            "maxPrice": maxPrice,
        })
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolGOLD,
            "amountFrom": 200,
            "to": accountGN0,
            "tokenTo": symbolSILVER,
            "maxPrice": maxPrice,
        })
        self.nodes[0].generate(1)

        # Test fort canning max price change
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[0], 2)
        print(self.nodes[0].getconnectioncount())
        destination = self.nodes[0].getnewaddress("", "legacy")
        swap_from = 200
        coin = 100000000

        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolGOLD,
            "amountFrom": swap_from,
            "to": destination,
            "tokenTo": symbolSILVER
        })
        self.nodes[0].generate(1)

        silver_received = self.nodes[0].getaccount(destination, {}, True)[idSilver]
        silver_per_gold = round((swap_from * coin) / (silver_received * coin), 8)

        # Reset swap and try again with max price set to expected amount
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolGOLD,
            "amountFrom": swap_from,
            "to": destination,
            "tokenTo": symbolSILVER,
            "maxPrice": silver_per_gold,
        })
        self.nodes[0].generate(1)

        # Reset swap and try again with max price set to one Satoshi below
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        try:
            self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolGOLD,
                "amountFrom": swap_from,
                "to": destination,
                "tokenTo": symbolSILVER,
                "maxPrice": silver_per_gold - Decimal('0.00000001'),
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Price is higher than indicated" in errorString)

        # REVERTING:
        #========================
        print ("Reverting...")
        # Reverting creation!
        self.start_node(3)
        self.nodes[3].generate(30)

        connect_nodes_bi(self.nodes, 0, 3)
        connect_nodes_bi(self.nodes, 1, 3)
        connect_nodes_bi(self.nodes, 2, 3)
        self.sync_blocks()
        assert_equal(len(self.nodes[0].listpoolpairs()), 0)

if __name__ == '__main__':
    PoolPairTest ().main ()
