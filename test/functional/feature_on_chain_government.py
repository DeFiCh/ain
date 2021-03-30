#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test on chain government behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal
from decimal import Decimal

class ChainGornmentTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-fortcanningheight=101'],
        ]

    def run_test(self):
        node = self.nodes[0]
        node.generate(101)

        assert_equal(node.getblockcount(), 101) # fort canning

        # Get addresses
        address = node.getnewaddress()
        title = "Create test community fund request proposal"
        tx = node.createcfp({"title":title, "amount":100, "cycles":3, "payoutAddress":address})

        # Generate a block
        node.generate(1)
        results = node.listproposals()
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["type"], "CommunityFundRequest")
        assert_equal(result["status"], "Voting")
        assert_equal(result["amount"], Decimal("100"))
        assert_equal(result["cyclesPaid"], 1)
        assert_equal(result["totalCycles"], 3)
        assert_equal(result["payoutAddress"], address)
        assert_equal(result["finalizeAfter"], 3 * 350 + 102)

if __name__ == '__main__':
    ChainGornmentTest().main ()
