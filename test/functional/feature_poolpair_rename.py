#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test pool pair rename"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error


class PoolPairRenameTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ["-txnotokens=0", "-amkheight=1", "-bayfrontheight=1", "-df23height=110"],
        ]

    def run_test(self):
        # Set up
        self.setup()

        # Rename token
        self.rename_token()

        # Rename pool
        self.rename_pool()

    def setup(self):
        # Generate chain
        self.nodes[0].generate(105)
        self.sync_blocks()

        # Pool owner address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Create tokens
        self.nodes[0].createtoken(
            {
                "symbol": "FB",
                "name": "dFB",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].createtoken(
            {
                "symbol": "DUSD",
                "name": "Decentralized USD",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": "FB",
                "tokenB": "DUSD",
                "comission": 0.001,
                "status": True,
                "ownerAddress": self.address,
            },
        )
        self.nodes[0].generate(1)

    def rename_token(self):
        # Rename FB token
        self.nodes[0].updatetoken("FB", {"name": "META", "symbol": "dMETA"})
        self.nodes[0].generate(1)

        # Check rename
        result = self.nodes[0].gettoken("dMETA")["1"]
        assert_equal(result["name"], "META")
        assert_equal(result["symbol"], "dMETA")

    def rename_pool(self):
        # Rename pool before height
        assert_raises_rpc_error(
            -32600,
            "Poolpair symbol cannot be changed below DF23 height",
            self.nodes[0].updatepoolpair,
            {"pool": "FB-DUSD", "name": "dMETA-Decentralized USD"},
        )
        assert_raises_rpc_error(
            -32600,
            "Poolpair symbol cannot be changed below DF23 height",
            self.nodes[0].updatepoolpair,
            {"pool": "FB-DUSD", "symbol": "META-DUSD"},
        )

        # Move to fork height
        self.nodes[0].generate(2)

        # Rename pool
        self.nodes[0].updatepoolpair(
            {
                "pool": "FB-DUSD",
                "name": "dMETA-Decentralized USD",
                "symbol": "META-DUSD",
            }
        )
        self.nodes[0].generate(1)

        # Check new names
        result = self.nodes[0].gettoken("META-DUSD")["3"]
        assert_equal(result["symbol"], "META-DUSD")
        assert_equal(result["name"], "dMETA-Decentralized USD")
        result = self.nodes[0].getpoolpair("META-DUSD")["3"]
        assert_equal(result["symbol"], "META-DUSD")
        assert_equal(result["name"], "dMETA-Decentralized USD")


if __name__ == "__main__":
    PoolPairRenameTest().main()
