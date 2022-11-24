#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test sending funds to metachain"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal


class MetachainSendFundsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-dmcgenesisheight=101'],
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-dmcgenesisheight=101'],
        ]

    def run_test(self):
        node = self.nodes[0]
        node1 = self.nodes[1]
        node.generate(101)
        self.sync_blocks()

        assert_equal(node.getblockcount(), 101)

        # Get addresses and set up account
        account = node.getnewaddress()
        node.utxostoaccount({account: "10@0"})
        node.generate(1)
        self.sync_blocks()

        # Check we have expected balance
        assert_equal(node1.getaccount(account)[0], "10.00000000@DFI")

        node.accounttometachain(account, "000000000000000000000000000000000000dead", "3.14")

        # Store block height
        blockcount = node.getblockcount()

        # Generate a block
        node.generate(1)
        self.sync_blocks()

        # Check the blockchain has extended as expected
        assert_equal(node1.getblockcount(), blockcount + 1)


if __name__ == '__main__':
    MetachainSendFundsTest().main()
