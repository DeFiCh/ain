#!/usr/bin/env python3
# Copyright (c) 2021 The DeFi Blockchain developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)
import time


class TestMasternodeOperator(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node0_keys = self.nodes[0].get_genesis_keys()
        node1_keys = self.nodes[1].get_genesis_keys()

        # Import private key
        self.nodes[0].importprivkey(node1_keys.operatorPrivKey)

        operators = [node0_keys.operatorAuthAddress, node1_keys.operatorAuthAddress]

        # Get start height
        start_height = self.nodes[0].getblockcount() + 1

        # Test single minter masternode
        self.restart_node(0, ["-gen", "-masternode_operator=" + operators[0]])

        # Wait to allow -gen to create some blocks
        time.sleep(1)

        # Test single minter masternode
        self.restart_node(0)

        # Get end height
        end_height = self.nodes[0].getblockcount() + 1

        # Make sure block was minted
        assert end_height > start_height

        minters = set()
        for x in range(start_height, end_height):
            blockhash = self.nodes[0].getblockhash(x)
            minters.add(self.nodes[0].getblock(blockhash)["minter"])

        # Only one minter should be present
        assert_equal(len(minters), 1)

        # Get start height
        start_height = end_height

        # Test multiple minter masternode
        self.restart_node(
            0,
            ["-gen"] + ["-masternode_operator=" + x for x in operators],
        )

        # Wait to allow -gen to create some blocks
        time.sleep(3)

        # Get end height
        end_height = self.nodes[0].getblockcount() + 1

        # Make sure block was minted
        assert end_height > start_height

        minters.clear()
        for x in range(start_height, end_height):
            blockhash = self.nodes[0].getblockhash(x)
            minters.add(self.nodes[0].getblock(blockhash)["minter"])

        # Both minters should be present
        assert_equal(len(minters), 2)


if __name__ == "__main__":
    TestMasternodeOperator().main()
