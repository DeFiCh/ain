#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - DUSD as collateral."""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time


class Token128Test(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txordering=2",
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=50",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                # "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
            [
                "-txordering=2",
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=50",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def test_set_collateral_token_128_before_nextnetworkupgradeheight(self):
        node = self.nodes[0]
        node.generate(120)
        address = node.get_genesis_keys().ownerAuthAddress

        node.createtoken(
            {
                "symbol": "FISH",
                "isDAT": False,
                "collateralAddress": address,
            }
        )
        node.generate(1)

        oracle_address = node.getnewaddress("", "legacy")
        price_feed = [
            {"token": "FISH#128", "currency": "USD"},
        ]

        oracle = node.appointoracle(oracle_address, price_feed, 10)
        node.generate(1)

        oracle_prices = [
            {"tokenAmount": "1@FISH#128", "currency": "USD"},
        ]
        node.setoracledata(oracle, int(time.time()), oracle_prices)
        node.generate(1)

        node.setcollateraltoken(
            {"token": "FISH#128", "factor": 1, "fixedIntervalPriceId": "FISH#128/USD"}
        )
        node.generate(1)

    def test_set_collateral_token_128_nextnetworkupgradeheight(self):
        node = self.nodes[1]
        node.generate(120)
        address = node.get_genesis_keys().ownerAuthAddress

        node.createtoken(
            {
                "symbol": "FISH",
                "isDAT": False,
                "collateralAddress": address,
            }
        )
        node.generate(1)

        oracle_address = node.getnewaddress("", "legacy")
        price_feed = [
            {"token": "FISH#128", "currency": "USD"},
        ]

        oracle = node.appointoracle(oracle_address, price_feed, 10)
        node.generate(1)

        oracle_prices = [
            {"tokenAmount": "1@FISH#128", "currency": "USD"},
        ]
        node.setoracledata(oracle, int(time.time()), oracle_prices)
        node.generate(1)

        node.setcollateraltoken(
            {"token": "FISH#128", "factor": 1, "fixedIntervalPriceId": "FISH#128/USD"}
        )
        node.generate(1)

    def run_test(self):
        self.test_set_collateral_token_128_before_nextnetworkupgradeheight()

        self.test_set_collateral_token_128_nextnetworkupgradeheight()


if __name__ == "__main__":
    Token128Test().main()
