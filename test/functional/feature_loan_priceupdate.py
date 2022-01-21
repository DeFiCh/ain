#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Active/Next price update."""

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import DefiTestFramework
from decimal import Decimal

from test_framework.util import assert_equal

import calendar
import time

class PriceUpdateTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(300)
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].createtoken({
            "symbol": "USD",
            "name": "USD token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)

        # mint BTC
        self.nodes[0].minttokens("2000@BTC")
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # transfer DFI
        self.nodes[0].utxostoaccount({account: "2000@DFI"})
        self.nodes[0].generate(1)

        # setup oracles
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [
            {"currency": "USD", "token": "TSLA"},
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"}]

        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        collTokenDFI_USD = {
            'token': "DFI",
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"}

        try:
            self.nodes[0].setcollateraltoken(collTokenDFI_USD)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("no live oracles for specified request" in errorString)

        # feed oracle
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "10@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        oracle2_prices = [
            {"currency": "USD", "tokenAmount": "20@DFI"},
            {"currency": "USD", "tokenAmount": "20@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken(collTokenDFI_USD)
        self.nodes[0].generate(1)

        collTokenBTC_USD = {
            'token': "BTC",
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"}
        self.nodes[0].setcollateraltoken(collTokenBTC_USD)
        self.nodes[0].generate(1)

        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN1')
        self.nodes[0].generate(1)

        # Create vault
        vaultId1 = self.nodes[0].createvault(account, 'LOAN1') # default loan scheme
        self.nodes[0].generate(6)

        self.nodes[0].deposittovault(vaultId1, account, '1000@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '1000@BTC')
        self.nodes[0].generate(1)

        # set TSLA as loan token
        loanTokenTSLA_USD = {
            'symbol': "TSLA",
            'name': "Tesla Token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1
        }
        try:
            self.nodes[0].setloantoken(loanTokenTSLA_USD)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("no live oracles for specified request" in errorString)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "20@TSLA"},
            {"currency": "USD", "tokenAmount": "20@DFI"},
            {"currency": "USD", "tokenAmount": "20@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        oracle2_prices = [
            {"currency": "USD", "tokenAmount": "10@TSLA"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken(loanTokenTSLA_USD)
        self.nodes[0].generate(1)

        try:
            self.nodes[0].takeloan({
                'vaultId': vaultId1,
                'amounts': "10@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No live fixed prices for TSLA/USD" in errorString)

        self.nodes[0].generate(5) # let price update

        loanAmount = 40
        self.nodes[0].takeloan({
            'vaultId': vaultId1,
            'amounts': str(loanAmount)+"@TSLA"})
        self.nodes[0].generate(1)
        takenLoanAmount = loanAmount
        try:
            fixedPrice = self.nodes[0].getfixedintervalprice("AAA/USD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("fixedIntervalPrice with id <AAA/USD> not found" in errorString)
        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        assert_equal(fixedPrice['activePrice'], Decimal(15.00000000))
        assert_equal(fixedPrice['nextPrice'], Decimal(15.00000000))
        assert_equal(fixedPrice['activePriceBlock'], 324)
        assert_equal(fixedPrice['nextPriceBlock'], 330)

        oracle2_prices = [
            {"currency": "USD", "tokenAmount": "15@TSLA"},
            {"currency": "USD", "tokenAmount": "15@DFI"},
            {"currency": "USD", "tokenAmount": "15@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(1)

        # Change price over deviation threshold to invalidate price
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "30@TSLA"},
            {"currency": "USD", "tokenAmount": "30@DFI"},
            {"currency": "USD", "tokenAmount": "30@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        # price is valid because it hasn't been updated yet
        loanAmount = 10
        self.nodes[0].takeloan({
            'vaultId': vaultId1,
            'amounts': str(loanAmount)+"@TSLA"})
        self.nodes[0].generate(2) # let price update to invalid state

        takenLoanAmount += loanAmount
        vault = self.nodes[0].getvault(vaultId1)
        assert_equal(vault["state"], "frozen")
        try:
            self.nodes[0].takeloan({
                'vaultId': vaultId1,
                'amounts': "10@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot take loan while any of the asset's price in the vault is not live" in errorString)

        try:
            self.nodes[0].withdrawfromvault(vaultId1, account, "100@DFI")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot withdraw from vault while any of the asset's price is invalid" in errorString)

        try:
            self.nodes[0].paybackloan({
                    'vaultId': vaultId1,
                    'from': account,
                    'amounts': ["0.5@TSLA"]})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot payback loan while any of the asset's price is invalid" in errorString)


        account2 = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].generate(1)
        params = {'ownerAddress': account2}

        try:
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot update vault while any of the asset's price is invalid" in errorString)

        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        assert_equal(fixedPrice['isLive'], False)
        assert_equal(fixedPrice['activePrice'], Decimal('15.00000000'))
        assert_equal(fixedPrice['nextPrice'], Decimal('22.50000000'))


        self.nodes[0].generate(5) # let price update to valid state
        self.nodes[0].updatevault(vaultId1, params)

        self.nodes[0].generate(1)
        vault = self.nodes[0].getvault(vaultId1)
        assert_equal(vault["ownerAddress"], account2)

        loanAmount = 30
        self.nodes[0].takeloan({
            'vaultId': vaultId1,
            'amounts': str(loanAmount)+ "@TSLA"})
        self.nodes[0].generate(1)
        takenLoanAmount += loanAmount

        try:
            self.nodes[0].withdrawfromvault(vaultId1, account, "900@DFI")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the collateral must be in DFI" in errorString)

        vault = self.nodes[0].getvault(vaultId1)
        self.nodes[0].withdrawfromvault(vaultId1, account, "100@BTC")
        self.nodes[0].generate(1)

        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        assert_equal(fixedPrice['isLive'], True)
        assert_equal(fixedPrice['activePrice'], Decimal('22.50000000'))
        assert_equal(fixedPrice['nextPrice'], Decimal('22.50000000'))

        vault = self.nodes[0].getvault(vaultId1)
        assert_equal(vault["collateralAmounts"][1], '900.00000000@BTC')
        interest_TSLA = self.nodes[0].getinterest('LOAN1')[0]["totalInterest"]
        totalLoanAmount = takenLoanAmount + interest_TSLA
        assert_equal(vault["loanAmounts"][0], str(totalLoanAmount)+"@TSLA")

        height = self.nodes[0].getblockcount()
        assert_equal(height, 340)
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "100@TSLA"},
            {"currency": "USD", "tokenAmount": "100@DFI"},
            {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(3)
        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        assert_equal(fixedPrice['isLive'], False)
        assert_equal(fixedPrice['activePrice'], Decimal('22.50000000'))
        assert_equal(fixedPrice['nextPrice'], Decimal('57.50000000'))
        assert_equal(fixedPrice['nextPriceBlock'], 348)
        assert_equal(fixedPrice['activePriceBlock'], 342)
        self.nodes[0].generate(6)
        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        assert_equal(fixedPrice['isLive'], True)
        assert_equal(fixedPrice['activePrice'], Decimal('57.50000000'))
        assert_equal(fixedPrice['nextPrice'], Decimal('57.50000000'))
        assert_equal(fixedPrice['nextPriceBlock'], 354)
        assert_equal(fixedPrice['activePriceBlock'], 348)

        fixedPriceList = self.nodes[0].listfixedintervalprices()
        assert_equal(len(fixedPriceList), 3)

        pagination = {"start": "DFI/USD"}
        fixedPriceList = self.nodes[0].listfixedintervalprices(pagination)
        assert_equal(len(fixedPriceList), 2)



if __name__ == '__main__':
    PriceUpdateTest().main()
