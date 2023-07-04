#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test.

"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import connect_nodes_bi

class EmptyTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [[]]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_blocks()

    def run_test(self):
        assert (self.nodes[0].getbalance() == 0)
        self.nodes[0].generate(1)
        self.sync_blocks()

if __name__ == '__main__':
    EmptyTest().main()
