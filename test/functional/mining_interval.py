#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test mining interval."""

from test_framework.test_framework import DefiTestFramework

class TestMiningInterval(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-eunospayaheight=1', '-greatworldheight=1']]

    def run_test(self):
        block_interval = 15

        self.nodes[0].generate(101)
        self.sync_blocks()

        for x in range(1, self.nodes[0].getblockcount() + 1):
            assert(self.nodes[0].getblock(self.nodes[0].getblockhash(x))['time'] % block_interval == 0)

if __name__ == '__main__':
    TestMiningInterval().main()
