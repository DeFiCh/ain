#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test future swap limitation"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import time


class FutureSwapLimitationTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
                "-subsidytest=1",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-dakotaheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-fortcanningepilogueheight=1",
                "-grandcentralheight=1",
                "-metachainheight=100",
                "-df23height=150",
            ],
        ]

    def run_test(self):
        # Run setup
        self.setup()

        # Test future swap limitation
        self.test_future_swap_limitation()

        # Test wiping of data on disable
        self.test_wiping_data()

    def setup(self):
        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        # Setup oracles
        self.setup_oracles()

        # Setup tokens
        self.setup_tokens()

        # Setup Gov vars
        self.setup_govvars()

        # Setup Pool
        self.setup_pool()

    def setup_oracles(self):
        # Price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "META"},
            {"currency": "USD", "token": "DUSD"},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@META"},
            {"currency": "USD", "tokenAmount": "1@DUSD"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):
        # Set loan tokens
        self.nodes[0].setloantoken(
            {
                "symbol": "META",
                "name": "Meta",
                "fixedIntervalPriceId": "META/USD",
                "isDAT": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken(
            {
                "symbol": "DUSD",
                "name": "DUSD",
                "fixedIntervalPriceId": "DUSD/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        # Set collateral token
        self.nodes[0].setcollateraltoken(
            {
                "token": "DFI",
                "factor": 1,
                "fixedIntervalPriceId": "DFI/USD",
            }
        )
        self.nodes[0].generate(1)

        # Mint tokens
        self.nodes[0].minttokens(["1000@META"])
        self.nodes[0].minttokens(["1000@DUSD"])
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})
        self.nodes[0].generate(1)

    def setup_govvars(self):
        # Activate EVM and transfer domain
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2203/start_block": "150",
                    "v0/params/dfip2203/reward_pct": "0.05",
                    "v0/params/dfip2203/block_period": "20",
                }
            }
        )
        self.nodes[0].generate(1)

        # Fully enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/dfip2203/active": "true"}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(result["v0/params/dfip2203/active"], "true")
        assert_equal(result["v0/params/dfip2203/reward_pct"], "0.05")
        assert_equal(result["v0/params/dfip2203/fee_pct"], "0.05")
        assert_equal(result["v0/params/dfip2203/block_period"], "20")

    def setup_pool(self):
        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": "META",
                "tokenB": "DUSD",
                "commission": 0,
                "status": True,
                "ownerAddress": self.address,
                "pairSymbol": "META-DUSD",
            },
            [],
        )
        self.nodes[0].generate(1)

        # Add liquidity
        self.nodes[0].addpoolliquidity(
            {self.address: ["100@META", "100@DUSD"]}, self.address
        )
        self.nodes[0].generate(1)

    def test_future_swap_limitation(self):
        # Try and set future swap limitaiton before fork height
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "true",
                }
            },
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/average_liquidity_percentage": "0.1",
                }
            },
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/block_period": "20",
                }
            },
        )

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Set future swap limitaiton
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "true",
                    "v0/params/dfip2211f/average_liquidity_percentage": "0.1",
                    "v0/params/dfip2211f/block_period": "20",
                }
            }
        )
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(result["v0/params/dfip2211f/active"], "true")
        assert_equal(result["v0/params/dfip2211f/average_liquidity_percentage"], "0.1")
        assert_equal(result["v0/params/dfip2211f/block_period"], "20")

        # Move to future swap event
        self.nodes[0].generate(190 - self.nodes[0].getblockcount())

        # Check liquidity data
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            {"META-DUSD": "100.00000000", "DUSD-META": "100.00000000"},
        )

        # Try and swap above limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 10.00000000@META",
            self.nodes[0].futureswap,
            self.address,
            "10.00000001@META",
        )
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 10.00000000@DUSD",
            self.nodes[0].futureswap,
            self.address,
            "10.00000001@DUSD",
            "META",
        )

        # Move midway in future swap period
        self.nodes[0].generate(199 - self.nodes[0].getblockcount())

        # Execute pool swap to change liquidity
        self.nodes[0].poolswap(
            {
                "from": self.address,
                "tokenFrom": "DUSD",
                "amountFrom": 100,
                "to": self.address,
                "tokenTo": "META",
            }
        )
        self.nodes[0].generate(1)

        # Swap the max limit
        self.nodes[0].futureswap(self.address, "5.00000000@META")
        self.nodes[0].futureswap(self.address, "5.00000000@META")
        self.nodes[0].generate(1)

        # Try and swap above limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 0.00000000@META",
            self.nodes[0].futureswap,
            self.address,
            "0.00000001@META",
        )

        # Move to future swap event
        self.nodes[0].generate(210 - self.nodes[0].getblockcount())

        # Check liquidity data changed
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            {"META-DUSD": "72.48624516", "DUSD-META": "155.00000000"},
        )

        # Try and swap above new limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 7.24862451@META",
            self.nodes[0].futureswap,
            self.address,
            "7.24862452@META",
        )

        # Try and swap above new limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 15.50000000@DUSD",
            self.nodes[0].futureswap,
            self.address,
            "15.50000001@DUSD",
            "META",
        )

        # Swap the max limit
        self.nodes[0].futureswap(self.address, "10.00000000@DUSD", "META")
        self.nodes[0].futureswap(self.address, "5.50000000@DUSD", "META")
        self.nodes[0].generate(1)

        # Try and swap above new limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 0.00000000@DUSD",
            self.nodes[0].futureswap,
            self.address,
            "0.00000001@DUSD",
            "META",
        )

    def test_wiping_data(self):
        # Disable future swap limitaiton
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "false",
                }
            }
        )
        self.nodes[0].generate(1)

        # Check liquidity data empty
        assert_equal(self.nodes[0].listloantokenliquidity(), {})


if __name__ == "__main__":
    FutureSwapLimitationTest().main()
