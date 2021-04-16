#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DFIP8 community balance"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal

class Dfip8Test(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-eunosheight=1']]

    def run_test(self):

        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('0.08100800'))
        assert_equal(result['IncentiveFunding'], Decimal('103.08268000'))
        assert_equal(result['Swap'], Decimal('49.98193600'))
        assert_equal(result['Futures'], Decimal('49.98193600'))
        assert_equal(result['Options'], Decimal('40.01795200'))
        assert_equal(result['Burnt'], Decimal('7.00719200'))

        self.nodes[0].generate(9)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('0.81008000'))
        assert_equal(result['IncentiveFunding'], Decimal('1030.82680000'))
        assert_equal(result['Swap'], Decimal('499.81936000'))
        assert_equal(result['Futures'], Decimal('499.81936000'))
        assert_equal(result['Options'], Decimal('400.17952000'))
        assert_equal(result['Burnt'], Decimal('70.07192000'))

        # First reduction - 150 + 1 Eunos height on regtest
        self.nodes[0].generate(141)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.23086488'))
        assert_equal(result['IncentiveFunding'], Decimal('15563.77556916'))
        assert_equal(result['Swap'], Decimal('7546.44363550'))
        assert_equal(result['Futures'], Decimal('7546.44363550'))
        assert_equal(result['Options'], Decimal('6042.04725435'))
        assert_equal(result['Burnt'], Decimal('1057.96981275'))

        # First reduction plus one
        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.31052976'))
        assert_equal(result['IncentiveFunding'], Decimal('15665.14913832'))
        assert_equal(result['Swap'], Decimal('7595.59687100'))
        assert_equal(result['Futures'], Decimal('7595.59687100'))
        assert_equal(result['Options'], Decimal('6081.40170870'))
        assert_equal(result['Burnt'], Decimal('1064.86082550'))

        # Ten reductions plus one
        self.nodes[0].generate(1502 - self.nodes[0].getblockcount())

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('112.97249134'))
        assert_equal(result['IncentiveFunding'], Decimal('143757.50365818'))
        assert_equal(result['Swap'], Decimal('69704.03124200'))
        assert_equal(result['Futures'], Decimal('69704.03124200'))
        assert_equal(result['Options'], Decimal('55808.41399014'))
        assert_equal(result['Burnt'], Decimal('9772.12106638'))

if __name__ == '__main__':
    Dfip8Test().main()
