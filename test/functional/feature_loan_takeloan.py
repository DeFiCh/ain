#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - setcollateraltoken."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

import calendar
import time
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

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        account1 = self.nodes[1].get_genesis_keys().ownerAuthAddress

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        symboldUSD = "dUSD"
        symbolTSLA = "TSLA"
        symbolGOOGL = "GOOGL"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].createtoken({
            "symbol": symboldUSD,
            "name": "Theter USD",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        iddUSD = list(self.nodes[0].gettoken(symboldUSD).keys())[0]

        self.nodes[0].minttokens("10000@"+ symboldUSD)

        self.nodes[0].generate(1)
        self.sync_blocks()

        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # create pool USDT-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "dUSD-DFI",
        }, [])

        self.nodes[0].utxostoaccount({account0: "1000@" + symbolDFI})
        self.nodes[1].utxostoaccount({account1: "100@" + symbolDFI})

        self.nodes[0].generate(1)
        self.sync_blocks()

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "BTC"},
                        {"currency": "USD", "token": "TSLA"},
                        {"currency": "USD", "token": "GOOGL"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)
        self.sync_blocks()

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                          {"currency": "USD", "tokenAmount": "10@GOOGL"},
                          {"currency": "USD", "tokenAmount": "10@DFI"},
                          {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(60) # let fixed price update
        self.sync_blocks()

        # transfer
        self.nodes[0].addpoolliquidity({
            account0: ["300@" + symboldUSD, "100@" + symbolDFI]
        }, account0, [])
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})

        setLoanTokenTSLA = self.nodes[0].setloantoken({
                                    'symbol': symbolTSLA,
                                    'name': "Tesla stock token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': False,
                                    'interest': 1})

        setLoanTokenGOOGL = self.nodes[0].setloantoken({
                                    'symbol': symbolGOOGL,
                                    'name': "Tesla stock token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': True,
                                    'interest': 2})

        self.nodes[0].createloanscheme(150, 5, 'LOAN150')

        self.nodes[0].generate(1)
        self.sync_blocks()

        loantokens = self.nodes[0].listloantokens()

        assert_equal(len(loantokens), 2)
        idTSLA = list(loantokens[setLoanTokenTSLA]["token"])[0]
        idGOOGL = list(loantokens[setLoanTokenGOOGL]["token"])[0]

        vaultId = self.nodes[0].createvault( account0, 'LOAN150')

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].deposittovault(vaultId, account0, "200@DFI")

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].takeloan({
                    'vaultId': setLoanTokenTSLA,
                    'amounts': "1@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault <{}> not found".format(setLoanTokenTSLA) in errorString)

        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + symbolBTC})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan token with id (" + idBTC + ") does not exist" in errorString)

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
                    'amounts': "1@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for" in errorString)

        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan cannot be taken on token with id (" + idTSLA + ") as \"mintable\" is currently false" in errorString)

        setLoanTokenTSLA = self.nodes[0].updateloantoken(idTSLA,{
                                    'mintable': True})

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].minttokens(["1@" + symbolTSLA, "1@" + symbolGOOGL])

        self.nodes[0].generate(1)
        self.sync_blocks()

        # create pool TSLA
        self.nodes[0].createpoolpair({
            "tokenA": idTSLA,
            "tokenB": symboldUSD,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "TSLA-dUSD",
        }, [])

        # create pool GOOGL
        self.nodes[0].createpoolpair({
            "tokenA": idGOOGL,
            "tokenB": symboldUSD,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "GOOGL-dUSD",
        }, [])

        self.nodes[0].generate(1)
        self.sync_blocks()

        # transfer
        self.nodes[0].addpoolliquidity({account0: ["1@" + symbolTSLA, "300@" + symboldUSD]}, account0, [])
        self.nodes[0].addpoolliquidity({account0: ["1@" + symbolGOOGL, "400@" + symboldUSD]}, account0, [])

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': ["1@" + symbolTSLA, "2@" + symbolGOOGL]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('1'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idGOOGL], Decimal('2'))

        interest = self.nodes[0].getinterest('LOAN150', symbolTSLA)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000114'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000114'))

        interest = self.nodes[0].getinterest('LOAN150', symbolGOOGL)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000133'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000133'))

        loans = self.nodes[0].getloaninfo()

        assert_equal(loans['collateralValueUSD'], Decimal('2000.00000000'))
        assert_equal(loans['loanValueUSD'], Decimal('30.00003800'))

        vaultId1 = self.nodes[1].createvault( account1, 'LOAN150')

        self.nodes[1].generate(2)
        self.sync_blocks()

        interest = self.nodes[0].getinterest('LOAN150', symbolTSLA)[0]

        assert_equal(interest['totalInterest'], 3 * Decimal('0.00000114'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000114'))

        interest = self.nodes[0].getinterest('LOAN150', symbolGOOGL)[0]

        assert_equal(interest['totalInterest'], 3 * Decimal('0.00000133'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000133'))

        loans = self.nodes[0].getloaninfo()

        assert_equal(loans['collateralValueUSD'], Decimal('2000.00000000'))
        assert_equal(loans['loanValueUSD'], Decimal('30.00011400'))
        try:
            self.nodes[0].loanpayback({
                        'vaultId': setLoanTokenTSLA,
                        'from': account0,
                        'amounts': "0.5@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing vault with id" in errorString)

        try:
            self.nodes[0].loanpayback({
                        'vaultId': vaultId1,
                        'from': account0,
                        'amounts': "0.5@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault with id " + vaultId1 + " has no collaterals" in errorString)

        self.nodes[1].deposittovault(vaultId1, account1, "100@" + symbolDFI)

        self.nodes[1].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].loanpayback({
                        'vaultId': vaultId1,
                        'from': account0,
                        'amounts': "0.5@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("There are no loans on this vault" in errorString)

        self.nodes[0].loanpayback({
                    'vaultId': vaultId,
                    'from': account0,
                    'amounts': ["0.5@" + symbolTSLA, "1@" + symbolGOOGL]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].listaccounthistory(account0)[0]['amounts'].sort(), ['-1.00000000@GOOGL', '-0.50000000@TSLA'].sort())
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('0.5'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idGOOGL], Decimal('1'))

        # loan payback burn
        vaultInfo = self.nodes[0].getvault(vaultId)

        assert_equal(self.nodes[0].getburninfo()['paybackburn'], Decimal('0.00116798'))
        assert_equal(vaultInfo['loanAmount'].sort(), ['0.50000513@' + symbolTSLA, '1.00001197@' + symbolGOOGL].sort())

        loans = self.nodes[0].getloaninfo()

        assert_equal(loans['collateralValueUSD'], Decimal('3000.00000000'))
        assert_equal(loans['loanValueUSD'], Decimal('15.00017100'))

if __name__ == '__main__':
    LoanTakeLoanTest().main()
