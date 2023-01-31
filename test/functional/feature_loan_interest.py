#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - interest test."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

import calendar
import time
from decimal import Decimal

class LoanInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-fortcanningheight=50', '-fortcanningmuseumheight=50', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-fortcanningheight=50', '-fortcanningmuseumheight=50', '-txindex=1']]

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

        self.nodes[0].setloantoken({
                                    'symbol': symbolTSLA,
                                    'name': "Tesla stock token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': True,
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
        self.sync_blocks()

        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': ["1@" + symbolTSLA, "2@" + symbolGOOGL]})

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

        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': ["1000@" + symboldUSD]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        try:
            self.nodes[0].paybackloan({
                        'vaultId': vaultId,
                        'from': account0,
                        'amounts': ["999.99900000@" + symboldUSD]})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot payback this amount of loan for " + symboldUSD + ", either payback full amount or less than this amount" in errorString)

        self.nodes[0].paybackloan({
                        'vaultId': vaultId,
                        'from': account0,
                        'amounts': ["999.99700000@" + symboldUSD]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(sorted(vaultInfo['interestAmounts']), ['0.00000001@DUSD', '0.00000460@TSLA', '0.00001068@GOOGL'])

        self.nodes[0].generate(100)
        self.sync_blocks()

        vaultInfo = self.nodes[0].getvault(vaultId)
        assert_equal(sorted(vaultInfo['interestAmounts']), ['0.00000101@DUSD', '0.00011960@TSLA', '0.00027768@GOOGL'])

        DUSDbalance = self.nodes[0].getaccount(account0, {}, True)[iddUSD]

        self.nodes[0].paybackloan({
                        'vaultId': vaultId,
                        'from': account0,
                        'amounts': ["1000@" + symboldUSD]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        newDUSDbalance = self.nodes[0].getaccount(account0, {}, True)[iddUSD]
        assert_equal(newDUSDbalance, DUSDbalance - Decimal('0.00414257'))

if __name__ == '__main__':
    LoanInterestTest().main()
