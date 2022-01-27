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
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-acindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-acindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

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

        owner = self.nodes[0].getnewaddress("", "legacy")

        # 2 Transferring SILVER from N1 Account to N0 Account
        self.nodes[1].accounttoaccount(accountSN1, {accountGN0: "1000@" + symbolSILVER})
        self.nodes[1].generate(1)
        # Transferring GOLD from N0 Account to N1 Account
        self.nodes[0].accounttoaccount(accountGN0, {accountSN1: "200@" + symbolGOLD})
        self.nodes[0].generate(1)

        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        silverCheckN1 = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]

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
        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]

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

        goldCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        silverCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idSilver]

        testPoolSwapRes =  self.nodes[0].testpoolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 10,
            "to": accountSN1,
            "tokenTo": symbolGOLD,
        })

        # this acc will be
        goldCheckPS = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]

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
        goldCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idGold]
        silverCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idSilver]

        goldCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        silverCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idSilver]

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
            "tokenFrom": symbolGOLD,
            "amountFrom": 200,
            "to": accountGN0,
            "tokenTo": symbolSILVER,
            "maxPrice": maxPrice,
        })
        self.nodes[0].generate(1)
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 200,
            "to": accountGN0,
            "tokenTo": symbolGOLD,
            "maxPrice": maxPrice,
        })
        self.nodes[0].generate(1)

        # Test fort canning max price change
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[0], 2)
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

        # Test round up setup
        self.nodes[0].createtoken({
                "symbol": "BTC",
                "name": "Bitcoin",
                "collateralAddress": accountGN0
            })
        self.nodes[0].createtoken({
                "symbol": "LTC",
                "name": "Litecoin",
                "collateralAddress": accountGN0
            })
        self.nodes[0].generate(1)

        symbolBTC = "BTC#" + self.get_id_token("BTC")
        symbolLTC = "LTC#" + self.get_id_token("LTC")
        idBitcoin = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        self.nodes[0].minttokens("1@" + symbolBTC)
        self.nodes[0].minttokens("101@" + symbolLTC)
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolBTC,
            "tokenB": symbolLTC,
            "commission": 0.01,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "BTC-LTC",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            accountGN0: ["1@" + symbolBTC, "100@" + symbolLTC]
        }, accountGN0, [])
        self.nodes[0].generate(1)

        # Test round up
        new_dest = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolLTC,
                "amountFrom": 0.00000001,
                "to": new_dest,
                "tokenTo": symbolBTC
            })
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(new_dest, {}, True)[idBitcoin], Decimal('0.00000001'))

        # Reset swap
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Test for 2 Sat round up
        self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolLTC,
                "amountFrom": 0.00000190,
                "to": new_dest,
                "tokenTo": symbolBTC
            })
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(new_dest, {}, True)[idBitcoin], Decimal('0.00000002'))

        # Reset swap and move to Fort Canning Park Height and try swap again
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()
        self.nodes[0].generate(170 - self.nodes[0].getblockcount())

        # Test swap now results in zero amount
        self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolLTC,
                "amountFrom": 0.00000001,
                "to": new_dest,
                "tokenTo": symbolBTC
            })
        self.nodes[0].generate(1)

        assert(idBitcoin not in self.nodes[0].getaccount(new_dest, {}, True))

        # Reset swap
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Test previous 2 Sat swap now results in 1 Sat
        self.nodes[0].poolswap({
                "from": accountGN0,
                "tokenFrom": symbolLTC,
                "amountFrom": 0.00000190,
                "to": new_dest,
                "tokenTo": symbolBTC
            })
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(new_dest, {}, True)[idBitcoin], Decimal('0.00000001'))

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/poolpairs/%s/token_a_fee_pct'%(idGS): '0.05', 'v0/poolpairs/%s/token_b_fee_pct'%(idGS): '0.08'}})
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/poolpairs/%s/token_a_fee_pct'%(idGS): '0.05', 'v0/poolpairs/%s/token_b_fee_pct'%(idGS): '0.08'})

        result = self.nodes[0].getpoolpair(idGS)
        assert_equal(result[idGS]['dexFeePctTokenA'], Decimal('0.05'))
        assert_equal(result[idGS]['dexFeePctTokenB'], Decimal('0.08'))

        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolGOLD,
            "amountFrom": swap_from,
            "to": destination,
            "tokenTo": symbolSILVER,
        })
        commission = round((swap_from * 0.1), 8)
        amountA = swap_from - commission
        dexinfee = round(amountA * 0.05, 8)
        amountA = amountA - dexinfee
        pool = self.nodes[0].getpoolpair("GS")[idGS]
        reserveA = pool['reserveA']
        reserveB = pool['reserveB']

        self.nodes[0].generate(1)

        pool = self.nodes[0].getpoolpair("GS")[idGS]
        assert_equal(pool['reserveA'] - reserveA, amountA)
        swapped = self.nodes[0].getaccount(destination, {}, True)[idSilver]
        amountB = reserveB - pool['reserveB']
        dexoutfee = round(amountB * Decimal(0.08), 8)
        assert_equal(amountB - dexoutfee, swapped)
        assert_equal(self.nodes[0].listaccounthistory(accountGN0, {'token':symbolGOLD})[0]['amounts'], ['-200.00000000@'+symbolGOLD])

        assert_equal(self.nodes[0].getburninfo()['dexfeetokens'].sort(), ['%.8f'%(dexinfee)+symbolGOLD, '%.8f'%(dexoutfee)+symbolSILVER].sort())

        # set 1% token dex fee and commission
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/poolpairs/%s/token_a_fee_pct'%(idGS): '0.01', 'v0/poolpairs/%s/token_b_fee_pct'%(idGS): '0.01'}})
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/poolpairs/%s/token_a_fee_pct'%(idGS): '0.01', 'v0/poolpairs/%s/token_b_fee_pct'%(idGS): '0.01'})

        self.nodes[0].updatepoolpair({"pool": "GS", "commission": 0.01})
        self.nodes[0].generate(1)

        # swap 1 sat
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": symbolSILVER,
            "amountFrom": 0.00000001,
            "to": destination,
            "tokenTo": symbolGOLD,
        })
        pool = self.nodes[0].getpoolpair("GS")[idGS]
        reserveA = pool['reserveA']

        self.nodes[0].generate(1)

        pool = self.nodes[0].getpoolpair("GS")[idGS]
        assert_equal(reserveA, pool['reserveA'])

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
