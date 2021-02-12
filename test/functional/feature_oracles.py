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
        oracleAddress = self.nodes[2].getnewaddress("", "legacy")
        print('address', oracleAddress)
        self.nodes[0].sendtoaddress(oracleAddress, 50)

        self.nodes[0].generate(100)
        self.sync_all([self.nodes[0], self.nodes[2]])
        oracle_res = ''

        input('debug')

        try:
            oracle_res = self.nodes[0].appointoracle(oracleAddress, '["PT", "GOLD#128"]', 10)
        except JSONRPCException as e:
            print('failed to appoint oracle', e.error['message'])
            raise

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        input("debug")

        try:
            print("oracle_res:", oracle_res)
            print('node0 oracles:', self.nodes[0].listoracles())
        except JSONRPCException as e:
            print(e.error['message'])

        self.sync_all([self.nodes[0], self.nodes[2]])

        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())

        print('node0 balances', self.nodes[0].getbalances())
        print('node2 balances', self.nodes[2].getbalances())

        input("debug")

        oracle_id = json.loads(oracle_res)['oracleid']
        print('oracleid', oracle_id)
        # decodedtx = self.nodes[0].getrawtransaction(oracle_id, 1)
        # print('node0 raw oracle tx:', decodedtx)
        # for vin in decodedtx['vin']:
        #     print(vin)
        # print('node2 raw oracle tx:', self.nodes[2].getrawtransaction(oracle_id, 1))

        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())

        input("debug")

        timestamp = calendar.timegm(time.gmtime())
        oracle_data = ''
        input('debug')
        try:
            oracle_data = self.nodes[2].setoracledata(oracle_id, timestamp, '["10.1@PT", "5@GOLD#128"]')
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise

        print("oracle data:", oracle_data)

        # # remove oracle failure
        # self.sync_blocks()
        # print('oracle data returned:', self.nodes[3].removeoracle(oracle_id))
        #
        # # remove oracle success
        # self.sync_blocks()
        # print('oracle data returned:', self.nodes[0].removeoracle(oracle_id))


if __name__ == '__main__':
    OraclesTest().main()
