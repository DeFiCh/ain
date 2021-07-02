#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan Scheme."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class CreateLoanSchemeTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=110']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Try and create a loan scheme before Fort Canning
        try:
            self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("called before FortCanning height" in errorString)

        self.nodes[0].generate(9)

        # Create loan scheme
        self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        self.nodes[0].generate(1)

        # Check loan scheme created
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['identifier'], 'LOANMAX')
        assert_equal(results[0]['ratio'], 1000)
        assert_equal(results[0]['rate'], Decimal('0.50000000'))

        # Try and create a loan scheme with duplicate name
        try:
            self.nodes[0].createloanscheme(100, 1, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme already exist with identifier LOANMAX" in errorString)

        # Try and create a loan scheme with duplicate ratio and rate
        try:
            self.nodes[0].createloanscheme(1000, 0.5, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme with same rate and ratio already exists" in errorString)

        # Try and create a loan scheme with too small ratio
        try:
            self.nodes[0].createloanscheme(99, 0.5, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Ratio cannot be less than 100" in errorString)

        # Try and create a loan scheme with too small rate
        try:
            self.nodes[0].createloanscheme(1000, 0.009, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Rate cannot be less than 0.01" in errorString)

        # Create more loan schemes
        self.nodes[0].createloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].createloanscheme(175, 3, 'LOAN0002')
        self.nodes[0].createloanscheme(200, 2, 'LOAN0003')
        self.nodes[0].createloanscheme(350, 1.5, 'LOAN0004')
        self.nodes[0].createloanscheme(500, 1, 'LOAN0005')
        self.nodes[0].generate(1)

        # Check all loan schemes created and shown in order
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['identifier'], 'LOAN0001')
        assert_equal(results[0]['ratio'], 150)
        assert_equal(results[0]['rate'], Decimal('5.00000000'))
        assert_equal(results[1]['identifier'], 'LOAN0002')
        assert_equal(results[1]['ratio'], 175)
        assert_equal(results[1]['rate'], Decimal('3.00000000'))
        assert_equal(results[2]['identifier'], 'LOAN0003')
        assert_equal(results[2]['ratio'], 200)
        assert_equal(results[2]['rate'], Decimal('2.00000000'))
        assert_equal(results[3]['identifier'], 'LOAN0004')
        assert_equal(results[3]['ratio'], 350)
        assert_equal(results[3]['rate'], Decimal('1.50000000'))
        assert_equal(results[4]['identifier'], 'LOAN0005')
        assert_equal(results[4]['ratio'], 500)
        assert_equal(results[4]['rate'], Decimal('1.00000000'))
        assert_equal(results[5]['identifier'], 'LOANMAX')
        assert_equal(results[5]['ratio'], 1000)
        assert_equal(results[5]['rate'], Decimal('0.50000000'))

if __name__ == '__main__':
    CreateLoanSchemeTest().main()
