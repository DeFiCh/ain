#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""
from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import decimal
import math
import calendar
import time
import functools


class OraclesTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (foundation)
        # node1: data accessor
        # node2: non foundation
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=1']]

    @staticmethod
    def find_address_tx(node, address):
        vals = node.listunspent()
        count = len(vals)
        for i in range(0, count):
            v = vals[i]
            if v['address'] == address:
                return v
        return None

    @staticmethod
    def cmp_price_feed(x):
        return x['token'], x['currency']

    @staticmethod
    def parse_token_amount(token_amount: str):
        amount, _, token = token_amount.partition('@')
        return decimal.Decimal(amount), token

    def assert_compare_oracle_data(self, oracle_data_json, oracle_id=None, oracle_address=None,
                                   price_feeds=None, weightage=None, token_prices=None):
        if oracle_id is not None:
            if not oracle_id == oracle_data_json['oracleid']:
                raise Exception("oracleids don't match")

        # normally there is no need to compare oracle address
        if oracle_address is not None:
            if not oracle_address == oracle_data_json['address']:
                raise Exception("oracle addresses don't match")

        if weightage is not None:
            if not math.isclose(decimal.Decimal(weightage), decimal.Decimal(oracle_data_json['weightage'])):
                raise Exception("weightages are not equal")

        if price_feeds is not None:
            if not type(price_feeds) is list:
                raise Exception("price feeds is not list")

            if not sorted(price_feeds, key=self.cmp_price_feed) == \
                   sorted(oracle_data_json['priceFeeds'], key=self.cmp_price_feed):
                raise Exception("price feeds don't match")

        if token_prices is not None:
            token_prices_json = oracle_data_json['tokenPrices']
            for item in token_prices:
                amount, token = self.parse_token_amount(item['tokenAmount'])
                item['token'] = token
                item['amount'] = amount
            tpjs = sorted(token_prices_json, key=self.cmp_price_feed)
            tpps = sorted(token_prices, key=self.cmp_price_feed)

            if not functools.reduce(
                    lambda x, y: x and y,
                    map(
                        lambda p, q:
                        p['token'] == q['token']
                        and
                        p['currency'] == q['currency']
                        and
                        math.isclose(decimal.Decimal(p['amount']), decimal.Decimal(q['amount'])),
                    tpjs, tpps), True):
                raise Exception("prices are not equal")

    def synchronize(self, node: int, full: bool = False):
        self.nodes[node].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])
        if full:
            self.sync_mempools([self.nodes[0], self.nodes[1], self.nodes[2]])

    def run_test(self):
        self.nodes[0].generate(200)
        self.sync_all()

        # === stop node #3 for future revert ===
        self.stop_node(3)

        # === create oracle addresses ===
        oracle_address1 = self.nodes[2].getnewaddress("", "legacy")
        oracle_address2 = self.nodes[2].getnewaddress("", "legacy")

        assert (self.find_address_tx(self.nodes[1], oracle_address1) is None)
        assert (self.find_address_tx(self.nodes[1], oracle_address2) is None)

        # === supply oracles with money for transactions ===
        self.nodes[0].sendtoaddress(oracle_address1, 50)
        self.nodes[0].sendtoaddress(oracle_address2, 50)

        self.synchronize(0, full=True)

        send_money_tx1, send_money_tx2 = \
            [self.find_address_tx(self.nodes[2], address) for address in [oracle_address1, oracle_address2]]

        assert (send_money_tx1 is not None)
        assert (send_money_tx2 is not None)

        assert (send_money_tx1['amount'] == decimal.Decimal(50))
        assert (send_money_tx2['amount'] == decimal.Decimal(50))

        # === appoint oracles ===
        price_feeds1 = [{"currency": "USD", "token": "GOLD"}, {"currency": "USD", "token": "PT"}]

        # check that only founder can appoint oracles
        assert_raises_rpc_error(-5, 'Need foundation member authorization',
                                self.nodes[2].appointoracle, oracle_address1, price_feeds1, 10)
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)

        self.synchronize(node=0)

        all_oracles = self.nodes[1].listoracles()
        assert_equal(all_oracles, [oracle_id1])

        price_feeds2 = [{"currency": "EUR", "token": "PT"}, {"currency": "EUR", "token": "GOLD"}]
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)

        self.synchronize(node=0)

        all_oracles = self.nodes[1].listoracles()
        assert_equal(sorted(all_oracles), sorted([oracle_id1, oracle_id2]))

        # === remove oracle ===
        self.nodes[0].removeoracle(oracle_id2)

        # check that only founder can remove oracles
        assert_raises_rpc_error(-5, 'Need foundation member authorization',
                                self.nodes[2].removeoracle, oracle_id1)

        self.synchronize(node=0, full=True)

        all_oracles = self.nodes[1].listoracles()
        assert_equal(all_oracles, [oracle_id1])

        # === try to remove oracle, but fail ===
        assert_raises_rpc_error(-32600, 'oracle <{}> not found'.format(oracle_id2),
                                self.nodes[0].removeoracle, oracle_id2)

        # save removed oracle id for future tests
        invalid_oracle_id = oracle_id2

        # === re-create oracle2 ===

        price_feeds2 = [{"currency": "EUR", "token": "GOLD"}, {"currency": "EUR", "token": "PT"}]
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds2, 15)

        self.synchronize(node=0)

        # === check that price feeds are set correctly ===
        self.assert_compare_oracle_data(oracle_data_json=self.nodes[1].getoracledata(oracle_id1),
                                        oracle_id=oracle_id1,
                                        weightage=10,
                                        price_feeds=price_feeds1,
                                        token_prices=[]   # no prices yet
                                        )

        self.assert_compare_oracle_data(oracle_data_json=self.nodes[1].getoracledata(oracle_id2),
                                        oracle_id=oracle_id2,
                                        weightage=15,
                                        price_feeds=price_feeds2,
                                        token_prices=[]   # no prices yet
                                        )

        all_oracles = self.nodes[1].listoracles()
        assert_equal(sorted(all_oracles), sorted([oracle_id1, oracle_id2]))

        # === check get price in case when no live oracles are available ===
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, {"currency": "USD", "token": "GOLD"})
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, {"currency": "USD", "token": "PT"})
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, {"currency": "EUR", "token": "GOLD"})
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, {"currency": "EUR", "token": "PT"})

        timestamp = calendar.timegm(time.gmtime())

        # === set oracle data, simple case ===
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10.1@PT"}, {"currency": "USD", "tokenAmount": "5@GOLD"}]
        self.nodes[2].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.synchronize(node=2)

        oracle_data_json = self.nodes[1].getoracledata(oracle_id1)
        self.assert_compare_oracle_data(oracle_data_json=oracle_data_json, oracle_id=oracle_id1,
                                        price_feeds=price_feeds1, weightage=10,
                                        token_prices=oracle1_prices)    # now we have prices

        # decimals are not strict values, so we need to look how small is the difference
        assert (math.isclose(
            self.nodes[1].getprice({"currency":"USD", "token":"PT"}), decimal.Decimal(10.1)))

        assert (math.isclose(
            self.nodes[1].getprice({"currency":"USD", "token":"GOLD"}), decimal.Decimal(5)))

        price_feeds1 = [
            {"currency": "USD", "token": "PT"},
            {"currency": "EUR", "token": "PT"},
            {"currency": "EUR", "token": "GOLD"},
            {"currency": "USD", "token": "GOLD"}
        ]

        self.nodes[0].updateoracle(oracle_id1, oracle_address1, price_feeds1, 20)

        # check that only founder can appoint oracles
        assert_raises_rpc_error(-5, 'Need foundation member authorization',
                                self.nodes[2].updateoracle, oracle_id1, oracle_address1, price_feeds1, 10)

        self.synchronize(node=0)

        # === check price feeds ===
        oracle1_data = self.nodes[1].getoracledata(oracle_id1)

        self.assert_compare_oracle_data(oracle_data_json=oracle1_data,
                                        oracle_id=oracle_id1,
                                        price_feeds=price_feeds1,
                                        weightage=20)

        self.nodes[0].updateoracle(oracle_id1, oracle_address1, price_feeds1, 15)

        self.synchronize(node=0)

        oracle1_data = self.nodes[1].getoracledata(oracle_id1)
        # check only weightage
        self.assert_compare_oracle_data(oracle_data_json=oracle1_data,
                                        weightage=15)

        # check only prices
        self.assert_compare_oracle_data(oracle_data_json=oracle1_data,
                                        price_feeds=price_feeds1)

        oracle2_data = self.nodes[1].getoracledata(oracle_id2)

        self.assert_compare_oracle_data(oracle_data_json=oracle2_data,
                                        oracle_id=oracle_id2,
                                        price_feeds=price_feeds2,
                                        weightage=15)

        # === check listprice in case when no valid prices available ===
        # === the listprices method returns information ===
        assert (len(self.nodes[1].listprices()) == 4)
        # === but no valid prices in both oracles ===
        assert (len([x for x in self.nodes[1].listprices() if x['ok'] == True]) == 2)

        # === check get prices methods complex case ===

        oracle1_prices = [{"currency": "USD", "tokenAmount": "11.5@PT"}, {"currency": "EUR", "tokenAmount": "5@GOLD"}]

        self.nodes[2].setoracledata(oracle_id1, timestamp, oracle1_prices)

        oracle2_prices = [{"currency": "EUR", "tokenAmount": "4@GOLD"}]

        self.nodes[2].setoracledata(oracle_id2, timestamp, oracle2_prices)

        self.synchronize(node=2)

        # === set missing data ===
        price_feeds2 = [
            {"currency": "USD", "token": "PT"},
            {"currency": "EUR", "token": "PT"},
            {"currency": "EUR", "token": "GOLD"},
            {"currency": "USD", "token": "GOLD"}
        ]

        self.nodes[0].updateoracle(oracle_id2, oracle_address2, price_feeds2, 25)

        self.synchronize(node=0)

        # === we have no valid oracle values for PT#129 in EUR ===
        assert_raises_rpc_error(-1, 'no live oracles for specified request',
                                self.nodes[1].getprice, {"currency": "EUR", "token": "PT"})

        self.nodes[2].setoracledata(oracle_id2, timestamp, [
                                    {"currency":"USD", "tokenAmount":"10@GOLD"},
                                    {"currency":"EUR", "tokenAmount":"7@PT"}])

        token_prices1 = [
            {"currency":"USD", "tokenAmount":"11@GOLD"},
            {"currency":"EUR", "tokenAmount":"8@PT"},
            {"currency":"EUR", "tokenAmount":"10@GOLD"},
            {"currency":"USD", "tokenAmount":"7@PT"}
        ]

        self.nodes[2].setoracledata(oracle_id1, timestamp, token_prices1)

        self.synchronize(node=2)

        self.assert_compare_oracle_data(oracle_data_json=self.nodes[1].getoracledata(oracle_id1),
                                        oracle_id=oracle_id1,
                                        price_feeds=price_feeds1,
                                        token_prices=token_prices1)

        # === check expired price feed ===
        token_prices1 = [
            {"currency":"USD", "tokenAmount":"11@GOLD"},
            {"currency":"EUR", "tokenAmount":"8@PT"},
            {"currency":"EUR", "tokenAmount":"10@GOLD"},
            {"currency":"USD", "tokenAmount":"7@PT"}
        ]

        self.nodes[2].setoracledata(oracle_id1, timestamp - 7200, token_prices1)

        self.synchronize(node=2, full=True)

        pt_in_usd_raw_prices = self.nodes[1].listlatestrawprices({"currency":"USD", "token":"PT"})

        assert_equal(len(pt_in_usd_raw_prices), 1)
        assert_equal(pt_in_usd_raw_prices[0]['state'], 'expired')

        # === check date in range 0 -> now+300s (5 minutes) ===
        token_prices1 = [{"currency":"USD", "tokenAmount":"7@PT"}]

        future_timestamp = (calendar.timegm(time.gmtime()))+310 # add 5 minutes +10s for slow tests case
        assert_raises_rpc_error(-8, 'timestamp cannot be negative, zero or over 5 minutes in the future',
                                self.nodes[2].setoracledata, oracle_id1, future_timestamp, token_prices1)

        # === check for invalid oracle id
        assert_raises_rpc_error(-20, 'oracle <{}> not found'.format(invalid_oracle_id),
                                self.nodes[1].getoracledata, invalid_oracle_id)

        assert_raises_rpc_error(-32600, 'oracle <{}> not found'.format(invalid_oracle_id),
                                self.nodes[0].updateoracle,
                                invalid_oracle_id, oracle_address2, [{"currency":"USD", "token":"PT"}], 15)

        assert_raises_rpc_error(-32600, 'oracle <{}> not found'.format(invalid_oracle_id),
                                self.nodes[0].setoracledata,
                                invalid_oracle_id, timestamp, [{"currency":"USD", "tokenAmount":"10@PT"}])

        self.nodes[0].removeoracle(oracle_id1)
        self.nodes[0].removeoracle(oracle_id2)

        self.synchronize(node=0, full=True)


if __name__ == '__main__':
    OraclesTest().main()
