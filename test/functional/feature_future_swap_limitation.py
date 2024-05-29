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

        # Test longer future swap limitation period
        self.test_longer_fs_limit_period()

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

        # Setup Future Swap Limitation Gov vars
        self.test_and_set_fs_limit_vars()

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

        # Set Future Swap Gov vars
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

        # Store start block height
        self.start_block = self.nodes[0].getblockcount()

    def test_and_set_fs_limit_vars(self):

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
                    "v0/params/dfip2211f/liquidity_calc_sampling_period": "1",
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

        # Try and set block period below default sample size
        assert_raises_rpc_error(
            -32600,
            "Block period must be more than sampling period",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/block_period": "20",
                }
            },
        )

        # Try and set sample size of zero
        assert_raises_rpc_error(
            -5,
            "Value must be more than zero",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/liquidity_calc_sampling_period": "0",
                }
            },
        )

        # Set future swap limitaiton
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "true",
                    "v0/params/dfip2211f/liquidity_calc_sampling_period": "2",
                    "v0/params/dfip2211f/average_liquidity_percentage": "0.1",
                    "v0/params/dfip2211f/block_period": "20",
                }
            }
        )
        self.nodes[0].generate(1)

        # Try and set block period below defined sample size
        assert_raises_rpc_error(
            -32600,
            "Block period must be more than sampling period",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/block_period": "1",
                }
            },
        )

        # Verify Gov vars
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(result["v0/params/dfip2211f/active"], "true")
        assert_equal(result["v0/params/dfip2211f/liquidity_calc_sampling_period"], "2")
        assert_equal(result["v0/params/dfip2211f/average_liquidity_percentage"], "0.1")
        assert_equal(result["v0/params/dfip2211f/block_period"], "20")

    def test_future_swap_limitation(self):

        # Roll back to fork height
        self.rollback_to(self.start_block)

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Set future swap limitaiton
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "true",
                    "v0/params/dfip2211f/liquidity_calc_sampling_period": "2",
                    "v0/params/dfip2211f/average_liquidity_percentage": "0.1",
                    "v0/params/dfip2211f/block_period": "20",
                }
            }
        )
        self.nodes[0].generate(1)

        # Move to future swap event
        self.nodes[0].generate(190 - self.nodes[0].getblockcount())

        # Check liquidity data
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            [
                {
                    "META-DUSD": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
            ],
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

        # Swap half the limit
        self.nodes[0].futureswap(self.address, "5.00000000@META")
        self.nodes[0].generate(1)

        # Check liquidity data
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            [
                {
                    "META-DUSD": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "5.00000000",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
            ],
        )

        # Swap the max limit
        self.nodes[0].futureswap(self.address, "5.00000000@META")
        self.nodes[0].generate(1)

        # Check liquidity data
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            [
                {
                    "META-DUSD": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "0.00000000",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
            ],
        )

        # Swap the max limit in the other direction
        self.nodes[0].futureswap(self.address, "10.00000000@DUSD", "META")
        self.nodes[0].generate(1)

        # Check liquidity data
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            [
                {
                    "META-DUSD": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "0.00000000",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "0.00000000",
                    }
                },
            ],
        )

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
            [
                {
                    "META-DUSD": {
                        "liquidity": "69.98499472",
                        "limit": "6.99849947",
                        "remaining": "6.99849947",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "160.00000000",
                        "limit": "16.00000000",
                        "remaining": "16.00000000",
                    }
                },
            ],
        )

        # Try and swap above new limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 6.99849947@META",
            self.nodes[0].futureswap,
            self.address,
            "6.99849948@META",
        )

        # Try and swap above new limit
        assert_raises_rpc_error(
            -32600,
            "Swap amount exceeds 10% of average pool liquidity limit. Available amount to swap: 16.00000000@DUSD",
            self.nodes[0].futureswap,
            self.address,
            "16.00000001@DUSD",
            "META",
        )

        # Swap the max limit
        self.nodes[0].futureswap(self.address, "10.00000000@DUSD", "META")
        self.nodes[0].futureswap(self.address, "6.00000000@DUSD", "META")
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
        assert_equal(self.nodes[0].listloantokenliquidity(), [])

    def test_longer_fs_limit_period(self):

        # Roll back
        self.rollback_to(self.start_block)

        # Disable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/dfip2203/active": "false"}})
        self.nodes[0].generate(1)

        # Set Future Swap Gov vars
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2203/start_block": "150",
                    "v0/params/dfip2203/reward_pct": "0.05",
                    "v0/params/dfip2203/block_period": "720",
                }
            }
        )
        self.nodes[0].generate(1)

        # Enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/dfip2203/active": "true"}})
        self.nodes[0].generate(1)

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Set future swap limitaiton
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2211f/active": "true",
                    "v0/params/dfip2211f/liquidity_calc_sampling_period": "30",
                    "v0/params/dfip2211f/average_liquidity_percentage": "0.1",
                    "v0/params/dfip2211f/block_period": "2880",
                }
            }
        )
        self.nodes[0].generate(1)

        # Move forward the block period
        self.nodes[0].generate(2880)

        # Check minimum liquidity
        assert_equal(
            self.nodes[0].listloantokenliquidity(),
            [
                {
                    "META-DUSD": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
                {
                    "DUSD-META": {
                        "liquidity": "100.00000000",
                        "limit": "10.00000000",
                        "remaining": "10.00000000",
                    }
                },
            ],
        )


if __name__ == "__main__":
    FutureSwapLimitationTest().main()
