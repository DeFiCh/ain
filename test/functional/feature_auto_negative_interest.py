#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test automatic negative interest"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
import time
from decimal import Decimal


class AutoNegativeInterestTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanningmuseumheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanninggreatworldheight=1",
                "-fortcanningepilogueheight=1",
                "-grandcentralheight=1",
                "-df23height=150",
                "-jellyfish_regtest=1",
            ]
        ]

    def run_test(self):
        self.setup()
        self.auto_negative_interest()

    def setup(self):
        # Generate chain
        self.nodes[0].generate(101)

        # Store variables
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Set up environment
        self.create_oracles()
        self.create_tokens()
        self.create_pool_pairs()
        self.setup_negative_interest_env()

    def create_tokens(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, "LOAN1")
        self.nodes[0].generate(1)

        # Create collateral token
        self.nodes[0].setcollateraltoken(
            {
                "token": self.idDFI,
                "factor": 1,
                "fixedIntervalPriceId": f"{self.symbolDFI}/USD",
            }
        )
        self.nodes[0].generate(1)

        # Create loan token
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolDUSD,
                "name": self.symbolDUSD,
                "fixedIntervalPriceId": f"{self.symbolDUSD}/USD",
                "mintable": True,
                "interest": -1,
            }
        )
        self.nodes[0].generate(1)

        # Store DUSD symbol
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint tokens
        self.nodes[0].minttokens(f"10000@{self.symbolDUSD}")
        self.nodes[0].generate(1)

        # Fund address
        self.nodes[0].utxostoaccount({self.address: f"10000@{self.symbolDFI}"})
        self.nodes[0].generate(1)

    def create_oracles(self):
        # Appoint oracles
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolDUSD},
        ]
        oracle_id = self.nodes[0].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[0].generate(1)

        # Set Oracle data
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
        ]
        self.nodes[0].setoracledata(oracle_id, int(time.time()), oracle_prices)
        self.nodes[0].generate(11)

    def create_pool_pairs(self):
        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolDFI,
                "tokenB": self.symbolDUSD,
                "commission": 0,
                "status": True,
                "ownerAddress": self.address,
                "pairSymbol": f"{self.symbolDFI}-{self.symbolDUSD}",
            },
            [],
        )
        self.nodes[0].generate(1)

        # Store pool ID
        self.idDD = list(
            self.nodes[0].gettoken(f"{self.symbolDFI}-{self.symbolDUSD}").keys()
        )[0]

        # Add liquidity
        self.nodes[0].addpoolliquidity(
            {self.address: [f"100@{self.symbolDFI}", f"100@{self.symbolDUSD}"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def setup_negative_interest_env(self):
        # Set 100% DUSD fee on in only
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/poolpairs/{self.idDD}/token_b_fee_pct": "1",
                    f"v0/poolpairs/{self.idDD}/token_b_fee_direction": "in",
                }
            }
        )
        self.nodes[0].generate(1)

        # Swap DUSD to DFI, will burn all DUSD.
        self.nodes[0].poolswap(
            {
                "from": self.address,
                "tokenFrom": self.idDUSD,
                "amountFrom": 10,
                "to": self.address,
                "tokenTo": self.idDFI,
            }
        )
        self.nodes[0].generate(1)

        # Create vault
        vault = self.nodes[0].createvault(self.address, "LOAN1")
        self.nodes[0].generate(1)

        # Deposit to vault
        self.nodes[0].deposittovault(vault, self.address, f"450@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({"vaultId": vault, "amounts": f"300@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

    def auto_negative_interest(self):
        # Check estimated negative interest
        result = self.nodes[0].estimatenegativeinterest()
        assert_equal(result["dusdBurned"], Decimal("10.00000000"))
        assert_equal(result["dusdLoaned"], Decimal("300.00000000"))
        assert_equal(result["negativeInterest"], Decimal("-20.26666600"))

        # Check auto negative interest cannot be set before fork
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/feature/auto-negative-interest": "true"}},
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/negative_interest/automatic/block_period": "10"}},
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/negative_interest/automatic/burn_time_period": "86400"
                }
            },
        )

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Set auto negative interest
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/auto-negative-interest": "true",
                    "v0/negative_interest/automatic/block_period": "10",
                    "v0/negative_interest/automatic/burn_time_period": "86400",
                }
            }
        )
        self.nodes[0].generate(1)

        # Check auto negative interest is set
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/params/feature/auto-negative-interest"], "true")
        assert_equal(attributes["v0/negative_interest/automatic/block_period"], "10")
        assert_equal(
            attributes["v0/negative_interest/automatic/burn_time_period"], "86400"
        )

        # Check negative interest has not been calculated yet
        assert_equal(attributes[f"v0/token/{self.idDUSD}/loan_minting_interest"], "-1")

        # Move to automatic setting of negative interest
        self.nodes[0].generate(10)

        # Check negative interest has been calculated
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes[f"v0/token/{self.idDUSD}/loan_minting_interest"], "-20.266666"
        )


if __name__ == "__main__":
    AutoNegativeInterestTest().main()
