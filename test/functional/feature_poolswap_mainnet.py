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


class PoolPairTest(DefiTestFramework):
    def set_test_params(self):
        self.FCH_HEIGHT = 170
        self.FCR_HEIGHT = 200
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160',
             '-fortcanningheight=163', '-fortcanninghillheight=' + str(self.FCH_HEIGHT),
             '-fortcanningroadheight=' + str(self.FCR_HEIGHT), '-simulatemainnet', '-jellyfish_regtest=1']
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

    def add_1satoshi_liquidity_empty_pool(self):
        errorString = ''  # remove [pylint E0601]
        try:
            self.nodes[0].addpoolliquidity({
                self.account_gs: ["0.0000001@" + self.symbol_key_GOLD, "0.0000001@" + self.symbol_key_SILVER]
            }, self.account_gs, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert ('liquidity too low' in errorString)

    def add_liquidity(self):
        self.nodes[0].addpoolliquidity({
            self.account_gs: ["5000000@" + self.symbol_key_GOLD, "500000@" + self.symbol_key_SILVER]
        }, self.account_gs, [])
        self.nodes[0].addpoolliquidity({
            self.account_sd: ["100000@" + self.symbol_key_DOGE, "1000000@" + self.symbol_key_SILVER]
        }, self.account_sd, [])
        self.nodes[0].generate(1)

    def add_1satoshi_liquidity_non_empty_pool(self):
        errorString = ''  # remove [pylint E0601]
        try:
            self.nodes[0].addpoolliquidity({
                self.account_gs: ["0.00000001@" + self.symbol_key_GOLD, "0.00000001@" + self.symbol_key_SILVER]
            }, self.account_gs, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert ('amounts too low, zero liquidity' in errorString)

    def setup(self):
        self.nodes[0].generate(self.FCH_HEIGHT)
        self.create_tokens()
        self.mint_tokens(100000000)
        self.create_pool_pairs()
        self.add_1satoshi_liquidity_empty_pool()
        self.add_liquidity()
        self.add_1satoshi_liquidity_non_empty_pool()

    def test_swap_with_wrong_amounts(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '45000000.00000000@GOLD#128')
        # try swap negative amount
        errorString = ''  # remove [pylint E0601]
        try:
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": Decimal('-0.00000001'),
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert ('Amount out of range' in errorString)

        # try swap too small amount
        try:
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": Decimal('0.000000001'),
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert ('Invalid amount' in errorString)

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
        }, [])
        self.nodes[0].generate(1)

        from_account = self.nodes[0].getaccount(from_address)
        assert_equal(from_account[1], '44999999.99999999@GOLD#128')

        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(to_account, [])

        poolpair_info = self.nodes[0].getpoolpair("GS")
        assert_equal(poolpair_info['1']['reserveA'], Decimal('5000000.00000001'))
        assert_equal(poolpair_info['1']['reserveB'], Decimal('500000.00000000'))

    def test_50_simple_swaps_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999999@GOLD#128')

        for _ in range(50):
            self.nodes[0].poolswap({
                "from": self.account_gs,
                "tokenFrom": self.symbol_key_GOLD,
                "amountFrom": 0.00000001,
                "to": to_address,
                "tokenTo": self.symbol_key_SILVER,
            }, [])
        self.nodes[0].generate(1)

        from_account = self.nodes[0].getaccount(from_address)
        assert_equal(from_account[1], '44999999.99999949@GOLD#128')

        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(to_account, [])

        poolpair_info = self.nodes[0].getpoolpair("GS")
        assert_equal(poolpair_info['1']['reserveA'], Decimal('5000000.00000051'))
        assert_equal(poolpair_info['1']['reserveB'], Decimal('500000.00000000'))

    def test_compositeswap_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999949@GOLD#128')

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
        }, [])
        self.nodes[0].generate(1)

        from_account = self.nodes[0].getaccount(from_address)
        assert_equal(from_account[1], '44999999.99999948@GOLD#128')

        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(to_account, [])

        poolpair_info_GS = self.nodes[0].getpoolpair("GS")
        assert_equal(poolpair_info_GS['1']['reserveA'], Decimal('5000000.00000052'))
        assert_equal(poolpair_info_GS['1']['reserveB'], Decimal('500000.00000000'))

        # Second pool nmo changes as 1 sat is lost in comissions in first swap
        poolpair_info_SD = self.nodes[0].getpoolpair("DS")
        assert_equal(poolpair_info_SD['2']['reserveA'], Decimal('1000000.00000000'))
        assert_equal(poolpair_info_SD['2']['reserveB'], Decimal('100000.00000000'))

    def test_50_compositeswaps_1Satoshi(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999948@GOLD#128')

        for _ in range(50):
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
            }, [])

        self.nodes[0].generate(1)

        from_account = self.nodes[0].getaccount(from_address)
        assert_equal(from_account[1], '44999999.99999898@GOLD#128')

        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(to_account, [])

        poolpair_info_GS = self.nodes[0].getpoolpair("GS")
        assert_equal(poolpair_info_GS['1']['reserveA'], Decimal('5000000.00000102'))
        assert_equal(poolpair_info_GS['1']['reserveB'], Decimal('500000.00000000'))

        # Second pool nmo changes as 1 sat is lost in comissions in first swap
        poolpair_info_SD = self.nodes[0].getpoolpair("DS")
        assert_equal(poolpair_info_SD['2']['reserveA'], Decimal('1000000.00000000'))
        assert_equal(poolpair_info_SD['2']['reserveB'], Decimal('100000.00000000'))

    def goto(self, height):
        current_block = self.nodes[0].getblockcount()
        self.nodes[0].generate((height - current_block) + 1)

    def test_swap_full_amount_of_one_side_of_pool(self):
        from_address = self.account_gs
        from_account = self.nodes[0].getaccount(from_address)
        to_address = self.nodes[0].getnewaddress("")
        assert_equal(from_account[1], '44999999.99999898@GOLD#128')

        testPoolSwapRes = self.nodes[0].testpoolswap({
            "from": from_address,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": Decimal('5000000.00000102'),
            "to": to_address,
            "tokenTo": self.symbol_key_SILVER,
        }, "auto", True)
        assert_equal(testPoolSwapRes["amount"], '248743.71859296@129')
        assert_equal(len(testPoolSwapRes["pools"]), 1)

        self.nodes[0].compositeswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": Decimal('5000000.00000102'),
            "to": to_address,
            "tokenTo": self.symbol_key_SILVER,
        }, [])
        self.nodes[0].generate(1)

        from_account = self.nodes[0].getaccount(from_address)
        assert_equal(from_account[1], '40049999.99999765@GOLD#128')

        to_account = self.nodes[0].getaccount(to_address)
        assert_equal(to_account, ['248743.71859296@SILVER#129'])

        poolpair_info_GS = self.nodes[0].getpoolpair("GS")
        assert_equal(poolpair_info_GS['1']['reserveA'], Decimal('9950000.00000203'))
        assert_equal(poolpair_info_GS['1']['reserveB'], Decimal('251256.28140704'))

    def run_test(self):
        self.setup()

        self.test_swap_with_wrong_amounts()
        self.test_simple_swap_1Satoshi()
        self.test_50_simple_swaps_1Satoshi()
        self.test_compositeswap_1Satoshi()
        self.test_50_compositeswaps_1Satoshi()

        self.goto(self.FCR_HEIGHT)  # Move to FCR

        self.test_swap_full_amount_of_one_side_of_pool()


if __name__ == '__main__':
    PoolPairTest().main()
