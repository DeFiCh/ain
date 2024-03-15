#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token fractional split"""

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
                "-vaultindex=1",
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
                "-df23height=250",
                "-subsidytest=1",
            ]
        ]

    def run_test(self):

        # Setup test
        self.setup_tests()

        # Test token 5-to-2 merge
        self.token_split(-2.5)

        # Test token 2-to-5 split
        self.token_split(2.5)

        # Test token 4-to-3 merge
        self.token_split(-1.33333333)

        # Test token 3-to-4 split
        self.token_split(1.33333333)

    def setup_tests(self):

        # Set up test tokens
        self.setup_test_tokens()

        # Set up and check Gov vars
        self.setup_and_check_govvars()

        # Set up pool pair
        self.setup_poolpair()

        # Save block height
        self.block_height = self.nodes[0].getblockcount()

    def satoshi_limit(self, amount):
        return amount.quantize(Decimal("0.00000001"), rounding=ROUND_DOWN)

    def setup_poolpair(self):

        # Fund address for pool
        self.nodes[0].utxostoaccount({self.address: f"1000@{self.symbolDFI}"})
        self.nodes[0].minttokens([f"1000@{self.idTSLA}"])
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolTSLA,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.address,
                "symbol": self.symbolTSLA + "-" + self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.address: [f"1000@{self.symbolTSLA}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolTSLA = "TSLA"
        self.symbolDFI = "DFI"

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolTSLA},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(11)

        # Create tokens
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolTSLA,
                "name": self.symbolTSLA,
                "fixedIntervalPriceId": f"{self.symbolTSLA}/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken(
            {
                "token": self.symbolTSLA,
                "factor": 1,
                "fixedIntervalPriceId": f"{self.symbolTSLA}/USD",
            }
        )
        self.nodes[0].generate(1)

        # Store token IDs
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Enable mint tokens to address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/feature/mint-tokens-to-address": "true"}}
        )
        self.nodes[0].generate(1)

        # Create funded addresses
        self.funded_addresses = []
        for i in range(100):
            address = self.nodes[0].getnewaddress()
            amount = 10 + (i * 10)
            self.nodes[0].minttokens([f"{str(amount)}@{self.idTSLA}"], [], address)
            self.nodes[0].generate(1)
            self.funded_addresses.append([address, Decimal(str(amount))])

    def setup_and_check_govvars(self):
        # Try and enable fractional split before the fork
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/oracles/splits/fractional_enabled": "true"}},
        )

        # Move to fork
        self.nodes[0].generate(250 - self.nodes[0].getblockcount())

        # Try and create a fractional split before the fork
        assert_raises_rpc_error(
            -32600,
            "Fractional split not currently supported",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}": f"{self.idTSLA}/-2.5"
                }
            },
        )

        # Enable fractional split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/oracles/splits/fractional_enabled": "true"}}
        )
        self.nodes[0].generate(1)

        # Try and create a fractional split of less than 1
        assert_raises_rpc_error(
            -32600,
            "Fractional split cannot be less than 1",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}": f"{self.idTSLA}/-0.99999999"
                }
            },
        )

    def token_split(self, multiplier):

        # Rollback to start
        self.rollback_to(self.block_height)

        # Get token ID
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Set expected minted amount
        if multiplier < 0:
            minted = self.nodes[0].gettoken(self.idTSLA)[self.idTSLA][
                "minted"
            ] / Decimal(str(abs(multiplier)))
        else:
            minted = self.nodes[0].gettoken(self.idTSLA)[self.idTSLA][
                "minted"
            ] * Decimal(str(multiplier))

        # Token split
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}": f"{self.idTSLA}/{multiplier}"
                }
            }
        )
        self.nodes[0].generate(2)

        # Calculate pool pair reserve
        reserve_a = Decimal("1000.00000000")
        if multiplier < 0:
            reserve_a = self.satoshi_limit(reserve_a / Decimal(str(abs(multiplier))))
        else:
            reserve_a = reserve_a * Decimal(str(multiplier))

        # Check balances are equal
        assert_equal(self.nodes[0].listpoolpairs()["4"]["reserveA"], reserve_a)

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Check new token amount to whole DFI as Sats will be lost due to rounding
        result = self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]
        assert_equal(str(result["minted"]).split(".")[0], str(minted).split(".")[0])

        # Check new balances and history
        for [address, amount] in self.funded_addresses:
            account = self.nodes[0].getaccount(address)
            new_amount = account[0].split("@")[0]

            if multiplier < 0:
                split_amount = self.satoshi_limit(
                    amount / Decimal(str(abs(multiplier)))
                )
            else:
                split_amount = self.satoshi_limit(amount * Decimal(str(multiplier)))

            assert_equal(new_amount, f"{Decimal(split_amount):.8f}")
            history = self.nodes[0].listaccounthistory(
                address, {"txtype": "TokenSplit"}
            )
            assert_equal(len(history), 2)
            assert_equal(
                history[0]["amounts"][0], f"{-amount:.8f}" + f"@{self.symbolTSLA}/v1"
            )
            assert_equal(history[1]["amounts"][0], account[0])


if __name__ == "__main__":
    TokenFractionalSplitTest().main()
