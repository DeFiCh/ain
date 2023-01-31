#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes timelock"""

import time

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, disconnect_nodes, connect_nodes

class MasternodesTimelockTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-dakotacrescentheight=1', '-eunosheight=1', '-eunospayaheight=140'],
                           ['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-dakotacrescentheight=1', '-eunosheight=1', '-eunospayaheight=140']]

    def run_test(self):

        self.nodes[0].generate(101)

        collateral_prefork = self.nodes[0].getnewaddress("", "legacy")
        collateral = self.nodes[0].getnewaddress("", "legacy")
        collateral5 = self.nodes[0].getnewaddress("", "legacy")
        collateral10 = self.nodes[0].getnewaddress("", "legacy")
        collateral20 = self.nodes[0].getnewaddress("", "legacy")

        # Try to set time lock before EunosPaya
        try:
            self.nodes[0].createmasternode(collateral5, "", [], "FIVEYEARTIMELOCK")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Timelock cannot be specified before EunosPaya hard fork" in errorString)

        # Create regular MN to make sure accrued multiplier moves over
        nodeid_pre = self.nodes[0].createmasternode(collateral_prefork)
        self.nodes[0].generate(21)

        # Confirm multiplier of one
        result_pre = self.nodes[0].getmasternode(nodeid_pre)
        assert_equal(result_pre[nodeid_pre]['targetMultiplier'], 1)

        # Time travel a day and sync nodes
        disconnect_nodes(self.nodes[0], 1)
        self.nodes[0].set_mocktime(int(time.time()) + (24 * 60 * 60))
        self.nodes[0].generate(2)
        self.nodes[1].set_mocktime(int(time.time()) + (24 * 60 * 60))
        self.nodes[1].generate(1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # Confirm multiplier of four
        result_pre = self.nodes[0].getmasternode(nodeid_pre)
        assert_equal(result_pre[nodeid_pre]['targetMultiplier'], 4)

        # Generate to hard fork
        self.nodes[0].generate(16)

        # Check MN now has multiplier of four on first subnode
        result_pre = self.nodes[0].getmasternode(nodeid_pre)
        assert_equal(result_pre[nodeid_pre]['targetMultipliers'], [4, 1])

        # Create MNs with locked funds
        self.nodes[0].sendtoaddress(collateral20, 1)
        nodeid = self.nodes[0].createmasternode(collateral)
        nodeid5 = self.nodes[0].createmasternode(collateral5, "", [], "FIVEYEARTIMELOCK")
        nodeid10 = self.nodes[0].createmasternode(collateral10, "", [], "TENYEARTIMELOCK")
        self.nodes[0].generate(1)

        # Test MN creation with non-standard 20 year lock-in
        nodeid20 = self.nodes[0].createmasternode(collateral20, "", [], "TENYEARTIMELOCK")
        nodeid20_raw = self.nodes[0].getrawtransaction(nodeid20)
        self.nodes[0].clearmempool()
        pos = nodeid20_raw.find('446654784301')
        nodeid20_raw = nodeid20_raw[:pos + 52] + '1004' + nodeid20_raw[pos + 52 + 4:]

        try:
            self.nodes[0].sendrawtransaction(nodeid20_raw)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Timelock must be set to either 0, 5 or 10 years" in errorString)

        # Check state and timelock length
        result = self.nodes[0].getmasternode(nodeid)
        assert_equal(result[nodeid]['state'], 'PRE_ENABLED')
        assert_equal('timelock' not in result[nodeid], True)
        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['state'], 'PRE_ENABLED')
        assert_equal(result5[nodeid5]['timelock'], '5 years')
        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['state'], 'PRE_ENABLED')
        assert_equal(result10[nodeid10]['timelock'], '10 years')

        # Activate masternodes
        self.nodes[0].generate(20)
        self.sync_blocks()

        # Check multiplier reset and rollback
        disconnect_nodes(self.nodes[0], 1)
        result = self.nodes[1].getmininginfo()
        print(result)
        assert_equal(result['masternodes'][0]['targetMultipliers'], [5, 1])
        while result['masternodes'][0]['targetMultipliers'] == [5, 1]:
            self.nodes[1].generate(1)
            result = self.nodes[1].getmininginfo()
        assert_equal(result['masternodes'][0]['targetMultipliers'], [1, 1])
        self.nodes[1].invalidateblock(self.nodes[1].getblockhash(self.nodes[1].getblockcount() - 1))
        result = self.nodes[1].getmininginfo()
        assert_equal(result['masternodes'][0]['targetMultipliers'], [5, 1])

        # Check all multipliers are set to 1 and we only report for subnodes we have
        result = self.nodes[0].getmasternode(nodeid)
        assert_equal(result[nodeid]['targetMultipliers'], [1, 1])

        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['targetMultipliers'], [1, 1, 1])
        assert('multiplierSubnode3' not in result5[nodeid5])

        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['targetMultipliers'], [1, 1, 1, 1])

        # Time travel a day
        self.nodes[0].set_mocktime(int(time.time()) + (2 * 24 * 60 * 60))
        self.nodes[0].generate(1)

        # Check all multipliers have increased
        result = self.nodes[0].getmasternode(nodeid)
        assert_equal(result[nodeid]['targetMultipliers'], [4, 4])

        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['targetMultipliers'], [4, 4, 4])

        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['targetMultipliers'], [4, 4, 4, 4])

        # Time travel a week
        self.nodes[0].set_mocktime(int(time.time()) + (8 * 24 * 60 * 60))
        self.nodes[0].generate(1)

        # Check all multipliers have increased
        result = self.nodes[0].getmasternode(nodeid)
        assert_equal(result[nodeid]['targetMultipliers'], [28, 28])

        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['targetMultipliers'], [28, 28, 28])

        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['targetMultipliers'], [28, 28, 28, 28])

        # Time travel two weeks for max multiplier
        self.nodes[0].set_mocktime(int(time.time()) + (15 * 24 * 60 * 60) + 60)
        self.nodes[0].generate(1)

        # Check all multipliers have increased
        result = self.nodes[0].getmasternode(nodeid)
        assert_equal(result[nodeid]['targetMultipliers'], [57, 57])

        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['targetMultipliers'], [57, 57, 57])

        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['targetMultipliers'], [57, 57, 57, 57])

        # Let's try and resign the MNs
        try:
            self.nodes[0].resignmasternode(nodeid5)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Trying to resign masternode before timelock expiration" in errorString)

        try:
            self.nodes[0].resignmasternode(nodeid10)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Trying to resign masternode before timelock expiration" in errorString)

        # Time travel five years, add a day as we moved forward a day at the start
        self.nodes[0].set_mocktime(int(time.time()) + (5 * 365 * 24 * 60 * 60) + (24 * 60 * 60))

        # Generate enough future blocks to create average future time
        self.nodes[0].generate(41)

        # Check state
        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['state'], 'ENABLED')

        # Timelock should no longer be present
        assert_equal('timelock' not in result5[nodeid5], True)

        # Resign 5 year MN
        self.nodes[0].resignmasternode(nodeid5)

        # Try to resign 10 year MN
        try:
            self.nodes[0].resignmasternode(nodeid10)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Trying to resign masternode before timelock expiration" in errorString)

        # Generate enough blocks to confirm resignation
        self.nodes[0].generate(41)
        result5 = self.nodes[0].getmasternode(nodeid5)
        assert_equal(result5[nodeid5]['state'], 'RESIGNED')

        # Time travel ten years, add a day as we moved forward a day at the start
        self.nodes[0].set_mocktime(int(time.time()) + (10 * 365 * 24 * 60 * 60) + (24 * 60 * 60))

        # Generate enough future blocks to create average future time
        self.nodes[0].generate(41)

        # Check state
        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['state'], 'ENABLED')

        # Timelock should no longer be present
        assert_equal('timelock' not in result10[nodeid10], True)

        # Resign 10 year MN
        self.nodes[0].resignmasternode(nodeid10)

        # Generate enough blocks to confirm resignation
        self.nodes[0].generate(41)
        result10 = self.nodes[0].getmasternode(nodeid10)
        assert_equal(result10[nodeid10]['state'], 'RESIGNED')

if __name__ == '__main__':
    MasternodesTimelockTest().main()
