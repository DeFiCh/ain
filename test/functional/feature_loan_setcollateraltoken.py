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
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fhardforkheight=50', '-eunosheight=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fhardforkheight=50', '-eunosheight=1', '-txindex=1']]

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

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].setcollateraltoken({
                            'token': "DOGE",
                            'factor': 1,
                            'priceFeedId': oracle_id1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token DOGE does not exist!" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 2,
                            'priceFeedId': oracle_id1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("setCollateralToken factor must be lower or equal than 1!" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': -1,
                            'priceFeedId': oracle_id1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        try:
            self.nodes[0].setcollateraltoken({
                            'token': idDFI,
                            'factor': 1,
                            'priceFeedId': "76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("oracle (76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d) does not exist!" in errorString)

        collTokenTx1 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 0.5,
                                    'priceFeedId': oracle_id1})

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 1)
        assert_equal(collTokens[collTokenTx1]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx1]["factor"], Decimal('0.5'))
        assert_equal(collTokens[collTokenTx1]["priceFeedId"], oracle_id1)

        collTokenTx2 = self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})

        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 2)
        assert_equal(collTokens[collTokenTx2]["token"], symbolBTC)
        assert_equal(collTokens[collTokenTx2]["factor"], Decimal('1'))
        assert_equal(collTokens[collTokenTx2]["priceFeedId"], oracle_id1)

        collTokenTx3 = self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})


        self.nodes[0].generate(1)
        self.sync_blocks()

        collTokens = self.nodes[0].listcollateraltokens()

        assert_equal(len(collTokens), 2)
        assert_equal(collTokens[collTokenTx3]["token"], symbolDFI)
        assert_equal(collTokens[collTokenTx3]["factor"], Decimal('1'))

        assert(False)

if __name__ == '__main__':
    LoanSetCollateralTokenTest().main()
