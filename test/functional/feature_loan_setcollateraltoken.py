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

class LoanSetCollateralTokenTest (DefiTestFramework):
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

        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        symbolDFI = "DFI"
        symbolBTC = "BTC"

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        try:
            self.nodes[0].setcollateraltoken({
                            'token': "DOGE",
                            'factor': 1,
                            'priceFeedId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token DOGE does not exist" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 2,
                            'priceFeedId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("setCollateralToken factor must be lower or equal than 1" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': -1,
                            'priceFeedId': "DFI/USD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 1,
                            'priceFeedId': "Blabla"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("price feed not in valid format - token/currency" in errorString)

        collTokenTx1 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 0.5,
                                    'priceFeedId': "DFI/USD"})

        collTokenTx3 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'priceFeedId': "DFI/USD",
                                    'activateAfterBlock': 135})

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 2)
        assert_equal(collTokens[collTokenTx1]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx1]["factor"], Decimal('0.5'))
        assert_equal(collTokens[collTokenTx1]["priceFeedId"], "DFI/USD")

        collTokenTx2 = self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0.9,
                                    'priceFeedId': "BTC/USD"})

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 3)
        assert_equal(collTokens[collTokenTx2]["token"], symbolBTC)
        assert_equal(collTokens[collTokenTx2]["factor"], Decimal('0.9'))
        assert_equal(collTokens[collTokenTx2]["priceFeedId"], "BTC/USD")

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 3)
        assert_equal(collTokens[collTokenTx3]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx3]["factor"], Decimal('1'))

        collTokens = self.nodes[0].getcollateraltoken()
        assert_equal(len(collTokens), 2)

        collTokens = self.nodes[0].getcollateraltoken({'token': idDFI})

        assert_equal(collTokens[collTokenTx1]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx1]["factor"], Decimal('0.5'))
        assert_equal(collTokens[collTokenTx1]["activateAfterBlock"], 128)

        collTokens = self.nodes[0].getcollateraltoken({'token': idBTC})

        assert_equal(collTokens[collTokenTx2]["token"], symbolBTC)
        assert_equal(collTokens[collTokenTx2]["factor"], Decimal('0.9'))
        assert_equal(collTokens[collTokenTx2]["activateAfterBlock"], 129)

        self.nodes[0].generate(6)
        self.sync_blocks()

        collTokens = self.nodes[0].getcollateraltoken()
        assert_equal(len(collTokens), 2)

        collTokens = self.nodes[0].getcollateraltoken({'token': idDFI})

        assert_equal(collTokens[collTokenTx3]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx3]["factor"], Decimal('1'))
        assert_equal(collTokens[collTokenTx3]["activateAfterBlock"], 135)

        collTokens = self.nodes[0].getcollateraltoken({'token': idBTC})

        assert_equal(collTokens[collTokenTx2]["token"], symbolBTC)
        assert_equal(collTokens[collTokenTx2]["factor"], Decimal('0.9'))
        assert_equal(collTokens[collTokenTx2]["activateAfterBlock"], 129)

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0,
                                    'priceFeedId': "BTC/USD"})

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].getcollateraltoken()
        assert_equal(len(collTokens), 1)

if __name__ == '__main__':
    LoanSetCollateralTokenTest().main()
