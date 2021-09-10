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

class LoanTakeLoanTest (DefiTestFramework):
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

        self.nodes[0].generate(10)
        self.sync_blocks()

        symbolDFI = "DFI"
        symbolBTC = "BTC"

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        account1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.nodes[0].utxostoaccount({account0: "1000@" + symbolDFI})
        self.nodes[1].utxostoaccount({account1: "100@" + symbolDFI})

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"},{"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].setoracledata(oracle_id1, 1612237937,
                                      [{'currency': "USD",
                                       'tokenAmount': "2.8553@DFI"},
                                       {'currency': "USD",
                                       'tokenAmount': "48523@BTC"},
                                       {'currency': "USD",
                                       'tokenAmount': "300@TSLA"},
                                      ])

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})

        setLoanTokenTSLA = self.nodes[0].setloantoken({
                                    'symbol': "TSLA",
                                    'name': "Tesla stock token",
                                    'priceFeedId': oracle_id1,
                                    'mintable': False,
                                    'interest': 1})

        self.nodes[0].createloanscheme(150, 0.05, 'LOAN150')

        self.nodes[0].generate(1)
        self.sync_blocks()

        loantokens = self.nodes[0].listloantokens()

        assert_equal(len(loantokens), 1)
        idTSLA = list(loantokens[setLoanTokenTSLA]["token"])[0]

        vaultId = self.nodes[0].createvault( account0, 'LOAN150')

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].deposittovault(vaultId, account0, "200@DFI")

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].takeloan({
                    'vaultId': setLoanTokenTSLA,
                    'amounts': "1@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing vault with id" in errorString)


        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@BTC"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan token with id (1) does not exist" in errorString)

        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@AAAA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid Defi token: AAAA" in errorString)

        try:
            self.nodes[1].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for" in errorString)

        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan cannot be taken on token with id (2) as \"mintable\" is currently false" in errorString)

        setLoanTokenTSLA = self.nodes[0].updateloantoken('2',{
                                    'mintable': True})

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "2@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault does not have enough collateralization ratio defined by loan scheme" in errorString)

        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@TSLA"})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('1'))

        interest = self.nodes[0].getinterest('LOAN150')[0]

        assert_equal(interest['totalInterest'], Decimal('0.00001997'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00001997'))

        loans = self.nodes[0].getloaninfo()

        assert_equal(loans['collateralValueUSD'], Decimal('571.06000000'))
        assert_equal(loans['loanValueUSD'], Decimal('300.00599100'))

        vaultId1 = self.nodes[1].createvault( account1, 'LOAN150')

        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[1].deposittovault(vaultId1, account1, "100@DFI")

        self.nodes[1].generate(1)
        self.sync_blocks()

        interest = self.nodes[0].getinterest('LOAN150', 'TSLA')[0]

        assert_equal(interest['totalInterest'], 3 * Decimal('0.00001997'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00001997'))

        loans = self.nodes[0].getloaninfo()

        assert_equal(loans['collateralValueUSD'], Decimal('856.59000000'))
        assert_equal(loans['loanValueUSD'], Decimal('300.01797300'))

if __name__ == '__main__':
    LoanTakeLoanTest().main()
