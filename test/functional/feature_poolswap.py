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
    disconnect_nodes,
    assert_raises_rpc_error,
)

from decimal import Decimal
from math import trunc

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main (Foundation)
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-fortcanningroadheight=177', '-acindex=1', '-dexstats'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-fortcanningroadheight=177', '-acindex=1', '-dexstats'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-fortcanningroadheight=177', '-dexstats']]

    def setup(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI
        self.setup_tokens()
        self.symbolGOLD = "GOLD#" + self.get_id_token("GOLD")
        self.symbolSILVER = "SILVER#" + self.get_id_token("SILVER")
        self.idGold = list(self.nodes[0].gettoken(self.symbolGOLD).keys())[0]
        self.idSilver = list(self.nodes[0].gettoken(self.symbolSILVER).keys())[0]

        self.init_accounts()
        self.create_poolpair()


    def init_accounts(self):
        self.accountGN0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.accountSN1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.owner = self.nodes[0].getnewaddress("", "legacy")
        # 2 Transferring SILVER from N1 Account to N0 Account
        self.nodes[1].accounttoaccount(self.accountSN1, {self.accountGN0: "1000@" + self.symbolSILVER})
        self.nodes[1].generate(1)
        # Transferring GOLD from N0 Account to N1 Account
        self.nodes[0].accounttoaccount(self.accountGN0, {self.accountSN1: "200@" + self.symbolGOLD})
        self.nodes[0].generate(1)

        self.silverCheckN0 = self.nodes[0].getaccount(self.accountGN0, {}, True)[self.idSilver]

    def create_poolpair(self):
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolGOLD,
            "tokenB": self.symbolSILVER,
            "commission": 0.1,
            "status": True,
            "ownerAddress": self.owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].generate(1)
        # only 4 tokens = DFI, GOLD, SILVER, GS
        assert_equal(len(self.nodes[0].listtokens()), 4)

        # check tokens id
        pool = self.nodes[0].getpoolpair("GS")
        self.idGS = list(self.nodes[0].gettoken("GS").keys())[0]
        assert_equal(pool[self.idGS]['idTokenA'], self.idGold)
        assert_equal(pool[self.idGS]['idTokenB'], self.idSilver)

    def test_swap_with_no_liquidity(self):
        assert_raises_rpc_error(-32600, "Lack of liquidity", self.nodes[0].poolswap, {
                "from": self.accountGN0,
                "tokenFrom": self.symbolSILVER,
                "amountFrom": 10,
                "to": self.accountSN1,
                "tokenTo": self.symbolGOLD,
            }
        )

    def test_add_liquidity_from_different_nodes(self):
        self.nodes[0].addpoolliquidity({
            self.accountGN0: ["100@" + self.symbolGOLD, "500@" + self.symbolSILVER]
        }, self.accountGN0, [])
        self.nodes[0].generate(1)

        self.sync_blocks([self.nodes[0], self.nodes[1]])

        self.nodes[1].addpoolliquidity({
            self.accountSN1: ["100@" + self.symbolGOLD, "500@" + self.symbolSILVER]
        }, self.accountSN1, [])
        self.nodes[1].generate(1)

        self.sync_blocks([self.nodes[0], self.nodes[1]])

        goldCheckN0 = self.nodes[0].getaccount(self.accountGN0, {}, True)[self.idGold]
        silverCheckN0 = self.nodes[0].getaccount(self.accountGN0, {}, True)[self.idSilver]

        assert_equal(goldCheckN0, 700)
        assert_equal(silverCheckN0, 500)

        list_pool = self.nodes[0].listpoolpairs()

        assert_equal(list_pool['1']['reserveA'], 200)  # GOLD
        assert_equal(list_pool['1']['reserveB'], 1000) # SILVER

    def turn_off_pool_and_try_swap(self):
        self.nodes[0].updatepoolpair({"pool": "GS", "status": False})
        self.nodes[0].generate(1)
        try:
            self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolSILVER,
                "amountFrom": 10,
                "to": self.accountSN1,
                "tokenTo": self.symbolGOLD,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("turned off" in errorString)

    def turn_on_pool_and_swap(self):
        self.nodes[0].updatepoolpair({"pool": "GS", "status": True})
        self.nodes[0].generate(1)

        testPoolSwapRes =  self.nodes[0].testpoolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 10,
            "to": self.accountSN1,
            "tokenTo": self.symbolGOLD,
        })

        self.goldCheckPS = self.nodes[2].getaccount(self.accountSN1, {}, True)[self.idGold]

        testPoolSwapSplit = str(testPoolSwapRes).split("@", 2)

        self.psTestAmount = testPoolSwapSplit[0]
        psTestTokenId = testPoolSwapSplit[1]
        assert_equal(psTestTokenId, self.idGold)

        testPoolSwapVerbose =  self.nodes[0].testpoolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 10,
            "to": self.accountSN1,
            "tokenTo": self.symbolGOLD,
        }, "direct", True)

        assert_equal(testPoolSwapVerbose["path"], "direct")
        assert_equal(testPoolSwapVerbose["pools"][0], self.idGS)
        assert_equal(testPoolSwapVerbose["amount"], testPoolSwapRes)

    def test_swap_and_live_dex_data(self):
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 10,
            "to": self.accountSN1,
            "tokenTo": self.symbolGOLD,
        }, [])
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        # silver is tokenB
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_b'%(self.idGS)], Decimal('9.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_b'%(self.idGS)], Decimal('1.0'))

        goldCheckN0 = self.nodes[2].getaccount(self.accountGN0, {}, True)[self.idGold]
        silverCheckN0 = self.nodes[2].getaccount(self.accountGN0, {}, True)[self.idSilver]

        goldCheckN1 = self.nodes[2].getaccount(self.accountSN1, {}, True)[self.idGold]
        silverCheckN1 = self.nodes[2].getaccount(self.accountSN1, {}, True)[self.idSilver]

        list_pool = self.nodes[2].listpoolpairs()

        assert_equal(goldCheckN0, 700)
        assert_equal(str(silverCheckN0), "490.49999997") # TODO: calculate "true" values with trading fee!
        assert_equal(list_pool['1']['reserveA'] + goldCheckN1 , 300)
        assert_equal(Decimal(self.goldCheckPS) + Decimal(self.psTestAmount), Decimal(goldCheckN1))
        assert_equal(str(silverCheckN1), "500.50000000")
        assert_equal(list_pool['1']['reserveB'], 1009) #1010 - 1 (commission)

    def test_price_higher_than_indicated(self):
        list_pool = self.nodes[2].listpoolpairs()
        price = list_pool['1']['reserveA/reserveB']
        try:
            self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolSILVER,
                "amountFrom": 10,
                "to": self.accountSN1,
                "tokenTo": self.symbolGOLD,
                "maxPrice": price - Decimal('0.1'),
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Price is higher than indicated." in errorString)

    def test_max_price(self):
        maxPrice = self.nodes[0].listpoolpairs()['1']['reserveB/reserveA']
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 200,
            "to": self.accountGN0,
            "tokenTo": self.symbolGOLD,
            "maxPrice": maxPrice,
        })
        assert_raises_rpc_error(-26, 'Price is higher than indicated',
                                self.nodes[0].poolswap, {
                                    "from": self.accountGN0,
                                    "tokenFrom": self.symbolSILVER,
                                    "amountFrom": 200,
                                    "to": self.accountGN0,
                                    "tokenTo": self.symbolGOLD,
                                    "maxPrice": maxPrice,
                                }
        )
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_b'%(self.idGS)], Decimal('189.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_b'%(self.idGS)], Decimal('21.0'))

        maxPrice = self.nodes[0].listpoolpairs()['1']['reserveB/reserveA']
        # exchange tokens each other should work
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolGOLD,
            "amountFrom": 200,
            "to": self.accountGN0,
            "tokenTo": self.symbolSILVER,
            "maxPrice": maxPrice,
        })
        self.nodes[0].generate(1)
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 200,
            "to": self.accountGN0,
            "tokenTo": self.symbolGOLD,
            "maxPrice": maxPrice,
        })
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_a'%(self.idGS)], Decimal('180.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_a'%(self.idGS)], Decimal('20.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_b'%(self.idGS)], Decimal('369.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_b'%(self.idGS)], Decimal('41.0'))

    def test_fort_canning_max_price_change(self):
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[0], 2)

        destination = self.nodes[0].getnewaddress("", "legacy")
        self.swap_from = 200
        self.coin = 100000000

        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolGOLD,
            "amountFrom": self.swap_from,
            "to": destination,
            "tokenTo": self.symbolSILVER
        })
        self.nodes[0].generate(1)

        silver_received = self.nodes[0].getaccount(destination, {}, True)[self.idSilver]
        self.silver_per_gold = round((self.swap_from * self.coin) / (silver_received * self.coin), 8)

        # Reset swap and try again with max price set to expected amount
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolGOLD,
            "amountFrom": self.swap_from,
            "to": destination,
            "tokenTo": self.symbolSILVER,
            "maxPrice": self.silver_per_gold,
        })
        self.nodes[0].generate(1)

    def test_fort_canning_max_price_one_satoshi_below(self):
        # Reset swap and try again with max price set to one Satoshi below
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        destination = self.nodes[0].getnewaddress("", "legacy")
        try:
            self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolGOLD,
                "amountFrom": 200,
                "to": destination,
                "tokenTo": self.symbolSILVER,
                "maxPrice": self.silver_per_gold - Decimal('0.00000001'),
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Price is higher than indicated" in errorString)

    def setup_new_pool_BTC_LTC(self):
        self.nodes[0].createtoken({
                "symbol": "BTC",
                "name": "Bitcoin",
                "collateralAddress": self.accountGN0
            })
        self.nodes[0].createtoken({
                "symbol": "LTC",
                "name": "Litecoin",
                "collateralAddress": self.accountGN0
            })
        self.nodes[0].generate(1)

        self.symbolBTC = "BTC#" + self.get_id_token("BTC")
        self.symbolLTC = "LTC#" + self.get_id_token("LTC")
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]
        self.idLTC = list(self.nodes[0].gettoken(self.symbolLTC).keys())[0]

        self.nodes[0].minttokens("1@" + self.symbolBTC)
        self.nodes[0].minttokens("111@" + self.symbolLTC)
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolBTC,
            "tokenB": self.symbolLTC,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.owner,
            "pairSymbol": "BTC-LTC",
        }, [])
        self.nodes[0].generate(1)

        self.idBL = list(self.nodes[0].gettoken("BTC-LTC").keys())[0]

        self.nodes[0].addpoolliquidity({
            self.accountGN0: ["1@" + self.symbolBTC, "100@" + self.symbolLTC]
        }, self.accountGN0, [])
        self.nodes[0].generate(1)

    def one_satoshi_swap(self):
        new_dest = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 0.00000001,
                "to": new_dest,
                "tokenTo": self.symbolBTC
            })
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(new_dest, {}, True)[self.idBTC], Decimal('0.00000001'))

    def test_two_satoshi_round_up(self, FCP=False):
        # Reset swap
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        new_dest = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 0.00000190,
                "to": new_dest,
                "tokenTo": self.symbolBTC
            })
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_a'%(self.idBL)], Decimal('0.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_a'%(self.idBL)], Decimal('0.0'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_swap_b'%(self.idBL)], Decimal('0.00000189'))
        assert_equal(attributes['v0/live/economy/dex/%s/total_commission_b'%(self.idBL)], Decimal('1E-8'))

        expected_round_up = FCP and Decimal('0.00000001') or Decimal('0.00000002')
        assert_equal(self.nodes[0].getaccount(new_dest, {}, True)[self.idBTC], expected_round_up)

    def reset_swap_move_to_FCP_and_swap(self):
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()
        self.nodes[0].generate(170 - self.nodes[0].getblockcount())

        new_dest = self.nodes[0].getnewaddress("", "legacy")
        # Test swap now results in zero amount
        self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 0.00000001,
                "to": new_dest,
                "tokenTo": self.symbolBTC
            })
        self.nodes[0].generate(1)

        assert(self.idBTC not in self.nodes[0].getaccount(new_dest, {}, True))

    def set_token_and_pool_fees(self):
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/poolpairs/%s/token_a_fee_pct'%(self.idGS): '0.05', 'v0/poolpairs/%s/token_b_fee_pct'%(self.idGS): '0.08'}})
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/poolpairs/%s/token_a_fee_pct'%(self.idGS)], '0.05')
        assert_equal(attributes['v0/poolpairs/%s/token_b_fee_pct'%(self.idGS)], '0.08')

        result = self.nodes[0].getpoolpair(self.idGS)
        assert_equal(result[self.idGS]['dexFeePctTokenA'], Decimal('0.05'))
        assert_equal(result[self.idGS]['dexFeePctTokenB'], Decimal('0.08'))

    def test_listaccounthistory_and_burninfo(self):
        destination = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolGOLD,
            "amountFrom": self.swap_from,
            "to": destination,
            "tokenTo": self.symbolSILVER,
        })
        commission = round((self.swap_from * 0.1), 8)
        amountA = self.swap_from - commission
        dexinfee = round(amountA * 0.05, 8)
        amountA = amountA - dexinfee
        pool = self.nodes[0].getpoolpair("GS")[self.idGS]
        reserveA = pool['reserveA']
        reserveB = pool['reserveB']

        self.nodes[0].generate(1)

        pool = self.nodes[0].getpoolpair("GS")[self.idGS]
        assert_equal(pool['reserveA'] - reserveA, amountA)
        swapped = self.nodes[0].getaccount(destination, {}, True)[self.idSilver]
        amountB = reserveB - pool['reserveB']
        dexoutfee = round(amountB * Decimal(0.08), 8)
        assert_equal(amountB - dexoutfee, swapped)
        assert_equal(self.nodes[0].listaccounthistory(self.accountGN0, {'token':self.symbolGOLD})[0]['amounts'], ['-200.00000000@'+self.symbolGOLD])

        assert_equal(self.nodes[0].getburninfo()['dexfeetokens'].sort(), ['%.8f'%(dexinfee)+self.symbolGOLD, '%.8f'%(dexoutfee)+self.symbolSILVER].sort())

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/dex/%s/fee_burn_a'%(self.idGS)], dexinfee)
        assert_equal(attributes['v0/live/economy/dex/%s/fee_burn_b'%(self.idGS)], dexoutfee)

    def update_comission_and_fee_to_1pct_pool1(self):
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/poolpairs/%s/token_a_fee_pct'%(self.idGS): '0.01', 'v0/poolpairs/%s/token_b_fee_pct'%(self.idGS): '0.01'}})
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/poolpairs/%s/token_a_fee_pct'%(self.idGS)], '0.01')
        assert_equal(attributes['v0/poolpairs/%s/token_b_fee_pct'%(self.idGS)], '0.01')

        self.nodes[0].updatepoolpair({"pool": "GS", "commission": 0.01})
        self.nodes[0].generate(1)

        destination = self.nodes[0].getnewaddress("", "legacy")
        # swap 1 sat
        self.nodes[0].poolswap({
            "from": self.accountGN0,
            "tokenFrom": self.symbolSILVER,
            "amountFrom": 0.00000001,
            "to": destination,
            "tokenTo": self.symbolGOLD,
        })
        pool = self.nodes[0].getpoolpair("GS")[self.idGS]
        reserveA = pool['reserveA']

        self.nodes[0].generate(1)

        pool = self.nodes[0].getpoolpair("GS")[self.idGS]
        assert_equal(reserveA, pool['reserveA'])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/poolpairs/%s/token_a_fee_pct'%(self.idBL): '0.01', 'v0/poolpairs/%s/token_b_fee_pct'%(self.idBL): '0.01'}})
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/poolpairs/%s/token_a_fee_pct'%(self.idGS)], '0.01')
        assert_equal(attributes['v0/poolpairs/%s/token_b_fee_pct'%(self.idGS)], '0.01')
        assert_equal(attributes['v0/poolpairs/%s/token_a_fee_pct'%(self.idBL)], '0.01')
        assert_equal(attributes['v0/poolpairs/%s/token_b_fee_pct'%(self.idBL)], '0.01')

    def update_comission_and_fee_to_1pct_pool2(self):
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/%s/dex_in_fee_pct'%(self.idLTC): '0.02', 'v0/token/%s/dex_out_fee_pct'%(self.idBTC): '0.05'}})
        self.nodes[0].generate(1)

        result = self.nodes[0].getpoolpair(self.idBL)
        assert_equal(result[self.idBL]['dexFeeInPctTokenB'], Decimal('0.02'))
        assert_equal(result[self.idBL]['dexFeeOutPctTokenA'], Decimal('0.05'))

        destBTC = self.nodes[0].getnewaddress("", "legacy")
        swapltc = 10
        self.nodes[0].poolswap({
                "from": self.accountGN0,
                "tokenFrom": self.symbolLTC,
                "amountFrom": swapltc,
                "to": destBTC,
                "tokenTo": self.symbolBTC
        })
        commission = round((swapltc * 0.01), 8)
        amountB = Decimal(swapltc - commission)
        dexinfee = amountB * Decimal(0.02)
        amountB = amountB - dexinfee
        pool = self.nodes[0].getpoolpair("BTC-LTC")[self.idBL]
        reserveA = pool['reserveA']
        reserveB = pool['reserveB']

        self.nodes[0].generate(1)

        pool = self.nodes[0].getpoolpair("BTC-LTC")[self.idBL]
        assert_equal(pool['reserveB'] - reserveB, round(amountB, 8))
        swapped = self.nodes[0].getaccount(destBTC, {}, True)[self.idBTC]
        amountA = reserveA - pool['reserveA']
        dexoutfee = round(trunc(amountA * Decimal(0.05) * self.coin) / self.coin, 8)
        assert_equal(round(amountA - Decimal(dexoutfee), 8), round(swapped, 8))

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/dex/%s/fee_burn_b'%(self.idBL)], round(dexinfee, 8))
        assert_equal(attributes['v0/live/economy/dex/%s/fee_burn_a'%(self.idBL)], Decimal(str(round(dexoutfee, 8))))

    def test_testpoolswap_errors(self):
        assert_raises_rpc_error(-8, "tokenFrom is empty", self.nodes[0].testpoolswap, {
                                "amountFrom": 0.1, "tokenFrom": "", "tokenTo": self.symbolBTC, "from": self.accountGN0, "to": self.accountSN1, "maxPrice": 0.1})
        assert_raises_rpc_error(-8, "tokenTo is empty", self.nodes[0].testpoolswap, {
                                "amountFrom": 0.1, "tokenFrom": self.symbolBTC, "tokenTo": "", "from": self.accountGN0, "to": self.accountSN1, "maxPrice": 0.1})
        assert_raises_rpc_error(-32600, "Input amount should be positive", self.nodes[0].testpoolswap, {
                                "amountFrom": 0, "tokenFrom": self.symbolLTC, "tokenTo": self.symbolBTC, "from": self.accountGN0, "to": self.accountSN1, "maxPrice": 0.1})

    def revert_to_initial_state(self):
        self.rollback_to(block=0, nodes=[0, 1, 2])
        assert_equal(len(self.nodes[0].listpoolpairs()), 0)
        assert_equal(len(self.nodes[1].listpoolpairs()), 0)
        assert_equal(len(self.nodes[2].listpoolpairs()), 0)


    def run_test(self):
        self.setup()
        self.test_swap_with_no_liquidity()
        self.test_add_liquidity_from_different_nodes()
        self.turn_off_pool_and_try_swap()
        self.turn_on_pool_and_swap()
        self.test_swap_and_live_dex_data()
        self.test_price_higher_than_indicated()
        self.test_max_price()
        self.test_fort_canning_max_price_change()
        self.test_fort_canning_max_price_one_satoshi_below()
        self.setup_new_pool_BTC_LTC()
        self.one_satoshi_swap()
        self.test_two_satoshi_round_up()
        self.reset_swap_move_to_FCP_and_swap()
        self.test_two_satoshi_round_up(FCP=True)
        self.set_token_and_pool_fees()
        self.test_listaccounthistory_and_burninfo()
        self.update_comission_and_fee_to_1pct_pool1()
        self.update_comission_and_fee_to_1pct_pool2()
        self.test_testpoolswap_errors()
        self.revert_to_initial_state()

if __name__ == '__main__':
    PoolPairTest ().main ()
