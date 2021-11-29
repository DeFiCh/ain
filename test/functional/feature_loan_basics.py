#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - loan basics."""

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
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(50)
        self.sync_blocks()
        self.nodes[1].generate(110)
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
        assert_equal(loans['totals']['collateralTokens'], 0)
        assert_equal(loans['totals']['loanTokens'], 0)

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        self.nodes[0].utxostoaccount({account0: "2000@" + symbolDFI})
        self.nodes[1].utxostoaccount({account1: "100@" + symbolDFI})

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

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)
        self.sync_blocks()

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

        self.nodes[0].setloantoken({
                                    'symbol': symboldUSD,
                                    'name': "DUSD stable token",
                                    'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1})

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].createloanscheme(150, 5, 'LOAN150')

        self.nodes[0].generate(5)
        self.sync_blocks()

        iddUSD = list(self.nodes[0].gettoken(symboldUSD).keys())[0]

        loans = self.nodes[0].getloaninfo()
        assert_equal(loans['totals']['schemes'], 1)
        assert_equal(loans['totals']['collateralTokens'], 2)
        assert_equal(loans['totals']['loanTokens'], 3)

        loanTokens = self.nodes[0].listloantokens()

        assert_equal(len(loanTokens), 3)
        idTSLA = list(self.nodes[0].getloantoken(symbolTSLA)["token"])[0]
        idGOOGL = list(self.nodes[0].getloantoken(symbolGOOGL)["token"])[0]

        vaultId1 = self.nodes[0].createvault( account0, 'LOAN150')

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].deposittovault(vaultId1, account0, "400@DFI")

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "2000@" + symboldUSD})

        self.nodes[0].generate(1)
        self.sync_blocks()

        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])

        self.nodes[0].generate(1)
        self.sync_blocks()

        # transfer
        self.nodes[0].addpoolliquidity({
            account0: ["300@" + symboldUSD, "100@" + symbolDFI]
        }, account0, [])
        self.nodes[0].generate(1)
        self.sync_blocks()

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
        assert_equal(loans['totals']['collateralTokens'], 2)
        assert_equal(loans['totals']['loanTokens'], 3)

        vaultId2 = self.nodes[1].createvault( account1, 'LOAN150')

        self.nodes[1].generate(2)
        self.sync_blocks()

        interest = self.nodes[0].getinterest('LOAN150', symbolTSLA)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000342'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000114'))

        interest = self.nodes[0].getinterest('LOAN150', symbolGOOGL)[0]

        assert_equal(interest['totalInterest'], Decimal('0.00000798'))
        assert_equal(interest['interestPerBlock'], Decimal('0.00000266'))

        try:
            self.nodes[0].paybackloan({
                        'vaultId': setLoanTokenTSLA,
                        'from': account0,
                        'amounts': "0.5@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing vault with id" in errorString)

        try:
            self.nodes[0].paybackloan({
                        'vaultId': vaultId2,
                        'from': account0,
                        'amounts': "0.5@" + symbolTSLA})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault with id " + vaultId2 + " has no collaterals" in errorString)

        self.nodes[1].deposittovault(vaultId2, account1, "100@" + symbolDFI)

        self.nodes[1].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].paybackloan({
                        'vaultId': vaultId2,
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

        self.nodes[0].paybackloan({
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
        assert_equal(self.nodes[0].getburninfo()['paybackburn'], Decimal('0.00186822'))
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

        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': account0,
                    'amounts': vaultInfo['loanAmounts']})

        self.nodes[0].generate(1)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(vaultInfo['loanAmounts'], [])
        assert_equal(sorted(self.nodes[0].listaccounthistory(account0)[0]['amounts']), sorted(['-1.00001463@GOOGL', '-0.50000627@TSLA']))
        assert_greater_than_or_equal(self.nodes[0].getburninfo()['paybackburn'], Decimal('0.00443685'))

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

        self.nodes[0].paybackloan({
                    'vaultId': vaultId1,
                    'from': account0,
                    'amounts': "500@" + symboldUSD})

        self.nodes[0].generate(1)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId1)
        assert_equal(vaultInfo['collateralAmounts'], ['400.00000000@DFI'])
        assert_equal(vaultInfo['collateralValue'], Decimal('4000.00000000'))
        assert_equal(vaultInfo['loanValue'], Decimal('1500.0610730'))

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                          {"currency": "USD", "tokenAmount": "10@GOOGL"},
                          {"currency": "USD", "tokenAmount": "5@DFI"},
                          {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.nodes[0].generate(12)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId1)
        assert_equal(vaultInfo['state'], 'inLiquidation')
        assert_equal(len(vaultInfo['batches']), 1)
        assert_equal(vaultInfo['batches'][0]['collaterals'], ['400.00000000@DFI'])

        address = self.nodes[0].getnewaddress()
        self.nodes[0].utxostoaccount({address: "100@" + symbolDFI})
        self.nodes[0].generate(1)

        vaultId3 = self.nodes[0].createvault(address)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId3, address, "100@DFI")
        self.nodes[0].generate(1)

        # take loan
        self.nodes[0].takeloan({
            'vaultId': vaultId3,
            'amounts': ["10@TSLA", "10@GOOGL"]
        })
        self.nodes[0].generate(1)

        address2 = self.nodes[0].getnewaddress()
        self.nodes[0].sendtokenstoaddress({}, {address2:["5@TSLA", "5@GOOGL"]}) # split into two address
        self.nodes[0].generate(1)

        try:
            self.nodes[0].paybackloan({
                'vaultId': vaultId3,
                'from': "*",
                'amounts': ["10@" + symbolTSLA, "10@" + symbolGOOGL]
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Not enough tokens on account, call sendtokenstoaddress to increase it." in errorString)

        self.nodes[0].paybackloan({
                'vaultId': vaultId3,
                'from': "*",
                'amounts': "5@" + symbolTSLA
        })
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vaultId3)
        assert_equal(sorted(vault['loanAmounts']), sorted(['5.00002853@' + symbolTSLA, '10.00003993@' + symbolGOOGL]))

        self.nodes[0].sendtokenstoaddress({}, {address2:["5@" + symbolTSLA, "10@" + symbolGOOGL]})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId3,
            'from': "*",
            'amounts': ["5@" + symbolTSLA, "10@" + symbolGOOGL]
        })
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vaultId3)
        assert_equal(sorted(vault['loanAmounts']), sorted(['0.00003425@' + symbolTSLA, '0.00005324@' + symbolGOOGL]))

if __name__ == '__main__':
    LoanTakeLoanTest().main()
