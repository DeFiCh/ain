#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import os

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        ain = os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
        )
        genesis = os.path.join(ain, "lib/ain-evm/genesis.json")

        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-ethstartstate={}".format(genesis),
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
            ],
        ]

    def run_test(self):
        self.nodes[0].generate(111)

        ethblock0 = self.nodes[0].eth_getBlockByNumber(0)
        assert_equal(ethblock0["difficulty"], "0x400000")
        assert_equal(ethblock0["extraData"], "0x686f727365")
        assert_equal(ethblock0["gasLimit"], "0x1388")
        assert_equal(
            ethblock0["parentHash"],
            "0x0000000000000000000000000000000000000000000000000000000000000000",
        )
        # q(canonbrother): why is the mixHash required in genesis?
        # assert_equal(ethblock0['mixHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(ethblock0["nonce"], "0x123123123123123f")
        assert_equal(ethblock0["timestamp"], "0x539")

        balance = self.nodes[0].eth_getBalance(
            "a94f5374fce5edbc8e2a8697c15331677e6ebf0b"
        )
        assert_equal(balance, "0x9184e72a000")


if __name__ == "__main__":
    EVMTest().main()
