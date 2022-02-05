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
from decimal import Decimal

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.FC_HEIGHT = 170
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight='+str(self.FC_HEIGHT), '-simulatemainnet', '-jellyfish_regtest=1']
            ]

    def create_tokens(self):
        self.symbolGOLD = "GOLD"
        self.symbolSILVER = "SILVER"
        self.symbolDOGE = "DOGE"

        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.nodes[0].createtoken({
            "symbol": self.symbolGOLD,
            "name": "Gold token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.nodes[0].createtoken({
            "symbol": self.symbolSILVER,
            "name": "Silver token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.nodes[0].createtoken({
            "symbol": self.symbolDOGE,
            "name": "DOGE token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.symbol_key_GOLD = "GOLD#" + str(self.get_id_token(self.symbolGOLD))
        self.symbol_key_SILVER = "SILVER#" + str(self.get_id_token(self.symbolSILVER))
        self.symbol_key_DOGE = "DOGE#" + str(self.get_id_token(self.symbolDOGE))

    def mint_tokens(self, amount=1000):

        self.nodes[0].utxostoaccount({self.account0: "199999900@DFI"})
        self.nodes[0].generate(1)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_GOLD)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_SILVER)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_DOGE)
        self.account_gs = self.nodes[0].getnewaddress("")
        self.account_sd = self.nodes[0].getnewaddress("")
        self.nodes[0].generate(1)
        self.nodes[0].accounttoaccount(self.account0, {self.account_gs: "50000000@" + self.symbol_key_GOLD})
        self.nodes[0].accounttoaccount(self.account0, {self.account_gs: "50000000@" + self.symbol_key_SILVER})
        self.nodes[0].generate(1)
        self.nodes[0].accounttoaccount(self.account0, {self.account_sd: "50000000@" + self.symbol_key_SILVER})
        self.nodes[0].accounttoaccount(self.account0, {self.account_sd: "50000000@" + self.symbol_key_DOGE})
        self.nodes[0].generate(1)

    def create_pool_pairs(self):
        owner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createpoolpair({
            "tokenA": self.symbol_key_GOLD,
            "tokenB": self.symbol_key_SILVER,
            "commission": 0.01,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].generate(1)
        self.nodes[0].createpoolpair({
            "tokenA": self.symbol_key_SILVER,
            "tokenB": self.symbol_key_DOGE,
            "commission": 0.05,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "DS",
        }, [])
        self.nodes[0].generate(1)

    def add_liquidity(self):
        self.nodes[0].addpoolliquidity({
            self.account_gs: ["5000000@" + self.symbol_key_GOLD, "500000@" + self.symbol_key_SILVER]
        }, self.account_gs, [])
        self.nodes[0].addpoolliquidity({
            self.account_sd: ["100000@" + self.symbol_key_DOGE, "1000000@" + self.symbol_key_SILVER]
        }, self.account_sd, [])
        self.nodes[0].generate(1)


    def setup(self):
        self.nodes[0].generate(self.FC_HEIGHT)
        self.create_tokens()
        self.mint_tokens(100000000)
        self.create_pool_pairs()
        self.add_liquidity()

    def test_swap_with_wrong_amounts(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '45000000.00000000@GOLD#128')
        # try swap negative amount
        try:
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": Decimal('-0.00000001'),
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            },[])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Amount out of range' in errorString)

        #try swap too small amount
        try:
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": Decimal('0.000000001'),
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            },[])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Invalid amount' in errorString)


    def test_simple_swap_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '45000000.00000000@GOLD#128')

        self.nodes[0].poolswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 0.00000001,
            "to": to_address,
            "tokenTo": self.symbol_key_SILVER,
        },[])
        self.nodes[0].generate(1)
        from_account = self.nodes[0].getaccount(from_address)
        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(from_account[1], '44999999.99999999@GOLD#128')
        assert_equal(to_account, [])

    def test_200_simple_swaps_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999999@GOLD#128')

        for _ in range(200):
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": 0.00000001,
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            },[])
        self.nodes[0].generate(1)
        from_account = self.nodes[0].getaccount(from_address)
        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(from_account[1], '44999999.99999799@GOLD#128')
        assert_equal(to_account, [])

    def test_compositeswap_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999799@GOLD#128')

        testPoolSwapRes =  self.nodes[0].testpoolswap({
            "from": from_address,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 0.00000001,
            "to": to_address,
            "tokenTo": self.symbol_key_DOGE,
        }, "auto", True)
        assert_equal(testPoolSwapRes["amount"], '0.00000000@130')
        assert_equal(len(testPoolSwapRes["pools"]), 2)

        self.nodes[0].compositeswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 0.00000001,
            "to": to_address,
            "tokenTo": self.symbol_key_DOGE,
        },[])
        self.nodes[0].generate(1)
        from_account = self.nodes[0].getaccount(from_address)
        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(from_account[1], '44999999.99999798@GOLD#128')
        assert_equal(to_account, [])

    def test_200_compositeswaps_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999798@GOLD#128')

        for _ in range(200):
            testPoolSwapRes = self.nodes[0].testpoolswap({
                "from": from_address,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": 0.00000001,
                "to": to_address,
                "tokenTo": self.symbol_key_DOGE,
            }, "auto", True)
            assert_equal(testPoolSwapRes["amount"], '0.00000000@130')
            assert_equal(len(testPoolSwapRes["pools"]), 2)
            self.nodes[0].compositeswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": 0.00000001,
                "to": to_address,
                "tokenTo": self.symbol_key_DOGE,
            },[])
        self.nodes[0].generate(1)
        from_account = self.nodes[0].getaccount(from_address)
        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(from_account[1], '44999999.99999598@GOLD#128')
        assert_equal(to_account, [])

    def run_test(self):
        self.setup()

        self.test_swap_with_wrong_amounts()
        self.test_simple_swap_1Satoshi()
        self.test_200_simple_swaps_1Satoshi()
        self.test_compositeswap_1Satoshi()
        self.test_200_compositeswaps_1Satoshi()

if __name__ == '__main__':
    PoolPairTest ().main ()
