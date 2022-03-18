#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test community balances during reorgs"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, connect_nodes, disconnect_nodes

from decimal import Decimal

class CommunityBalanceReorg(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=0'], ['-txnotokens=0', '-amkheight=0']]

    def run_test(self):
        # Generate across nodes
        self.nodes[0].generate(10)
        self.sync_blocks()

        # Disconnect
        disconnect_nodes(self.nodes[0], 1)

        self.nodes[0].generate(20)

        assert_equal(self.nodes[0].getblockcount(), 30)
        assert_equal(self.nodes[1].getblockcount(), 10)

        # Check rewards
        assert_equal(self.nodes[0].getblockcount(), 30)
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('3.00000000'))
        assert_equal(self.nodes[0].listcommunitybalances()['Burnt'], 300)

        # Create longer chain
        self.nodes[1].generate(21)

        # Reconnect nodes. Should switch node 0 to node 1 chain reorging blocks.
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # Make sure we are on the longer chain
        assert_equal(self.nodes[0].getblockcount(), 31)
        assert_equal(self.nodes[1].getblockcount(), 31)

        # Should be one more than before
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('3.10000000'))
        assert_equal(self.nodes[0].listcommunitybalances()['Burnt'], 310)

if __name__ == '__main__':
    CommunityBalanceReorg().main()
