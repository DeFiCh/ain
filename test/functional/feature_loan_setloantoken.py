#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - setloantoken."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

from decimal import Decimal

class LoanSetLoanTokenTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=50', '-eunosheight=50', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=50', '-eunosheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(25)
        self.sync_blocks()
        self.nodes[1].generate(100)
        self.sync_blocks()

        tokenTxid = self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'priceFeedId': "TSLA/USD",
                            'mintable': False,
                            'interest': 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("oracle (" + tokenTxid + ") does not exist" in errorString)

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'priceFeedId': "TSLA/USD",
                            'mintable': False,
                            'interest': -1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLAA",
                            'name': "Tesla stock token",
                            'priceFeedId': "",
                            'mintable': False,
                            'interest': 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("price feed not in valid format - token/currency" in errorString)

        setLoanTokenTx = self.nodes[0].setloantoken({
                            'symbol': "TSLAAAA",
                            'name': "Tesla",
                            'priceFeedId': "TSLA/USD",
                            'mintable': False,
                            'interest': 1})

        self.nodes[0].generate(1)
        self.sync_blocks()

        loantokens = self.nodes[0].listloantokens()

        assert_equal(len(loantokens), 1)
        tokenId = list(loantokens[setLoanTokenTx]["token"])[0]
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["symbol"], "TSLAAAA")
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["name"], "Tesla")
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["mintable"], False)
        assert_equal(loantokens[setLoanTokenTx]["priceFeedId"], "TSLA/USD")
        assert_equal(loantokens[setLoanTokenTx]["interest"], Decimal('1'))

        self.nodes[0].updateloantoken("TSLAAAA",{
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'mintable': True,
                            'interest': 3})

        self.nodes[0].generate(1)
        self.sync_blocks()

        loantokens = self.nodes[0].listloantokens()

        assert_equal(len(loantokens), 1)
        tokenId = list(loantokens[setLoanTokenTx]["token"])[0]
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["symbol"], "TSLA")
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["name"], "Tesla stock token")
        assert_equal(loantokens[setLoanTokenTx]["token"][tokenId]["mintable"], True)
        assert_equal(loantokens[setLoanTokenTx]["priceFeedId"], "TSLA/USD")
        assert_equal(loantokens[setLoanTokenTx]["interest"], Decimal('3'))

if __name__ == '__main__':
    LoanSetLoanTokenTest().main()
