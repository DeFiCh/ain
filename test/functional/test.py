#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal


class DST20(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txordering=2",
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
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
            ]
        ]

    def run_test(self):
        self.node = self.nodes[0]
        self.w0 = self.node.w3

        self.node.generate(150)

        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )
        self.nodes[0].generate(2)

        # create block filter
        filter_id = self.w0.eth.filter("latest")

        for _ in range(5):
            # generate one block
            self.nodes[0].generate(1)

            # get filter
            changes = self.w0.eth.get_filter_changes(filter_id.filter_id)
            assert_equal(len(changes), 1)
            assert_equal(changes[0], self.w0.eth.get_block("latest")["hash"])

        for _ in range(5):
            # generate two blocks
            self.nodes[0].generate(2)

            # get filter
            changes = self.w0.eth.get_filter_changes(filter_id.filter_id)
            assert_equal(len(changes), 2)
            assert_equal(changes[1], self.w0.eth.get_block("latest")["hash"])


if __name__ == "__main__":
    DST20().main()
