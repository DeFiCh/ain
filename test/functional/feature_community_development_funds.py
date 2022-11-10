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
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=60', '-eunosheight=70', '-fortcanningheight=80', '-fortcanninghillheight=90', '-fortcanningroadheight=100', '-fortcanningcrunchheight=110', '-fortcanningspringheight=120', '-fortcanninggreatworldheight=130', '-grandcentralheight=201', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=60', '-eunosheight=70', '-fortcanningheight=80', '-fortcanninghillheight=90', '-fortcanningroadheight=100', '-fortcanningcrunchheight=110', '-fortcanningspringheight=120', '-fortcanninggreatworldheight=130', '-grandcentralheight=201', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=60', '-eunosheight=70', '-fortcanningheight=80', '-fortcanninghillheight=90', '-fortcanningroadheight=100', '-fortcanningcrunchheight=110', '-fortcanningspringheight=120', '-fortcanninggreatworldheight=130', '-grandcentralheight=201', '-subsidytest=1'],
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
        balanceLessFee = foundationBalance - Decimal("0.00092720")
        node1.utxostoaccount({'2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS': str(balanceLessFee) + "@0"})
        assert_equal(node1.getbalances()['mine']['trusted'], 0)

        self.sync_mempools()
        node0.generate(1)
        self.sync_all()
        self.stop_node(2)

        assert_equal(node1.getbalances()['mine']['trusted'], Decimal("19.887464"))
        assert_equal(node1.getbalances()['mine']['immature'], foundation['mine']['immature'])

        # GrandCnetral hardfork height
        node0.generate(1)
        self.sync_all(self.nodes[:2])

        assert_equal(node1.getbalances()['mine']['trusted'], 2 * Decimal("19.887464"))
        assert_equal(node1.getbalances()['mine']['immature'], foundation['mine']['immature'] - Decimal("19.887464"))

        # check that funds are not trasfered yet before governance is activated
        assert_equal(Decimal(node1.getaccount('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS')[0].split('@')[0]), balanceLessFee + Decimal("19.887464"))
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], 0)

        # activate on-chain governance
        node0.setgov({"ATTRIBUTES":{'v0/params/feature/gov':'true'}})
        node0.generate(1)
        self.sync_all(self.nodes[:2])

        # check that funds are not trasfered yet before governance is activated
        assert_equal(node1.getaccount('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS'), [])
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + 2*Decimal("19.887464"))

        foundation1 = node1.getbalances()
        foundationBalance1 = foundation1['mine']['trusted']
        after_hardfork = foundationBalance1 + foundation1['mine']['immature']

        # no change in total sum, after
        assert_equal(before_hardfork + Decimal("19.887464"), after_hardfork + foundationBalance)

        # activate on-chain governance
        node0.setgov({"ATTRIBUTES":{'v0/params/feature/gov':'false'}})
        node0.generate(1)
        self.sync_all(self.nodes[:2])

        # check that funds are not trasfered yet before governance is activated
        assert_equal(Decimal(node1.getaccount('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS')[0].split('@')[0]), balanceLessFee + 3*Decimal("19.887464"))
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], 0)

        # foundation coins are locked
        node0.generate(2)
        self.sync_all(self.nodes[:2])

        print ("Reverting...")
        self.start_node(2)

        # GrandCnetral hardfork height
        node2.generate(1)

        node2.generate(5)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], 0)
        assert_equal(node1.listcommunitybalances()['CommunityDevelopmentFunds'], 0)
        assert_equal(node2.listcommunitybalances()['CommunityDevelopmentFunds'], 0)

        node0.setgov({"ATTRIBUTES":{'v0/params/feature/gov':'true'}})
        node0.generate(1)
        self.sync_blocks()

        assert_equal(node1.getaccount('2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS'), [])
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal('{:.8f}'.format(7 * 19.887464)))
        assert_equal(node1.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal('{:.8f}'.format(7 * 19.887464)))
        assert_equal(node2.listcommunitybalances()['CommunityDevelopmentFunds'], balanceLessFee + Decimal('{:.8f}'.format(7 * 19.887464)))

        node0.generate(93)
        self.sync_blocks()

        foundation = node1.getbalances()
        assert_equal(foundation['mine']['trusted'], Decimal('2008.63386400'))

        node0.generate(1)
        self.sync_blocks()

        foundation = node1.getbalances()
        assert_equal(foundation['mine']['trusted'], Decimal('2008.633864000'))

if __name__ == '__main__':
    CommunityDevelopmentFunds().main ()
