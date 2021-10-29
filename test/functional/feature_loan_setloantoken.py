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
import calendar
import time

class LoanSetLoanTokenTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=50', '-eunosheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(101)

        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': False,
                            'interest': 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("no live oracles for specified request" in errorString)

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': False,
                            'interest': -1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        try:
            self.nodes[0].setloantoken({
                            'symbol': "TSLAA",
                            'name': "Tesla stock token",
                            'fixedIntervalPriceId': "aa",
                            'mintable': False,
                            'interest': 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("price feed not in valid format - token/currency" in errorString)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
                            'symbol': "TSLAAAA",
                            'name': "Tesla",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': False,
                            'interest': 1})

        self.nodes[0].generate(1)

        loanTokens = self.nodes[0].listloantokens()

        assert_equal(len(loanTokens), 1)

        loanToken = self.nodes[0].getloantoken("TSLAAAA")
        tokenId = list(loanToken["token"])[0]
        assert_equal(loanToken["token"][tokenId]["symbol"], "TSLAAAA")
        assert_equal(loanToken["token"][tokenId]["name"], "Tesla")
        assert_equal(loanToken["token"][tokenId]["mintable"], False)
        assert_equal(loanToken["fixedIntervalPriceId"], "TSLA/USD")
        assert_equal(loanToken["interest"], Decimal('1'))

        self.nodes[0].updateloantoken("TSLAAAA",{
                            'symbol': "TSLA",
                            'name': "Tesla stock token",
                            'mintable': True,
                            'interest': 3})

        self.nodes[0].generate(1)

        loanTokens = self.nodes[0].listloantokens()
        assert_equal(len(loanTokens), 1)

        loanToken = self.nodes[0].getloantoken("TSLA")
        tokenId = list(loanToken["token"])[0]
        assert_equal(loanToken["token"][tokenId]["symbol"], "TSLA")
        assert_equal(loanToken["token"][tokenId]["name"], "Tesla stock token")
        assert_equal(loanToken["token"][tokenId]["mintable"], True)
        assert_equal(loanToken["fixedIntervalPriceId"], "TSLA/USD")
        assert_equal(loanToken["interest"], Decimal('3'))

        # cannot set too old timestamp
        try:
            self.nodes[0].setoracledata(oracle_id1, timestamp - 3600, oracle1_prices)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Timestamp" in errorString and "is out of price update window" in errorString)

        # Create loan token for DUSD/USD without Oracle
        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': False,
            'interest': 0})
        self.nodes[0].generate(1)

        # Update loan token for DUSD/USD without Oracle
        self.nodes[0].updateloantoken("DUSD",{
            'symbol': "DUSD",
            'name': "DUSD",
            'mintable': True,
            'interest': 0})

if __name__ == '__main__':
    LoanSetLoanTokenTest().main()
