#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

"""Test operator RPC.
verify createoperator, updateoperator RPCs
"""
from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import decimal
import math
import calendar
import time
import json
import functools

class OperatorTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fupgradeheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fupgradeheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fupgradeheight=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fupgradeheight=1']]

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

    def synchronize(self, node: int, full: bool = False):
        self.nodes[node].generate(1)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])
        if full:
            self.sync_mempools([self.nodes[0], self.nodes[1], self.nodes[2]])

    def run_test(self):
        self.nodes[0].generate(200)
        self.sync_all()

        # create operator addresses 
        operator_address1 = self.nodes[1].getnewaddress("", "legacy")
        operator_address2 = self.nodes[1].getnewaddress("", "legacy")

        assert (self.find_address_tx(self.nodes[0], operator_address1) is None)
        assert (self.find_address_tx(self.nodes[0], operator_address1) is None)

        # supply operators with money for transactions
        self.nodes[0].sendtoaddress(operator_address1, 50)
        self.nodes[0].sendtoaddress(operator_address2, 50)

        self.synchronize(0, full=True)
        self.synchronize(1, full=True)

        send_money_tx1, send_money_tx2 = \
            [self.find_address_tx(self.nodes[1], address) for address in [operator_address1, operator_address2]]

        assert (send_money_tx1 is not None)
        assert (send_money_tx2 is not None)

        assert (send_money_tx1['amount'] == decimal.Decimal(50))
        assert (send_money_tx2['amount'] == decimal.Decimal(50))

        balance = self.nodes[1].getbalances()

        # create operator from node 1
        operator_id1 = self.nodes[1].createoperator(operator_address1, "operator1", "operator1url", "DRAFT") #DRAFT

        self.synchronize(node=1)
        
        # update operator operator_id1 from node 1
        self.nodes[1].updateoperator(operator_id1, "operator1", "operator1urlnew", "ACTIVE") #ACTIVE

        self.synchronize(node=1)

        #try to update the operator from node 0. should return an error
        assert_raises_rpc_error(-5, "Incorrect authorization for",
                                self.nodes[0].updateoperator, operator_id1, "operator1", "operator1urlnew2", "ACTIVE")

if __name__ == '__main__':
    OperatorTest().main()
