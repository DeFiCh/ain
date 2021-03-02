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
from test_framework.util import assert_equal, connect_nodes_bi, assert_raises_rpc_error

import decimal
import math
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

    def list_tokens(self):
        return self.nodes[1].listtokens()

    @staticmethod
    def format_name(k, v):
        return '{}#{}'.format(v['symbol'], k) if not v['isDAT'] else v['symbol']

    def get_token_names(self):
        tokens = self.list_tokens()

        return [self.format_name(key, val) for key, val in tokens.items()]

    @staticmethod
    def cmp_price_feed(x):
        return x['token'], x['currency']

    def run_test(self):
        self.nodes[0].generate(200)
        print('synchronize')
        self.sync_all()

        # === stop node #3 for future revert ===
        self.stop_node(3)

        print('node[0] is a founder')
        print('we will appoint, update and remove oracles on node[0] - founder node')
        print('we will update oracle data on node[2] - oracle node')
        print('we will read data on node[1] - user node, since reading data requires no founder or oracle rights')

        assert_equal(len(self.list_tokens()), 1)    # only one token == DFI
        print('currently we have following tokens:', self.get_token_names())
        print('create tokens PT#1 and GOLD#128')
        self.create_tokens()
        print('now we have following tokens:', self.get_token_names())

        # === create oracle addresses ===
        print('create oracle addresses:')

        print('getnewaddress', '""', "legacy")
        print('getnewaddress', '""', "legacy")
        oracle_address1 = self.nodes[2].getnewaddress("", "legacy")
        print('oracle1 address is', oracle_address1)
        oracle_address2 = self.nodes[2].getnewaddress("", "legacy")
        print('address2 address is', oracle_address2)
        print('node[2] owns both')

        print('oracles have no money yet')
        assert (self.find_address_tx(self.nodes[1], oracle_address1) is None)
        assert (self.find_address_tx(self.nodes[1], oracle_address2) is None)

        # === supply oracles with money for transactions ===
        print('supply oracles with money for transactions')
        print('sendtoaddress', oracle_address1, 50)
        print('sendtoaddress', oracle_address2, 50)
        self.nodes[0].sendtoaddress(oracle_address1, 50)
        self.nodes[0].sendtoaddress(oracle_address2, 50)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1], self.nodes[2]])

        send_money_tx1, send_money_tx2 = \
            [self.find_address_tx(self.nodes[2], address) for address in [oracle_address1, oracle_address2]]

        assert(send_money_tx1 is not None)
        assert(send_money_tx2 is not None)

        print('oracle1 address now has', send_money_tx1['amount'])
        print('oracle2 address now has', send_money_tx2['amount'])
        assert(send_money_tx1['amount'] == decimal.Decimal(50))
        assert(send_money_tx2['amount'] == decimal.Decimal(50))

        print('oracles:', self.nodes[1].listoracles())
        print('currently we have no oracles')

        # === appoint oracles ===
        price_feeds1 = '[{"currency": "USD", "token": "GOLD#128"}, {"currency": "USD", "token": "PT#129"}]'
        print('appoint oracle1')
        print('appointoracle', oracle_address1, price_feeds1, 10)
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        all_oracles = self.nodes[1].listoracles()
        print('we have created oracle1, so now we have 1 oracle')
        print('all oracles:\n',
              'listoracles\n',
              all_oracles)
        assert_equal(all_oracles, [oracle_id1])

        price_feeds2 = '[{"currency": "EUR", "token": "PT#129"}, {"currency": "EUR", "token": "GOLD#128"}]'
        print('appoint oracle2\n',
              'appointoracle', oracle_address2, price_feeds2, 15)
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        all_oracles = self.nodes[1].listoracles()
        print('we have created oracle2, so now we have 2 oracles')
        print('all oracles:', all_oracles)

        assert_equal(sorted(all_oracles), sorted([oracle_id1, oracle_id2]))

        print('oracle id is txid of oracle appointment')
        print('oracleid1 is', oracle_id1)
        print('oracleid2 is', oracle_id2)

        print('get oracle1 feeds: \n',
              'getpricefeeds', oracle_id1, '\n',
              self.nodes[1].getpricefeeds(oracle_id1))

        print('get oracle2 feeds: \n',
              'getpricefeeds', oracle_id2, '\n',
              self.nodes[1].getpricefeeds(oracle_id2))

        # === remove oracle ===
        print('demonstrate oracle removal, remove oracle2:', oracle_id2, 'is going to be removed')
        print('removeoracle', oracle_id2)
        self.nodes[0].removeoracle(oracle_id2)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        all_oracles = self.nodes[1].listoracles()
        print('all oracles:', all_oracles)

        assert_equal(all_oracles, [oracle_id1])

        # === try to remove oracle, but fail ===
        print('now when oracle2 is removed, removing it again will fail')
        assert_raises_rpc_error(-32600, 'Oracle <{}> doesn\'t exist'.format(oracle_id2),
                                self.nodes[0].removeoracle, oracle_id2)

        # save removed oracle id for future tests
        invalid_oracle_id = oracle_id2

        # === re-create oracle2 ===
        print('now re-create oracle2')

        price_feeds2 = '[{"currency": "EUR", "token": "GOLD#128"}, {"currency": "EUR", "token": "PT#129"}]'
        print('appointoracle', oracle_address2, price_feeds2, 15)
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)
        print('new oracle_id2 is', oracle_id2)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        # === check that price feeds are set correctly ===
        assert_equal(json.loads(price_feeds1), self.nodes[1].getpricefeeds(oracle_id1))
        assert_equal(json.loads(price_feeds2), self.nodes[1].getpricefeeds(oracle_id2))

        all_oracles = self.nodes[1].listoracles()
        print('all oracles:', all_oracles)

        # === check get price in case when no live oracles are available ===
        print('currently oracles have no data, so price requests will fail')
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, '{"currency": "USD", "token": "GOLD#128"}')
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, '{"currency": "USD", "token": "PT#129"}')
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, '{"currency": "EUR", "token": "GOLD#128"}')
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, '{"currency": "EUR", "token": "PT#129"}')

        timestamp = calendar.timegm(time.gmtime())

        # === set oracle data, simple case ===
        print('set oracle data for oracle1')
        oracle1_prices = '[{"currency": "USD", "tokenAmount": "10.1@PT#129"}, '\
                         '{"currency": "USD", "tokenAmount": "5@GOLD#128"}]'
        print('setoracledata', oracle_id1, timestamp, oracle1_prices)
        self.nodes[2].setoracledata(oracle_id1, timestamp, oracle1_prices)

        print('synchronize')
        self.nodes[2].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        print('PT#129 aggregated price in USD = ', self.nodes[1].getprice('{"currency":"USD", "token":"PT#129"}'))
        print('GOLD#128 aggregated price in USD = ', self.nodes[1].getprice('{"currency":"USD", "token":"GOLD#128"}'))

        # decimals are not strict values, so we need to look how small is the difference
        assert(math.isclose(
            self.nodes[1].getprice('{"currency":"USD", "token":"PT#129"}'), decimal.Decimal(10.1)))

        assert(math.isclose(
            self.nodes[1].getprice('{"currency":"USD", "token":"GOLD#128"}'), decimal.Decimal(5)))

        print('get latest raw prices for GOLD#128 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"GOLD#128"}'))

        print('get latest raw prices for PT#129 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"PT#129"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"PT#129"}'))

        print('all aggregated prices list now:\n',
              'listprices\n',
              self.nodes[1].listprices())

        price_feeds1 = \
            '[{"currency": "USD", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "GOLD#128"}, ' \
            '{"currency": "USD", "token": "GOLD#128"}]'
        print('updateoracle', oracle_id1, oracle_address1, price_feeds1, 15)
        self.nodes[0].updateoracle(oracle_id1, oracle_address1, price_feeds1, 15)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        # === check price feeds ===
        print('oracle1 feeds: ',
              'getpricefeeds', oracle_id1, '\n',
              self.nodes[1].getpricefeeds(oracle_id1))
        assert_equal(
            sorted(json.loads(price_feeds1), key=self.cmp_price_feed),
            sorted(self.nodes[1].getpricefeeds(oracle_id1), key=self.cmp_price_feed)
        )

        print('oracle2 feeds: \n',
              'getpricefeeds', oracle_id2, '\n',
              self.nodes[1].getpricefeeds(oracle_id2))
        assert_equal(
            sorted(json.loads(price_feeds2), key=self.cmp_price_feed),
            sorted(self.nodes[1].getpricefeeds(oracle_id2), key=self.cmp_price_feed)
        )

        # === check listprice in case when no valid prices available ===
        # === the listprices method returns information ===
        assert (len(self.nodes[1].listprices()) == 4)
        # === but no valid prices in both oracles ===
        assert (len([x for x in self.nodes[1].listprices() if x['ok']]) == 2)

        # === check get prices methods complex case ===

        oracle1_prices = '[{"currency": "USD", "tokenAmount": "11.5@PT#129"},'\
                         '{"currency": "EUR", "tokenAmount": "5@GOLD#128"}]'

        print('setoracledata', oracle_id1, timestamp, oracle1_prices)
        self.nodes[2].setoracledata(oracle_id1, timestamp, oracle1_prices)

        oracle2_prices = '[{"currency": "EUR", "tokenAmount": "4@GOLD#128"}]'

        print('setoracledata', oracle_id2, timestamp, oracle2_prices)
        self.nodes[2].setoracledata(oracle_id2, timestamp, oracle2_prices)

        print('synchronize')
        self.nodes[2].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])

        print('get PT prices in USD\n',
              'listlatestrawprices', '{"currency": "USD", "token": "PT#129"}\n',
              self.nodes[1].listlatestrawprices('{"currency": "USD", "token": "PT#129"}'))
        print('get GOLD prices in USD\n',
              'listlatestrawprices', '{"currency": "USD", "token": "GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency": "USD", "token": "GOLD#128"}'))
        print('all feeds\n',
              'listlatestrawprices\n',
              self.nodes[1].listlatestrawprices())

        print('get aggregated price PT:\n',
              'getprice {"currency": "USD", "token": "PT#129"}\n',
              self.nodes[1].getprice('{"currency": "USD", "token": "PT#129"}'))

        # # === currently we have no oracles for GOLD in USD, check that ===
        # assert_raises_rpc_error(-1, 'no live oracles for specified request',
        #                         self.nodes[1].getprice, '{"currency": "USD", "token": "GOLD#128"}')

        # === set missing data ===

        price_feeds2 = \
            '[{"currency": "USD", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "PT#129"}, ' \
            '{"currency": "EUR", "token": "GOLD#128"}, ' \
            '{"currency": "USD", "token": "GOLD#128"}]'

        print('update oracle2\n',
              'updateoracle', oracle_id2, oracle_address2, price_feeds2, 25)

        self.nodes[0].updateoracle(oracle_id2, oracle_address2, price_feeds2, 25)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1], self.nodes[2]])

        # === we have no valid oracle values for PT#129 in EUR ===
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, '{"currency": "EUR", "token": "PT#129"}')

        print('set missing data\n',
              'setoracledata ',
              '[{"currency":"USD", "tokenAmount":"10@GOLD#128"}, '
              '{"currency":"EUR", "tokenAmount":"7@PT#129"}]', '\n',
              self.nodes[2].setoracledata(oracle_id2, timestamp,
                                          '[{"currency":"USD", "tokenAmount":"10@GOLD#128"},'
                                          '{"currency":"EUR", "tokenAmount":"7@PT#129"}]'))

        print('set oracle1 data\n',
              'setoracledata', oracle_id1, timestamp,
              '[{"currency":"USD", "tokenAmount":"11@GOLD#128"},'
              '{"currency":"EUR", "tokenAmount":"8@PT#129"},'
              '{"currency":"EUR", "tokenAmount":"10@GOLD#128"},'
              '{"currency":"USD", "tokenAmount":"7@PT#129"}]', '\n',
              self.nodes[2].setoracledata(oracle_id1, timestamp,
                                          '[{"currency":"USD", "tokenAmount":"11@GOLD#128"},'
                                          '{"currency":"EUR", "tokenAmount":"8@PT#129"},'
                                          '{"currency":"EUR", "tokenAmount":"10@GOLD#128"},'
                                          '{"currency":"USD", "tokenAmount":"7@PT#129"}]'))

        print('synchronize')
        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1], self.nodes[2]])

        print('now we have missing data',
              'getprice {"currency": "USD", "token": "GOLD#128"}\n',
              self.nodes[1].getprice('{"currency": "USD", "token": "GOLD#128"}'))

        print('get aggregated price for PT in EUR\n',
              'getprice {"currency":"EUR", "token":"PT#129"}\n',
              self.nodes[1].getprice('{"currency":"EUR", "token":"PT#129"}'))

        print('get latest raw prices for GOLD#128 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"GOLD#128"}'))

        print('get latest raw prices for PT#129 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"PT#129"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"PT#129"}'))

        print('get latest raw prices for GOLD#128 in EUR:\n',
              'listlatestrawprices', '{"currency":"EUR", "token":"GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"EUR", "token":"GOLD#128"}'))

        print('get latest raw prices for PT#129 in USD:\n',
              'listlatestrawprices', '{"currency":"EUR", "token":"PT#129"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"EUR", "token":"PT#129"}'))

        print('all aggregated prices list now:\n',
              'listprices\n',
              self.nodes[1].listprices())

        # === check expired price feed ===
        # print('check price feed expiration')
        print('set expired timestamp to oracle1 prices')
        print('set oracle1 data\n',
              'setoracledata', oracle_id1, timestamp - 7200,
              '[{"currency":"USD", "tokenAmount":"11@GOLD#128"},'
              '{"currency":"EUR", "tokenAmount":"8@PT#129"},'
              '{"currency":"EUR", "tokenAmount":"10@GOLD#128"},'
              '{"currency":"USD", "tokenAmount":"7@PT#129"}]', '\n',
              self.nodes[2].setoracledata(oracle_id1, timestamp - 7200,
                                          '[{"currency":"USD", "tokenAmount":"11@GOLD#128"},'
                                          '{"currency":"EUR", "tokenAmount":"8@PT#129"},'
                                          '{"currency":"EUR", "tokenAmount":"10@GOLD#128"},'
                                          '{"currency":"USD", "tokenAmount":"7@PT#129"}]'))

        print('synchronize')
        self.nodes[2].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1], self.nodes[2]])

        print('now some raw prices have `expired` state')

        print('now we have missing data',
              'getprice {"currency": "USD", "token": "GOLD#128"}\n',
              self.nodes[1].getprice('{"currency": "USD", "token": "GOLD#128"}'))

        print('get aggregated price for PT in EUR\n',
              'getprice {"currency":"EUR", "token":"PT#129"}\n',
              self.nodes[1].getprice('{"currency":"EUR", "token":"PT#129"}'))

        print('get latest raw prices for GOLD#128 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"GOLD#128"}'))

        pt_in_usd_raw_prices = self.nodes[1].listlatestrawprices('{"currency":"USD", "token":"PT#129"}')
        print('get latest raw prices for PT#129 in USD:\n',
              'listlatestrawprices', '{"currency":"USD", "token":"PT#129"}\n',
              pt_in_usd_raw_prices)

        assert_equal(len(pt_in_usd_raw_prices), 1)
        assert_equal(pt_in_usd_raw_prices[0]['state'], 'expired')

        print('get latest raw prices for GOLD#128 in EUR:\n',
              'listlatestrawprices', '{"currency":"EUR", "token":"GOLD#128"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"EUR", "token":"GOLD#128"}'))

        print('get latest raw prices for PT#129 in USD:\n',
              'listlatestrawprices', '{"currency":"EUR", "token":"PT#129"}\n',
              self.nodes[1].listlatestrawprices('{"currency":"EUR", "token":"PT#129"}'))

        print('all aggregated prices list now:\n',
              'listprices\n',
              self.nodes[1].listprices())

        # check for unsupported currency failure
        assert_raises_rpc_error(-8, 'Currency {} is not supported'.format('JPY'),
                                self.nodes[1].getprice, '{"currency":"JPY", "token":"PT#129"}')

        assert_raises_rpc_error(-8, 'Currency <{}> is not supported'.format('JPY'),
                                self.nodes[1].listlatestrawprices, '{"currency":"JPY", "token":"PT#129"}')

        assert_raises_rpc_error(-8, 'currency <{}> is not supported'.format('JPY'),
                                self.nodes[0].appointoracle,
                                oracle_address2, '[{"currency":"JPY", "token":"PT#129"}]', 15)

        assert_raises_rpc_error(-8, 'currency <{}> is not supported'.format('JPY'),
                                self.nodes[0].updateoracle,
                                oracle_id2, oracle_address2, '[{"currency":"JPY", "token":"PT#129"}]', 15)

        assert_raises_rpc_error(-8, 'currency <{}> is not supported'.format('JPY'),
                                self.nodes[0].setoracledata,
                                oracle_id2, timestamp, '[{"currency":"JPY", "tokenAmount":"10@PT#129"}]')

        # === check for unexpected token ===
        assert_raises_rpc_error(-22, 'Invalid Defi token: {}'.format('DOGE#130'),
                                self.nodes[1].getprice, '{"currency":"USD", "token":"DOGE#130"}')

        assert_raises_rpc_error(-22, 'Invalid Defi token: <{}>'.format('DOGE#130'),
                                self.nodes[1].listlatestrawprices, '{"currency":"USD", "token":"DOGE#130"}')

        assert_raises_rpc_error(-22, 'Invalid Defi token: {}'.format('DOGE#130'),
                                self.nodes[0].appointoracle,
                                oracle_address2, '[{"currency":"USD", "token":"DOGE#130"}]', 15)

        assert_raises_rpc_error(-22, 'Invalid Defi token: {}'.format('DOGE#130'),
                                self.nodes[0].updateoracle,
                                oracle_id2, oracle_address2, '[{"currency":"USD", "token":"DOGE#130"}]', 15)

        assert_raises_rpc_error(-22, 'Invalid Defi token: {}'.format('DOGE#130'),
                                self.nodes[0].setoracledata,
                                oracle_id2, timestamp, '[{"currency":"USD", "tokenAmount":"10@DOGE#130"}]')

        # === check for invalid oracle id

        assert_raises_rpc_error(-20, 'oracle <{}> not found'.format(invalid_oracle_id),
                                self.nodes[1].getpricefeeds, invalid_oracle_id)

        assert_raises_rpc_error(-32600, 'Oracle <{}> doesn\'t exist'.format(invalid_oracle_id),
                                self.nodes[0].updateoracle,
                                invalid_oracle_id, oracle_address2, '[{"currency":"USD", "token":"PT#129"}]', 15)

        assert_raises_rpc_error(-32600, 'oracle <{}> not found'.format(invalid_oracle_id),
                                self.nodes[0].setoracledata,
                                invalid_oracle_id, timestamp, '[{"currency":"USD", "tokenAmount":"10@PT#129"}]')

        print('all oracles now will be removed')
        print('removeoracle', oracle_id1)
        print('removeoracle', oracle_id2)
        self.nodes[0].removeoracle(oracle_id1)
        self.nodes[0].removeoracle(oracle_id2)

        print('synchronize')
        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1], self.nodes[2]])

        print('all oracles now: \n',
              'listoracles\n',
              self.nodes[1].listoracles())


if __name__ == '__main__':
    OraclesTest().main()
