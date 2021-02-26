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

        self.nodes[0].generate(200)
        self.sync_all()

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
        self.sync_all([self.nodes[0], self.nodes[2]])

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

        #7 Create oracle node[2]
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
            feeds = '[{"currency": "USD", "token": "PT"}, {"currency": "USD", "token": "GOLD#128"}]'
            # feeds = '[{"currency": "USD", "token": "PT"}, ' \
            #         '{"currency": "EUR", "token": "PT"}, {"currency": "EUR", "token": "GOLD#128"}, ' \
            #         '{"currency": "USD", "token": "GOLD#128"}]'
            oracle_id1 = self.nodes[0].appointoracle(oracle_address1, feeds, 10)
        except JSONRPCException as e:
            print('failed to appoint oracle', e.error['message'])
            raise

        try:
            oracle2_pairs = '[{"currency": "EUR", "token": "PT"}, {"currency": "EUR", "token": "GOLD#128"}]'
            oracle_id2 = self.nodes[0].appointoracle(oracle_address2, oracle2_pairs, 15)
        except JSONRPCException as e:
            print('failed to appoint oracle', e.error['message'])
            raise

        print('oracleid1', oracle_id1)
        print('oracleid2', oracle_id2)

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        print('oracle1 feeds: ', self.nodes[0].getpricefeeds(oracle_id1['txid']))
        print('oracle2 feeds: ', self.nodes[0].getpricefeeds(oracle_id2['txid']))

        # feeds = '[{"currency": "USD", "token": "PT"}, ' \
        #         '{"currency": "EUR", "token": "PT"}, {"currency": "EUR", "token": "GOLD#128"}, ' \
        #         '{"currency": "USD", "token": "GOLD#128"}]'
        # oracle_id1 = self.nodes[0].appointoracle(oracle_address1, feeds, 10)
        #
        # self.nodes[0].generate(1)
        # self.sync_all([self.nodes[0], self.nodes[2]])

        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())

        print('node0 balances', self.nodes[0].getbalances())
        print('node2 balances', self.nodes[2].getbalances())

        timestamp = calendar.timegm(time.gmtime())

        # input("debug set oracle data")
        print('check if appoint oracle1 is applied on node 0:', self.nodes[0].isappliedcustomtx(oracle_id1['txid'], oracle_id1['height']))
        print('check if appoint oracle1 is applied on node 0:', self.nodes[2].isappliedcustomtx(oracle_id1['txid'], oracle_id1['height']))
        print('masternodes:', self.nodes[0].listmasternodes())

        try:
            self.nodes[2].setoracledata(
                oracle_id1['txid'], timestamp,
                '[{"currency": "USD", "tokenAmount": "10.1@PT"}, {"currency": "USD", "tokenAmount": "5@GOLD#128"}]'
            )
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise

        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        update_oracle_res = ''
        try:
            feeds = '[{"currency": "USD", "token": "PT"}, ' \
                    '{"currency": "EUR", "token": "PT"}, ' \
                    '{"currency": "EUR", "token": "GOLD#128"}, ' \
                    '{"currency": "USD", "token": "GOLD#128"}]'
            input("debug")
            update_oracle_res = self.nodes[0].updateoracle(oracle_id1['txid'], oracle_address1, feeds, 15)
        except JSONRPCException as e:
            print('failed to update oracle', e.error['message'])
            raise

        print(update_oracle_res)
        print('decoded update oracle tx', self.nodes[0].getrawtransaction(update_oracle_res['txid'], 1))

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        print('oracle1 feeds: ', self.nodes[0].getpricefeeds(oracle_id1['txid']))

        input("debug")

        print('check if update oracle1 is applied on node 0:',
              self.nodes[0].isappliedcustomtx(update_oracle_res['txid'], update_oracle_res['height']))
        print('check if update oracle1 is applied on node 2:',
              self.nodes[2].isappliedcustomtx(update_oracle_res['txid'], update_oracle_res['height']))

        try:
            print('set oracle data tx:',
                  self.nodes[2].setoracledata(
                      oracle_id1['txid'],
                      timestamp,
                      '[{"currency": "USD", "tokenAmount": "10.2@PT"}]')
                  )
            # self.nodes[2].setoracledata(oracle_id1['txid'], timestamp,
            #                             '[{"currency": "USD", "tokenAmount": "10.5@PT"},'
            #                             ' {"currency": "USD", "tokenAmount": "7@GOLD#128"},'
            #                             ' {"currency": "EUR", "tokenAmount": "5@GOLD#128"}]')
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise

        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        try:
            self.nodes[0].setoracledata(
                oracle_id1['txid'], timestamp,
                '[{"currency": "EUR", "tokenAmount": "9@PT"},'
                ' {"currency": "EUR", "tokenAmount": "5@GOLD#128"}]')
        except JSONRPCException as e:
            print('failed to set oracle data', e.error['message'])
            raise


        try:
            print('PT prices', self.nodes[2].listlatestrawprices('{"currency": "USD", "token": "PT"}'))
            print('GOLD prices', self.nodes[2].listlatestrawprices('{"currency": "USD", "token": "GOLD#128"}'))
            print('all feeds', self.nodes[2].listlatestrawprices())

            print('get aggregated price PT', self.nodes[2].getprice('{"currency": "USD", "token": "PT"}'))
            print('get aggregated price GOLD', self.nodes[2].getprice('{"currency": "USD", "token": "GOLD#128"}'))
        except JSONRPCException as e:
            print('failed to list prices', e.error['message'])
        except Exception as e:
            print(str(e))

        try:
            print('PT for EUR', self.nodes[2].getprice('{"currency":"EUR", "token":"PT"}'))
        except JSONRPCException as e:
            print('failed to calculate aggregated price PT in EU', e.error['message'])

        remove_oracle_txid = ''
        # input('debug')
        try:
            print('oracle', oracle_id1['txid'], 'will be removed')
            remove_oracle_txid = self.nodes[0].removeoracle(oracle_id1['txid'])
        except JSONRPCException as e:
            print('failed to remove oracle', oracle_id1['txid'], e.error['message'])
            raise

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        time.sleep(2)

        print('node 0 oracles: ', self.nodes[0].listoracles())
        print('node 2 oracles: ', self.nodes[2].listoracles())

        print('check if remove oracle applied on node 2:', self.nodes[2].isappliedcustomtx(remove_oracle_txid['txid'], remove_oracle_txid['height']))
        # print('decoded remove oracle tx', self.nodes[2].getrawtransaction(remove_oracle_txid['txid'], 1))

        # try:
        #     print('oracle', oracle_id1['txid'], 'will be removed')
        #     self.nodes[0].removeoracle(oracle_id1['txid'])
        # except JSONRPCException as e:
        #     print('failed to remove oracle', oracle_id1['txid'], e.error['message'])
        #     raise


        # print('node 0 oracles: ', self.nodes[0].listoracles())
        # print('node 2 oracles: ', self.nodes[2].listoracles())

        # # remove oracle failure
        # self.sync_all()
        # print('oracle data returned:', self.nodes[3].removeoracle(oracle_id1['txid']))
        #
        # # remove oracle success
        # self.sync_all()
        # print('oracle data returned:', self.nodes[0].removeoracle(oracle_id1['txid']))


if __name__ == '__main__':
    OraclesTest().main()
