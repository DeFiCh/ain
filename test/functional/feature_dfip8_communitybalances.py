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
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-eunosheight=1', '-subsidytest=1']]

    def run_test(self):
        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('0.08100800'))
        assert_equal(result['Burnt'], Decimal('250.07169600'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.08100800'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('250.07169600'))

        self.nodes[0].generate(9)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('0.81008000'))
        assert_equal(result['Burnt'], Decimal('2500.71696000'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.08100800'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('250.07169600'))

        # First reduction - 150 + 1 Eunos height on regtest
        self.nodes[0].generate(141)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.23086488'))
        assert_equal(result['Burnt'], Decimal('37756.67990726'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('245.92550726'))

        # First reduction plus one
        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.31052976'))
        assert_equal(result['Burnt'], Decimal('38002.60541452'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('245.92550726'))

        # Invalidate a block and test rollback
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.23086488'))
        assert_equal(result['Burnt'], Decimal('37756.67990726'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('245.92550726'))

        #Go forward again to first reduction
        self.nodes[0].generate(1)

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('12.31052976'))
        assert_equal(result['Burnt'], Decimal('38002.60541452'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.07966488'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('245.92550726'))

        # Ten reductions plus one
        self.nodes[0].generate(1502 - self.nodes[0].getblockcount())

        result = self.nodes[0].listcommunitybalances()

        assert_equal(result['AnchorReward'], Decimal('112.97249134'))
        assert_equal(result['Burnt'], Decimal('348746.10120472'))

        getblock = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        assert_equal(getblock['nonutxo'][0]['AnchorReward'], Decimal('0.06853592'))
        assert_equal(getblock['nonutxo'][0]['Burnt'], Decimal('211.57039811'))

if __name__ == '__main__':
    Dfip8Test().main()
