#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify anchor auths pruning
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import time


class AnchorsAuthsPruningTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [
            ["-dummypos=1", "-spv=1", '-amkheight=0', "-dakotaheight=1", "-fortcanningheight=1"],
        ]
        self.setup_clean_chain = True

    def genmocktime(self, time, intervals):
        for i in range(intervals):
            self.nodes[0].set_mocktime(time + (i * 60 * 60))
            self.nodes[0].generate(5)

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)

        self.genmocktime(int(time.time()), 12)

        assert_equal(len(self.nodes[0].spv_listanchors()), 0)

        # Checking starting set
        assert_equal(len(self.nodes[0].spv_listanchorauths()), 2)  # 15,30

        # Setting anchor
        self.nodes[0].spv_setlastheight(1)
        txinfo = self.nodes[0].spv_createanchor([{
            'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_equal(txinfo['defiHash'], self.nodes[0].getblockhash(30))
        assert_equal(txinfo['defiHeight'], 30)

        # Still the same
        assert_equal(len(self.nodes[0].spv_listanchorauths()), 2)  # 15,30
        self.genmocktime(int(time.time() + (12 * 60 * 60)), 6)

        # Couple of auths added
        assert_equal(len(self.nodes[0].spv_listanchorauths()), 4)  # + 45,60

        # Nothing should change
        self.nodes[0].spv_setlastheight(5)
        assert_equal(len(self.nodes[0].spv_listanchorauths()), 4)  # 15,30,45,60

        # Pruning should occur
        self.nodes[0].spv_setlastheight(6)
        auths = self.nodes[0].spv_listanchorauths()  # 45,60 only
        assert_equal(len(auths), 2)
        assert_equal(auths[0]['blockHeight'], 60)
        assert_equal(auths[1]['blockHeight'], 45)


if __name__ == '__main__':
    AnchorsAuthsPruningTest().main()
