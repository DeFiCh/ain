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
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=0', '-dakotaheight=160', '-fortcanningheight=163', '-fortcanninghillheight=170', '-simulatemainnet', '-jellyfish_regtest=1']
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
        self.nodes[0].createtoken({
            "symbol": self.symbolSILVER,
            "name": "Silver token",
            "collateralAddress": self.account0
        })
        self.nodes[0].createtoken({
            "symbol": self.symbolDOGE,
            "name": "DOGE token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.symbol_key_GOLD = "GOLD#" + str(self.get_id_token(self.symbolGOLD))
        self.symbol_key_SILVER = "SILVER#" + str(self.get_id_token(self.symbolSILVER))
        self.symbol_key_DOGE = "DOGE#" + str(self.get_id_token(self.symbolDOGE))

        self.idGold = list(self.nodes[0].gettoken(self.symbol_key_GOLD).keys())[0]
        self.idSilver = list(self.nodes[0].gettoken(self.symbol_key_SILVER).keys())[0]
        self.idDOGE = list(self.nodes[0].gettoken(self.symbol_key_DOGE).keys())[0]

    def mint_tokens(self, amount=1000):

        self.nodes[0].utxostoaccount({self.account0: "199999900@DFI"})
        self.nodes[0].generate(1)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_GOLD)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_SILVER)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_DOGE)
        self.account_gs = self.nodes[0].getnewaddress("")
        self.account_sd = self.nodes[0].getnewaddress("")
        self.account_dg = self.nodes[0].getnewaddress("")
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
        self.nodes[0].createpoolpair({
            "tokenA": self.symbol_key_SILVER,
            "tokenB": self.symbol_key_DOGE,
            "commission": 1,
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
        self.nodes[0].generate(120)
        self.create_tokens()
        self.mint_tokens(100000000)
        self.create_pool_pairs()
        self.add_liquidity()



    def run_test(self):
        self.setup()

        silver_swaps_add = self.nodes[0].getnewaddress("")
        self.nodes[0].poolswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 100,
            "to": silver_swaps_add,
            "tokenTo": self.symbol_key_SILVER,
        },[])
        self.nodes[0].generate(1)
        silver_account = self.nodes[0].getaccount(silver_swaps_add)
        assert_equal(silver_account[0], '9.89980399@SILVER#128')

        # THIS IS FAILING
        self.nodes[0].poolswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 100,
            "to": self.account_sd,
            "tokenTo": self.symbol_key_DOGE,
        },[])

        testPoolSwapRes =  self.nodes[0].testpoolswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 100,
            "to": self.account_dg,
            "tokenTo": self.symbol_key_DOGE,
        }, "auto", True)

        testPoolSwapVerbose =  self.nodes[0].testpoolswap({
            "from": self.account_gs,
            "tokenFrom": self.symbol_key_GOLD,
            "amountFrom": 0.00000001,
            "to": self.account_dg,
            "tokenTo": self.symbol_key_DOGE,
        }, "auto", True)

if __name__ == '__main__':
    PoolPairTest ().main ()
