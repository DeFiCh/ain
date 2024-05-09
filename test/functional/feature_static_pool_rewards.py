#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test static pool rewards"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

from decimal import Decimal, ROUND_DOWN
import time


class TokenFractionalSplitTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-subsidytest=1",
                "-txnotokens=0",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanningmuseumheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-grandcentralheight=1",
                "-grandcentralepilogueheight=1",
                "-metachainheight=105",
                "-df23height=110",
                "-df24height=120",
            ]
        ]

    def run_test(self):

        # Setup test
        self.setup_tests()

        # Test new pool reward
        self.static_reward_calculation()

    def setup_tests(self):

        # Set up test tokens
        self.setup_test_tokens()

        # Set up pool pair
        self.setup_poolpair()

    def setup_poolpair(self):

        # Fund address for pool
        self.nodes[0].utxostoaccount({self.address: f"1000@{self.symbolDFI}"})
        self.nodes[0].minttokens([f"1000@{self.idBTC}"])
        self.nodes[0].generate(1)

        # Create pool symbol
        pool_symbol = self.symbolBTC + "-" + self.symbolDFI

        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolBTC,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.address,
                "symbol": pool_symbol,
            }
        )
        self.nodes[0].generate(1)

        # Set pool pair splits
        pool_id = list(self.nodes[0].getpoolpair(pool_symbol).keys())[0]
        self.nodes[0].setgov({"LP_SPLITS": {pool_id: 1}})
        self.nodes[0].generate(1)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.address: [f"1000@{self.symbolBTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def setup_test_tokens(self):
        self.nodes[0].generate(110)

        # Symbols
        self.symbolBTC = "BTC"
        self.symbolDFI = "DFI"

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Create token
        self.nodes[0].createtoken(
            {
                "symbol": self.symbolBTC,
                "name": self.symbolBTC,
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        # Store token IDs
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]

    def static_reward_calculation(self):

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate pre-fork reward
        pre_fork_reward = end_balance - start_balance

        # Move past fork height
        self.nodes[0].generate(120 - self.nodes[0].getblockcount())

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate post-fork reward
        post_fork_reward = end_balance - start_balance

        # Check rewards are the same
        assert_equal(pre_fork_reward, post_fork_reward)


if __name__ == "__main__":
    TokenFractionalSplitTest().main()
