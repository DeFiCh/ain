#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

import calendar
import time
import json


class OraclesTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1)    # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_all([self.nodes[0], self.nodes[2]])

        # # Stop node #3 for future revert
        # self.stop_node(3)

        # create tokens:
        #========================
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].generate(1)

        # 1 Creating DAT token
        self.nodes[0].createtoken({
            "symbol": "PT",
            "name": "Platinum",
            "isDAT": True,
            "collateralAddress": collateral0
        })

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2], self.nodes[2]])

        # At this point, token was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['1']["symbol"], "PT")

        # check sync:
        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['1']["symbol"], "PT")

        # 3 Trying to make regular token
        self.nodes[0].generate(1)
        createTokenTx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": False,
            "collateralAddress": collateral0
        })
        self.nodes[0].generate(1)

        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)
        self.sync_all([self.nodes[0], self.nodes[2]])
        #7 Create oracle node[1]
        oracle_address1 = self.nodes[2].getnewaddress("", "legacy")
        oracle_address2 = self.nodes[2].getnewaddress("", "legacy")

        print('address1', oracle_address1)
        print('address2', oracle_address2)
        self.nodes[0].sendtoaddress(oracle_address1, 50)
        self.nodes[0].sendtoaddress(oracle_address2, 50)

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])
        oracle_id1 = ''

        try:
            oracle_id1 = self.nodes[0].appointoracle(oracle_address1, '["PT", "GOLD#128"]', 10)
        except JSONRPCException as e:
            print('failed to appoint oracle', e.error['message'])
            raise

        try:
            oracle_id2 = self.nodes[0].appointoracle(oracle_address2, '["GOLD#128"]', 15)
        except JSONRPCException as e:
            print('failed to appoint oracle', e.error['message'])
            raise


        # oracle_id1 = json.loads(oracle_res)['oracleid']

        print('oracleid1', oracle_id1)
        print('decodedtx', self.nodes[0].getrawtransaction(oracle_id1, 1))
        print('oracleid2', oracle_id2)
        print('decodedtx2', self.nodes[0].getrawtransaction(oracle_id2, 1))

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        try:
            print("oracle_id1:", oracle_id1)
            print('node0 oracles:', self.nodes[0].listoracles())
        except JSONRPCException as e:
            print(e.error['message'])

        self.sync_all([self.nodes[0], self.nodes[2]])

        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())

        print('node0 balances', self.nodes[0].getbalances())
        print('node2 balances', self.nodes[2].getbalances())

        timestamp = calendar.timegm(time.gmtime())

        try:
            self.nodes[2].setoracledata(oracle_id1, timestamp, '["10.1@PT", "5@GOLD#128"]')
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise

        try:
            self.nodes[2].setoracledata(oracle_id2, timestamp, '["5@GOLD#128"]')
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise

        self.nodes[1].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        try:
            self.nodes[0].updateoracle(oracle_id2, oracle_address2, timestamp, '["PT"]', 15)
        except JSONRPCException as e:
            print('failed to update oracle', e.error['message'])
            raise

        # # remove oracle failure
        # self.sync_blocks()
        # print('oracle data returned:', self.nodes[3].removeoracle(oracle_id1))
        #
        # # remove oracle success
        # self.sync_blocks()
        # print('oracle data returned:', self.nodes[0].removeoracle(oracle_id1))


if __name__ == '__main__':
    OraclesTest().main()
