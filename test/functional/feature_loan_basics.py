#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - setcollateraltoken."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than_or_equal

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
        symboldUSD = "DUSD"
        symbolTSLA = "TSLA"
        symbolGOOGL = "GOOGL"

        loans = self.nodes[0].getloaninfo()
        assert_equal(loans['totals']['schemes'], 0)
        assert_equal(loans['totals']['collateraltokens'], 0)
        assert_equal(loans['totals']['loantokens'], 0)

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
            "pairSymbol": "DUSD-DFI",
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
        self.nodes[0].generate(1)
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

        self.nodes[0].setloantoken({
                                    'symbol': symbolGOOGL,
                                    'name': "Tesla stock token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': True,
                                    'interest': 2})

        self.nodes[0].createloanscheme(150, 5, 'LOAN150')

        self.nodes[0].generate(7)
        self.sync_blocks()

        loans = self.nodes[0].getloaninfo()
        assert_equal(loans['totals']['schemes'], 1)
        assert_equal(loans['totals']['collateraltokens'], 2)
        assert_equal(loans['totals']['loantokens'], 2)

        loantokens = self.nodes[0].listloantokens()

        assert_equal(len(loantokens), 2)
        idTSLA = list(self.nodes[0].getloantoken(symbolTSLA)["token"])[0]
        idGOOGL = list(self.nodes[0].getloantoken(symbolGOOGL)["token"])[0]

        vaultId = self.nodes[0].createvault( account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].deposittovault(vaultId, account0, "200@DFI")
        self.nodes[0].generate(1)

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
            "pairSymbol": "TSLA-DUSD",
        }, [])

        # create pool GOOGL
        self.nodes[0].createpoolpair({
            "tokenA": idGOOGL,
            "tokenB": symboldUSD,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "GOOGL-DUSD",
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

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(sorted(vaultInfo['loanAmounts']), sorted(['1.00000114@' + symbolTSLA, '2.00000266@' + symbolGOOGL]))
        assert_equal(sorted(vaultInfo['interestAmounts']), sorted(['0.00000266@GOOGL','0.00000114@TSLA']))
        assert_equal(vaultInfo['interestValue'], Decimal('0.00003800'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('1'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idGOOGL], Decimal('2'))

        interest = self.nodes[0].getinterest('LOAN150', symbolTSLA)[0]
        assert_equal(interest['totalInterest'], Decimal('0.00000114'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000114'))

        interest = self.nodes[0].getinterest('LOAN150', symbolGOOGL)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000266'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000266'))

        loans = self.nodes[0].getloaninfo()
        assert_equal(loans['totals']['schemes'], 1)
        assert_equal(loans['totals']['collateraltokens'], 2)
        assert_equal(loans['totals']['loantokens'], 2)

        vaultId1 = self.nodes[1].createvault( account1, 'LOAN150')

        self.nodes[1].generate(2)
        self.sync_blocks()

        interest = self.nodes[0].getinterest('LOAN150', symbolTSLA)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000342'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000114'))

        interest = self.nodes[0].getinterest('LOAN150', symbolGOOGL)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000798'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000266'))


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

        for interest in self.nodes[0].getinterest('LOAN150'):
            if interest['token'] == symbolTSLA:
                assert_equal(interest['totalInterest'], Decimal('0.00000456'))
            elif interest['token'] == symbolGOOGL:
                assert_equal(interest['totalInterest'], Decimal('0.00001064'))

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(sorted(vaultInfo['loanAmounts']), sorted(['1.00000456@' + symbolTSLA, '2.00001064@' + symbolGOOGL]))
        assert_equal(vaultInfo['interestValue'], Decimal('0.00015200'))
        assert_equal(sorted(vaultInfo['interestAmounts']), sorted(['0.00001064@GOOGL','0.00000456@TSLA']))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('1.00000000'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idGOOGL], Decimal('2.00000000'))

        self.nodes[0].loanpayback({
                    'vaultId': vaultId,
                    'from': account0,
                    'amounts': ["0.50000456@" + symbolTSLA, "1.00001064@" + symbolGOOGL]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(sorted(self.nodes[0].listaccounthistory(account0)[0]['amounts']), sorted(['-1.00001064@GOOGL', '-0.50000456@TSLA']))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idTSLA], Decimal('0.49999544'))
        assert_equal(self.nodes[0].getaccount(account0, {}, True)[idGOOGL], Decimal('0.99998937'))

        for interest in self.nodes[0].getinterest('LOAN150'):
            if interest['token'] == symbolTSLA:
                assert_equal(interest['totalInterest'], Decimal('0.00000057'))
            elif interest['token'] == symbolGOOGL:
                assert_equal(interest['totalInterest'], Decimal('0.00000133'))

        # loan payback burn
        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(self.nodes[0].getburninfo()['paybackburn'], Decimal('0.00186824'))
        assert_equal(sorted(vaultInfo['loanAmounts']), sorted(['0.50000057@' + symbolTSLA, '1.00000133@' + symbolGOOGL]))

        try:
            self.nodes[0].withdrawfromvault(vaultId, account0, "200@" + symbolDFI)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot withdraw all collaterals as there are still active loans in this vault" in errorString)

        try:
            self.nodes[0].withdrawfromvault(vaultId, account0, "199@" + symbolDFI)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault does not have enough collateralization ratio defined by loan scheme" in errorString)

        self.nodes[0].withdrawfromvault(vaultId, account0, "100@" + symbolDFI)
        # self.nodes[0].generate(1)

        #to be able to repay whole loan
        self.nodes[0].minttokens(["0.00001083@" + symbolTSLA, "0.00002659@" + symbolGOOGL])

        self.nodes[0].generate(10)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(sorted(vaultInfo['loanAmounts']), sorted(['0.50000627@' + symbolTSLA, '1.00001463@' + symbolGOOGL]))

        self.nodes[0].loanpayback({
                    'vaultId': vaultId,
                    'from': account0,
                    'amounts': vaultInfo['loanAmounts']})


        self.nodes[0].generate(1)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(vaultInfo['loanAmounts'], [])
        assert_equal(sorted(self.nodes[0].listaccounthistory(account0)[0]['amounts']), sorted(['-1.00001463@GOOGL', '-0.50000627@TSLA']))
        assert_greater_than_or_equal(self.nodes[0].getburninfo()['paybackburn'], Decimal('0.00443691'))

        for interest in self.nodes[0].getinterest('LOAN150'):
            if interest['token'] == symbolTSLA:
                assert_equal(interest['totalInterest'], Decimal('0.00000000'))
                assert_equal(interest['interestPerBlock'], Decimal('0.00000000'))
            elif interest['token'] == symbolGOOGL:
                assert_equal(interest['totalInterest'], Decimal('0.00000000'))
                assert_equal(interest['interestPerBlock'], Decimal('0.00000000'))

        self.nodes[0].withdrawfromvault(vaultId, account0, "100@" + symbolDFI)

        self.nodes[0].generate(1)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(vaultInfo['collateralAmounts'], [])
        assert_equal(vaultInfo['collateralValue'], Decimal('0.00000000'))
        assert_equal(vaultInfo['loanValue'], Decimal('0.00000000'))

if __name__ == '__main__':
    LoanTakeLoanTest().main()
