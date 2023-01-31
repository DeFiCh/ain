#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test verifies no checkpoint overlap"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    connect_nodes_bi,
    assert_equal
)

class CheckpointTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']
        ]

    def run_test(self):
        self.nodes[0].generate(100)
        self.sync_blocks()

        # Stop node #1 for future revert
        self.stop_node(1)

        self.nodes[0].generate(1)

        addr = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].utxostoaccount({addr: "10@0"})
        self.nodes[0].generate(1)

        height = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(height)
        # generate one block, so checkpoint is not current one
        self.nodes[0].generate(1)
        self.nodes[0].setmockcheckpoint(height, blockhash)
        self.nodes[0].generate(1)
        headblock = self.nodes[0].getblockcount()

        # REVERTING:
        #========================
        self.start_node(1)
        self.nodes[1].generate(10)

        connect_nodes_bi(self.nodes, 0, 1)
        # reverting prior last checkpoint is forbidden
        assert_equal(self.nodes[0].getblockcount(), headblock)

if __name__ == '__main__':
    CheckpointTest ().main ()
