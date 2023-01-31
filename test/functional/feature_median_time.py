#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test median time change"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

import time
from random import randint

class MedianTimeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [ "-dummypos=1", '-amkheight=0', "-dakotaheight=1", "-fortcanningheight=100"],
        self.setup_clean_chain = True

    def CalcMedianTime(self):
        medianTime = 11
        times = []
        for i in range(medianTime):
            times.append(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount() - i))['time'])
        times.sort()
        if self.nodes[0].getblockcount() >= 100:
            return times[8]
        return times[int(medianTime / 2)]

    def GenerateBlocks(self, blocks):
        for _ in range(blocks):
            self.nodes[0].set_mocktime(int(time.time()) + randint(10, 60))
            self.nodes[0].generate(1)

    def run_test(self):
        self.nodes[0].generate(11)
        assert_equal(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['mediantime'], self.CalcMedianTime())

        # Test some random block times pre-fork
        self.GenerateBlocks(11)
        assert_equal(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['mediantime'], self.CalcMedianTime())

        # Move to hard fork
        self.nodes[0].generate(100 - self.nodes[0].getblockcount())
        assert_equal(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['mediantime'], self.CalcMedianTime())

        # Test some random block times post-fork
        self.GenerateBlocks(5)
        assert_equal(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['mediantime'], self.CalcMedianTime())

if __name__ == '__main__':
    MedianTimeTest().main()
