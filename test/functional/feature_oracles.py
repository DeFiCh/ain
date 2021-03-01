#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""
import decimal

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, connect_nodes_bi, assert_raises_rpc_error

import calendar
import time


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

        self.default_oracle_money = 50

    def create_tokens(self):
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].generate(1)

        # 1 Creating DAT token
        create_token_tx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": False,
            "collateralAddress": collateral0
        })

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        # At this point, token was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['128']["symbol"], "GOLD")

        # check sync:
        tokens = self.nodes[2].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['128']["symbol"], "GOLD")

        # 3 Trying to make regular token
        self.nodes[0].generate(1)
        create_token_tx = self.nodes[0].createtoken({
            "symbol": "PT",
            "name": "Platinum",
            "isDAT": False,
            "collateralAddress": collateral0
        })
        self.nodes[0].generate(1)

        # Checks
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 3)
        assert_equal(tokens['129']["symbol"], "PT")
        assert_equal(tokens['129']["creationTx"], create_token_tx)
        self.sync_all([self.nodes[0], self.nodes[2]])

    @staticmethod
    def find_address_tx(node, address):
        vals = node.listunspent()
        count = len(vals)
        for i in range(0, count):
            v = vals[i]
            if v['address'] == address:
                return v
        return None

    def get_token_names(self):
        tokens = self.nodes[0].listtokens()
        return ['{}#{}'.format(val['symbol'], key) for key, val in tokens.items()]

    def run_test(self):
        self.nodes[0].generate(200)
        self.sync_all()

        # Stop node #3 for future revert
        self.stop_node(3)

        print('node[0] is a founder')

        assert_equal(len(self.nodes[0].listtokens()), 1)    # only one token == DFI
        print('currently we have following tokens: {}', self.get_token_names())
        print('create tokens PT#1 and GOLD#128')
        self.create_tokens()
        print('now we have tokens following tokens:', self.get_token_names())

        # create oracle addresses
        print('create oracle addresses:')

        oracle_address1 = self.nodes[2].getnewaddress("", "legacy")
        print('oracle1 address is', oracle_address1)
        oracle_address2 = self.nodes[2].getnewaddress("", "legacy")
        print('address2 address is', oracle_address2)
        print('node[2] owns both')

        print('oracles have no money yet')
        assert (self.find_address_tx(self.nodes[2], oracle_address1) is None)
        assert (self.find_address_tx(self.nodes[2], oracle_address2) is None)

        # supply oracles with money for transactions
        print('supply oracles with money for transactions')
        self.nodes[0].sendtoaddress(oracle_address1, 50)
        self.nodes[0].sendtoaddress(oracle_address2, 50)

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        send_money_tx1, send_money_tx2 = \
            [self.find_address_tx(self.nodes[2], address) for address in [oracle_address1, oracle_address2]]

        assert(send_money_tx1 is not None)
        assert(send_money_tx2 is not None)

        print('oracle1 address now has', send_money_tx1['amount'])
        print('oracle1 address now has', send_money_tx2['amount'])
        assert(send_money_tx1['amount'] == decimal.Decimal(50))
        assert(send_money_tx2['amount'] == decimal.Decimal(50))

        print('currently we have no oracles')
        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())
        assert_equal(self.nodes[0].listoracles(), [])
        assert_equal(self.nodes[2].listoracles(), [])

        price_feeds1 = '[{"currency": "USD", "token": "GOLD#128"}, {"currency": "USD", "token": "PT#129"}]'
        print('appoint oracle1, its allowed token-currency pairs are:', price_feeds1)
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        all_oracles = self.nodes[2].listoracles()
        print('we have created oracle1, so now we have 1 oracle')
        print('all oracles:', all_oracles)
        assert_equal(self.nodes[0].listoracles(), [oracle_id1])
        assert_equal(all_oracles, [oracle_id1])

        price_feeds2 = '[{"currency": "EUR", "token": "PT#129"}, {"currency": "EUR", "token": "GOLD#128"}]'
        print('appoint oracle2, its allowed token-currency pairs are:', price_feeds2)
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        all_oracles = self.nodes[2].listoracles()
        print('we have created oracle2, so now we have 2 oracles')
        print('all oracles:', all_oracles)

        assert_equal(sorted(all_oracles), sorted([oracle_id1, oracle_id2]))

        print('oracle id is txid of oracle appointment')
        print('oracleid1 is', oracle_id1)
        print('oracleid2 is', oracle_id2)

        print('node2 oracles:', all_oracles)

        print('oracle1 feeds: ', self.nodes[0].getpricefeeds(oracle_id1))
        print('oracle2 feeds: ', self.nodes[0].getpricefeeds(oracle_id2))

        print('demonstrate oracle removal, remove oracle2:', oracle_id2, 'is going to be removed')
        self.nodes[0].removeoracle(oracle_id2)

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        all_oracles = self.nodes[2].listoracles()
        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', all_oracles)

        assert_equal(all_oracles, [oracle_id1])

        print('now re-create oracle2')

        price_feeds2 = '[{"currency": "EUR", "token": "PT#129"}, {"currency": "EUR", "token": "GOLD#128"}]'
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # feeds = '[{"currency": "USD", "token": "PT"}, ' \
        #         '{"currency": "EUR", "token": "PT"}, {"currency": "EUR", "token": "GOLD#128"}, ' \
        #         '{"currency": "USD", "token": "GOLD#128"}]'
        # oracle_id1 = self.nodes[0].appointoracle(oracle_address1, feeds, 10)
        #
        # self.nodes[0].generate(1)
        # self.sync_all([self.nodes[0], self.nodes[2]])

        print('node0 oracles:', self.nodes[0].listoracles())
        print('node2 oracles:', self.nodes[2].listoracles())

        timestamp = calendar.timegm(time.gmtime())

        self.nodes[2].setoracledata(
            oracle_id1, timestamp,
            '[{"currency": "USD", "tokenAmount": "10.1@PT#129"}, {"currency": "USD", "tokenAmount": "5@GOLD#128"}]'
        )

        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        price_feeds1 = \
            '[{"currency": "USD", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "GOLD#128"}, ' \
            '{"currency": "USD", "token": "GOLD#128"}]'
        update_oracle_res = self.nodes[0].updateoracle(oracle_id1, oracle_address1, price_feeds1, 15)

        print(update_oracle_res)
        print('decoded update oracle tx', self.nodes[0].getrawtransaction(update_oracle_res, 1))

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        print('oracle1 feeds node0: ', self.nodes[0].getpricefeeds(oracle_id1))
        print('oracle1 feeds node2: ', self.nodes[2].getpricefeeds(oracle_id1))

        print('set oracle data tx:',
              self.nodes[2].setoracledata(
                  oracle_id1, timestamp,
                  '[{"currency": "USD", "tokenAmount": "10.2@PT#129"}]')
              )
        # self.nodes[2].setoracledata(oracle_id1['txid'], timestamp,
        #                             '[{"currency": "USD", "tokenAmount": "10.5@PT#129"},'
        #                             ' {"currency": "USD", "tokenAmount": "7@GOLD#128"},'
        #                             ' {"currency": "EUR", "tokenAmount": "5@GOLD#128"}]')

        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        self.nodes[2].setoracledata(
            oracle_id1, timestamp,
            '['
            '{"currency": "EUR", "tokenAmount": "9@PT#129"},'
            '{"currency": "EUR", "tokenAmount": "5@GOLD#128"}'
            ']')

        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[2]])

        print('PT for USD prices', self.nodes[2].listlatestrawprices('{"currency": "USD", "token": "PT#129"}'))
        print('GOLD for USD prices', self.nodes[2].listlatestrawprices('{"currency": "USD", "token": "GOLD#128"}'))
        print('all feeds', self.nodes[2].listlatestrawprices())

        print('get aggregated price PT', self.nodes[2].getprice('{"currency": "USD", "token": "PT#129"}'))

        print('currently we have no oracles for GOLD for USD, check that')
        # assert_raises_message(self.nodes[2].getprice('{"currency": "USD", "token": "GOLD#128"}'))
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[2].getprice, '{"currency": "USD", "token": "GOLD#128"}')

        # print('get aggregated price GOLD', self.nodes[2].getprice('{"currency": "USD", "token": "GOLD#128"}'))

        print('PT for EUR', self.nodes[2].getprice('{"currency":"EUR", "token":"PT#129"}'))

        print('oracle', oracle_id1, 'will be removed')
        remove_oracle_txid = self.nodes[0].removeoracle(oracle_id1)

        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])
        # self.sync_mempools([self.nodes[0], self.nodes[2]])

        print('node 0 oracles: ', self.nodes[0].listoracles())
        print('node 2 oracles: ', self.nodes[2].listoracles())

        # try:
        #     print('oracle', oracle_id1, 'will be removed')
        #     self.nodes[0].removeoracle(oracle_id1)
        # except JSONRPCException as e:
        #     print('failed to remove oracle', oracle_id1['txid'], e.error['message'])
        #     raise


        # print('node 0 oracles: ', self.nodes[0].listoracles())
        # print('node 2 oracles: ', self.nodes[2].listoracles())

        # # remove oracle failure
        # self.sync_all()
        # print('oracle data returned:', self.nodes[3].removeoracle(oracle_id1))
        #
        # # remove oracle success
        # self.sync_all()
        # print('oracle data returned:', self.nodes[0].removeoracle(oracle_id1))


if __name__ == '__main__':
    OraclesTest().main()
