#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test consolidate rewards"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, connect_nodes_bi

import time


class ConsolidateRewardsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.df24height = 200
        self.args = [
            "-txnotokens=0",
            "-subsidytest=1",
            "-regtest-minttoken-simulate-mainnet=1",
            "-amkheight=1",
            "-bayfrontheight=1",
            "-bayfrontmarinaheight=1",
            "-bayfrontgardensheight=1",
            "-clarkequayheight=1",
            "-dakotaheight=1",
            "-dakotacrescentheight=1",
            "-eunosheight=1",
            "-eunospayaheight=1",
            "-fortcanningheight=1",
            "-fortcanningmuseumheight=1",
            "-fortcanningparkheight=1",
            "-fortcanninghillheight=1",
            "-fortcanningroadheight=1",
            "-fortcanningcrunchheight=1",
            "-fortcanningspringheight=1",
            "-fortcanninggreatworldheight=1",
            "-grandcentralheight=1",
            "-grandcentralepilogueheight=1",
            "-metachainheight=105",
            "-df23height=110",
            f"-df24height={self.df24height}",
        ]
        self.extra_args = [self.args, self.args]

    def run_test(self):

        # Set up
        self.setup()

        # Consolidate before fork
        self.pre_fork24_consolidate()

        # Consolidate after fork
        self.post_fork24_consolidate()

    def setup(self):

        # Generate chain
        self.nodes[0].generate(110)

        # Symbols
        self.symbolDFI = "DFI"
        self.symbolGOOGL = "GOOGL"
        self.symbolDUSD = "DUSD"
        self.symbolGD = "GOOGL-DUSD"

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolGOOGL},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolGOOGL}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set loan tokens
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolGOOGL,
                "name": self.symbolGOOGL,
                "fixedIntervalPriceId": f"{self.symbolGOOGL}/USD",
                "isDAT": True,
                "interest": 0,
            }
        )
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolDUSD,
                "name": self.symbolDUSD,
                "fixedIntervalPriceId": f"{self.symbolDUSD}/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolGOOGL,
                "tokenB": self.symbolDUSD,
                "commission": 0.001,
                "status": True,
                "ownerAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "symbol": self.symbolGD,
            }
        )
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idGD = list(self.nodes[0].gettoken(self.symbolGD).keys())[0]

        # Create new address
        self.address = self.nodes[0].getnewaddress("", "legacy")

        # Mint tokens to address
        self.nodes[0].minttokens([f"100000@{self.idDUSD}", f"100000@{self.idGOOGL}"])
        self.nodes[0].generate(1)

        # Set loan token split
        self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": {self.idGD: 1}})
        self.nodes[0].generate(1)

        # Fund pools
        self.nodes[0].addpoolliquidity(
            {
                self.nodes[0]
                .get_genesis_keys()
                .ownerAuthAddress: [
                    f"100000@{self.symbolDUSD}",
                    f"100000@{self.symbolGOOGL}",
                ]
            },
            self.address,
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

    def pre_fork24_consolidate(self):

        # Compare hash before consolidation
        hash_0 = self.nodes[0].logdbhashes()["dvmhash"]
        hash_1 = self.nodes[1].logdbhashes()["dvmhash"]
        assert_equal(hash_0, hash_1)

        # Generate rewards
        self.nodes[0].generate(10)
        self.sync_blocks()

        # Stop node
        self.stop_node(1)

        # Start node with consolidation
        self.args.append(f"-consolidaterewards={self.symbolGD}")
        self.start_node(1, self.args)
        connect_nodes_bi(self.nodes, 0, 1)

        # Split Google
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{self.nodes[0].getblockcount() + 2}": f"{self.idGOOGL}/2"
                }
            }
        )
        self.nodes[0].generate(2)
        self.sync_blocks()

        # Update ID
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Compare hash before consolidation
        hash_0 = self.nodes[0].logdbhashes()["dvmhash"]
        hash_1 = self.nodes[1].logdbhashes()["dvmhash"]
        assert_equal(hash_0, hash_1)

    def post_fork24_consolidate(self):

        # Move to fork
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Generate post fork rewards
        self.nodes[0].generate(10)
        self.sync_blocks()

        # Compare hash before consolidation
        hash_0 = self.nodes[0].logdbhashes()["dvmhash"]
        hash_1 = self.nodes[1].logdbhashes()["dvmhash"]
        assert_equal(hash_0, hash_1)

        # Stop node
        self.stop_node(1)

        # Start node with consolidation
        self.args.append(f"-consolidaterewards={self.symbolGD}")
        self.start_node(1, self.args)
        connect_nodes_bi(self.nodes, 0, 1)

        # Split Google
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{self.nodes[0].getblockcount() + 2}": f"{self.idGOOGL}/2"
                }
            }
        )
        self.nodes[0].generate(2)
        self.sync_blocks()

        # Compare hash before consolidation
        hash_0 = self.nodes[0].logdbhashes()["dvmhash"]
        hash_1 = self.nodes[1].logdbhashes()["dvmhash"]
        assert_equal(hash_0, hash_1)


if __name__ == "__main__":
    ConsolidateRewardsTest().main()
