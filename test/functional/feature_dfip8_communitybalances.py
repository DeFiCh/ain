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
        assert_equal(result['Burnt'], Decimal('146.98901600'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.08100800'))
        assert_equal(getblock['nonutxo'][0]['IncentiveFunding'], Decimal('103.08268000'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('146.98901600'))

        self.nodes[0].generate(9)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('0.81008000'))
        assert_equal(result['IncentiveFunding'], Decimal('1030.82680000'))
        assert_equal(result['Burnt'], Decimal('1469.89016000'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.08100800'))
        assert_equal(getblock['nonutxo'][0]['IncentiveFunding'], Decimal('103.08268000'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('146.98901600'))

        # First reduction - 150 + 1 Eunos height on regtest
        self.nodes[0].generate(141)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.23086488'))
        assert_equal(result['IncentiveFunding'], Decimal('15563.77556916'))
        assert_equal(result['Burnt'], Decimal('22192.90433810'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['IncentiveFunding'], Decimal('101.37356916'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('144.55193810'))

        # First reduction plus one
        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.31052976'))
        assert_equal(result['IncentiveFunding'], Decimal('15665.14913832'))
        assert_equal(result['Burnt'], Decimal('22337.45627620'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['IncentiveFunding'], Decimal('101.37356916'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('144.55193810'))

        # Ten reductions plus one
        self.nodes[0].generate(1502 - self.nodes[0].getblockcount())

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('112.97249134'))
        assert_equal(result['IncentiveFunding'], Decimal('143757.50365818'))
        assert_equal(result['Burnt'], Decimal('204988.59754052'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.06853592'))
        assert_equal(getblock['nonutxo'][0]['IncentiveFunding'], Decimal('87.21196359'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('124.35843451'))

if __name__ == '__main__':
    Dfip8Test().main()
