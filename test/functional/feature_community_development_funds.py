#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test CommunityDevelopmentFunds behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi
)

from decimal import Decimal

class CommunityDevelopmentFunds(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=201', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=201', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=201', '-subsidytest=1'],
        ]

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]

        node0.generate(199)
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], Decimal('0'))
        node1.importprivkey('cMv1JaaZ9Mbb3M3oNmcFvko8p7EcHJ8XD7RCQjzNaMs7BWRVZTyR')
        foundation = node1.getbalances()
        foundationBalance = foundation['mine']['trusted']
        before_hardfork = foundationBalance + foundation['mine']['immature']
        balanceLessFee = foundationBalance - Decimal("0.0010000")
        node1.utxostoaccount({'2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS': str(balanceLessFee) + "@0"})
        self.sync_mempools()
        node0.generate(1)
        self.sync_all()
        self.stop_node(2)

        node1.generate(1)
        self.sync_all(self.nodes[:2])
        foundationBalance = foundation['mine']['trusted']
        after_hardfork = foundationBalance + foundation['mine']['immature']

        # no change
        assert_equal(before_hardfork, after_hardfork)

        # foundation coins are locked
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal("19.887464"))
        node0.generate(1)
        self.sync_all(self.nodes[:2])

        print ("Reverting...")
        self.start_node(2)
        node2.generate(3)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        assert_equal(node1.getaccount('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS'), [])
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal('{:.8f}'.format(3 * 19.887464)))
        assert_equal(node2.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal('{:.8f}'.format(3 * 19.887464)))

if __name__ == '__main__':
    CommunityDevelopmentFunds().main ()
