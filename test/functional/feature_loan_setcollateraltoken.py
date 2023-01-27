#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - setcollateraltoken."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error

from decimal import Decimal
import calendar
import time

class LoanSetCollateralTokenTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-fortcanningheight=50', '-fortcanninghillheight=50', '-fortcanningcrunchheight=150', '-txindex=1']]

    @DefiTestFramework.rollback
    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(101)

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        symbolGOOGL = "GOOGL"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": symbolBTC,
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": symbolGOOGL,
            "name": symbolGOOGL,
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idGOOGL = list(self.nodes[0].gettoken(symbolGOOGL).keys())[0]

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
            {"currency": "USD", "token": symbolDFI},
            {"currency": "USD", "token": symbolBTC},
            {"currency": "USD", "token": symbolGOOGL},
        ]
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
            {"currency": "USD", "tokenAmount": f'1@{symbolDFI}'},
            {"currency": "USD", "tokenAmount": f'1@{symbolBTC}'},
            {"currency": "USD", "tokenAmount": f'1@{symbolGOOGL}'},
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600, "setCollateralToken factor must be lower or equal than 1", self.nodes[0].setcollateraltoken, {
            'token': idDFI,
            'factor': 2,
            'fixedIntervalPriceId': "DFI/USD"})

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

        dfi_activation_height = self.nodes[0].getblockcount()

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

        btc_activation_height = self.nodes[0].getblockcount()

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
        assert_equal(collTokens["activateAfterBlock"], dfi_activation_height)

        collTokens = self.nodes[0].getcollateraltoken(idBTC)

        assert_equal(collTokens["token"], symbolBTC)
        assert_equal(collTokens["factor"], Decimal('0.9'))
        assert_equal(collTokens["activateAfterBlock"], btc_activation_height)

        self.nodes[0].generate(30)

        collTokens = self.nodes[0].getcollateraltoken(idDFI)

        assert_equal(collTokens["token"], symbolDFI)
        assert_equal(collTokens["factor"], Decimal('1'))
        assert_equal(collTokens["activateAfterBlock"], 135)

        collTokens = self.nodes[0].getcollateraltoken(idBTC)

        assert_equal(collTokens["token"], symbolBTC)
        assert_equal(collTokens["factor"], Decimal('0.9'))
        assert_equal(collTokens["activateAfterBlock"], btc_activation_height)

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        collTokens = self.nodes[0].listcollateraltokens()
        assert_equal(len(collTokens), 4)

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Check errors on FCC
        assert_raises_rpc_error(-32600, "setCollateralToken factor must be lower or equal than 1.00000000", self.nodes[0].setcollateraltoken, {
            'token': idDFI,
            'factor': 1.01,
            'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].generate(1)

        # Check errors
        assert_raises_rpc_error(-32600, "Percentage exceeds 100%", self.nodes[0].setcollateraltoken, {
            'token': idDFI,
            'factor': 1.01,
            'fixedIntervalPriceId': "DFI/USD"})

        # Create collateral token
        self.nodes[0].setcollateraltoken({
            'token': idGOOGL,
            'factor': 0.12345678,
            'fixedIntervalPriceId': "GOOGL/USD"})
        self.nodes[0].generate(1)

        # Check attributess
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result[f'v0/token/{idGOOGL}/loan_collateral_enabled'], 'true')
        assert_equal(result[f'v0/token/{idGOOGL}/loan_collateral_factor'], '0.12345678')
        assert_equal(result[f'v0/token/{idGOOGL}/fixed_interval_price_id'], 'GOOGL/USD')

        # Get token creation TX
        token = self.nodes[0].gettoken(idGOOGL)[idGOOGL]

        # Check entry in list collateral tokens
        result = self.nodes[0].listcollateraltokens()[2]
        assert_equal(result['token'], 'GOOGL')
        assert_equal(result['tokenId'], token['creationTx'])
        assert_equal(result['factor'], Decimal('0.12345678'))
        assert_equal(result['fixedIntervalPriceId'], 'GOOGL/USD')

if __name__ == '__main__':
    LoanSetCollateralTokenTest().main()
