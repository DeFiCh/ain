#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify send tokens operation with autoselection accounts balances
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal

class SendTokensToAddressTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # We need to enlarge -datacarriersize for allowing for test big OP_RETURN scripts
        # resulting from building AnyAccountsToAccounts msg with many accounts balances
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
        ]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        tokens = [
            {
                "wallet": self.nodes[0],
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 30
            },
            {
                "wallet": self.nodes[0],
                "symbol": "SILVER",
                "name": "just silver",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 30
            },
        ]
        # inside this function "tokenId" and "symbolId" will be assigned for each token obj
        self.setup_tokens(tokens)

        token0_symbol = tokens[0]["symbolId"]
        token1_symbol = tokens[1]["symbolId"]
        token0_tokenId = tokens[0]["tokenId"]
        token1_tokenId = tokens[1]["tokenId"]

        to = {}
        wallet1_addr1 = self.nodes[1].getnewaddress("", "legacy")
        wallet1_addr2 = self.nodes[1].getnewaddress("", "legacy")
        to[wallet1_addr1] = ["10@" + token0_symbol, "20@" + token1_symbol]
        to[wallet1_addr2] = ["20@" + token0_symbol, "10@" + token1_symbol]

        self.nodes[0].sendtokenstoaddress({}, to, "forward")

        self.nodes[0].generate(1)
        self.sync_blocks()

        wallet1_addr1_balance = self.nodes[0].getaccount(wallet1_addr1, {}, True)
        wallet1_addr2_balance = self.nodes[0].getaccount(wallet1_addr2, {}, True)

        assert_equal(wallet1_addr1_balance[token0_tokenId], Decimal(10))
        assert_equal(wallet1_addr1_balance[token1_tokenId], Decimal(20))
        assert_equal(wallet1_addr2_balance[token0_tokenId], Decimal(20))
        assert_equal(wallet1_addr2_balance[token1_tokenId], Decimal(10))

        print("Test sendtokenstoaddress pie...")
        to = {}
        wallet0_addr = self.nodes[0].getnewaddress("", "legacy")
        to[wallet0_addr] = ["15@" + token0_symbol, "15@" + token1_symbol]

        self.nodes[1].sendtokenstoaddress({}, to, "pie")

        self.nodes[1].generate(1)
        self.sync_blocks()

        wallet1_addr1_balance = self.nodes[0].getaccount(wallet1_addr1, {}, True)
        wallet1_addr2_balance = self.nodes[0].getaccount(wallet1_addr2, {}, True)

        # pie gets amount by desc order
        assert_equal(wallet1_addr1_balance[token0_tokenId], Decimal(10))
        assert_equal(wallet1_addr1_balance[token1_tokenId], Decimal(5))
        assert_equal(wallet1_addr2_balance[token0_tokenId], Decimal(5))
        assert_equal(wallet1_addr2_balance[token1_tokenId], Decimal(10))

        print("Test sendtokenstoaddress crumbs...")
        to[wallet0_addr] = ["6@" + token0_symbol, "7@" + token1_symbol]

        self.nodes[1].sendtokenstoaddress({}, to, "crumbs")

        self.nodes[1].generate(1)
        self.sync_blocks()

        wallet1_addr1_balance = self.nodes[0].getaccount(wallet1_addr1, {}, True)
        wallet1_addr2_balance = self.nodes[0].getaccount(wallet1_addr2, {}, True)

        # crumbs gets amount by asc order, 0 balances not present
        assert(token1_tokenId not in wallet1_addr1_balance)
        assert(token0_tokenId not in wallet1_addr2_balance)
        assert_equal(wallet1_addr1_balance[token0_tokenId], Decimal(9))
        assert_equal(wallet1_addr2_balance[token1_tokenId], Decimal(8))

if __name__ == '__main__':
    SendTokensToAddressTest().main()
