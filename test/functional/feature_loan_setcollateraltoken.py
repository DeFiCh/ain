#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - setcollateraltoken."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

from decimal import Decimal
import calendar
import time

class LoanSetCollateralTokenTest (DefiTestFramework):
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

        symbolDFI = "DFI"
        symbolBTC = "BTC"

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        try:
            self.nodes[0].setcollateraltoken({
                            'token': "DOGE",
                            'factor': 1,
                            'fixedIntervalPriceId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token DOGE does not exist" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 1,
                            'fixedIntervalPriceId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Price feed DFI/USD does not belong to any oracle" in errorString)

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 1,
                            'fixedIntervalPriceId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("no live oracles for specified request" in errorString)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 2,
                            'fixedIntervalPriceId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("setCollateralToken factor must be lower or equal than 1" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': -1,
                            'fixedIntervalPriceId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 1,
                            'fixedIntervalPriceId': "Blabla"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("price feed not in valid format - token/currency" in errorString)

        collTokenTx1 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 0.5,
                                    'fixedIntervalPriceId': "DFI/USD"})

        collTokenTx3 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD",
                                    'activateAfterBlock': 135})

        self.nodes[0].generate(1)

        collTokens = self.nodes[0].listcollateraltokens()
        assert_equal(len(collTokens), 2)

        collToken1 = [token for token in collTokens if token["tokenId"] == collTokenTx1][0]
        assert_equal(collToken1["token"], symbolDFI)
        assert_equal(collToken1["factor"], Decimal('0.5'))
        assert_equal(collToken1["fixedIntervalPriceId"], "DFI/USD")

        collTokenTx2 = self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0.9,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        collTokens = self.nodes[0].listcollateraltokens()
        assert_equal(len(collTokens), 3)

        collToken2 = [token for token in collTokens if token["tokenId"] == collTokenTx2][0]
        assert_equal(collToken2["token"], symbolBTC)
        assert_equal(collToken2["factor"], Decimal('0.9'))
        assert_equal(collToken2["fixedIntervalPriceId"], "BTC/USD")

        self.nodes[0].generate(1)

        collTokens = self.nodes[0].listcollateraltokens()
        assert_equal(len(collTokens), 3)

        collToken3 = [token for token in collTokens if token["tokenId"] == collTokenTx3][0]
        assert_equal(collToken3["token"], symbolDFI)
        assert_equal(collToken3["factor"], Decimal('1'))

        collTokens = self.nodes[0].getcollateraltoken(idDFI)

        assert_equal(collTokens["token"], symbolDFI)
        assert_equal(collTokens["factor"], Decimal('0.5'))
        assert_equal(collTokens["activateAfterBlock"], 105)

        collTokens = self.nodes[0].getcollateraltoken(idBTC)

        assert_equal(collTokens["token"], symbolBTC)
        assert_equal(collTokens["factor"], Decimal('0.9'))
        assert_equal(collTokens["activateAfterBlock"], 106)

        self.nodes[0].generate(30)

        collTokens = self.nodes[0].getcollateraltoken(idDFI)

        assert_equal(collTokens["token"], symbolDFI)
        assert_equal(collTokens["factor"], Decimal('1'))
        assert_equal(collTokens["activateAfterBlock"], 135)

        collTokens = self.nodes[0].getcollateraltoken(idBTC)

        assert_equal(collTokens["token"], symbolBTC)
        assert_equal(collTokens["factor"], Decimal('0.9'))
        assert_equal(collTokens["activateAfterBlock"], 106)

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        collTokens = self.nodes[0].listcollateraltokens()
        assert_equal(len(collTokens), 4)

if __name__ == '__main__':
    LoanSetCollateralTokenTest().main()
