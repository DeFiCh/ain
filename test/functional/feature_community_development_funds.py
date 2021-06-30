#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test CommunityDevelopmentFunds behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

from decimal import Decimal

class CommunityDevelopmentFunds(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-fortcanningheight=201', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-fortcanningheight=201', '-subsidytest=1'],
        ]

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        node0.generate(200)
        self.sync_all()

        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], Decimal('0'))
        node1.importprivkey('cMv1JaaZ9Mbb3M3oNmcFvko8p7EcHJ8XD7RCQjzNaMs7BWRVZTyR')
        foundationBalance = node1.getbalances()['mine']['trusted'] - Decimal("0.0010000")
        node1.utxostoaccount({'2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS': str(foundationBalance) + "@0"})
        node1.generate(1)
        self.sync_all()
        # foundation coins are locked
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], foundationBalance + Decimal('19.88746400'))
        txid = node0.sendtoaddress('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS', 5)
        node0.generate(1)
        self.sync_all()
        history = node1.listburnhistory()
        assert_equal(len(history), 1)
        assert_equal(history[0]['owner'], '2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS')
        assert_equal(history[0]['blockHeight'], 202)
        assert_equal(history[0]['txid'], txid)
        assert_equal(history[0]['amounts'], ['5.00000000@DFI'])
        # foundation coins received after fortcanningheight are burnt
        foundationBalance = node1.getbalances()['mine']['trusted'] - Decimal("0.0010000")
        assert_raises_rpc_error(-26, "burnt-output", node1.utxostoaccount, {'2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS': str(foundationBalance) + "@0"})

if __name__ == '__main__':
    CommunityDevelopmentFunds().main ()
