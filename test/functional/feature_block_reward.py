#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test account mining behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_greater_than,
    assert_equal
)

class BlockRewardTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-eunosheight=100', '-eunosheight=100', '-fortcanningheight=110', '-subsidytest=1']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(120)

        newBaseBlockSubsidy = 405.0400
        masternodePortion = 0.3333 # 33.33%
        mingBaseReward = newBaseBlockSubsidy * masternodePortion

        result = node.listaccounthistory("mine", {"depth":0})
        assert_equal(result[0]["amounts"][0], f'{mingBaseReward:.8f}@DFI')

        account = node.getnewaddress()
        node.utxostoaccount({account: "1.1@0"})
        node.utxostoaccount({account: "1.2@0"})
        node.utxostoaccount({account: "1.3@0"})
        node.generate(1)

        result = node.listaccounthistory("mine", {"depth":0})
        for subResult in result:
            if subResult["type"] == "blockReward":
                assert_greater_than(subResult["amounts"][0], f'{mingBaseReward:.8f}@DFI')

if __name__ == '__main__':
    BlockRewardTest().main ()
