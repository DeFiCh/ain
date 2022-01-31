#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - get interest calculation."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

import calendar
import time
from decimal import Decimal

class LoanGetInterestTest (DefiTestFramework):
    symbolDFI = "DFI"
    symbolBTC = "BTC"
    symboldUSD = "DUSD"

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
                '-fortcanningheight=50', '-fortcanningmuseumheight=200', '-fortcanningparkheight=270', '-fortcanninghillheight=300', '-eunosheight=50', '-txindex=1']
        ]

    def setup(self):
        self.nodes[0].generate(150)

        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.account0
        })

        self.nodes[0].generate(1)

        idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]

        self.nodes[0].utxostoaccount({self.account0: "1000@" + self.symbolDFI})

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
        ]
        oracle_id1 = self.nodes[0].appointoracle(
            oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "100000@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"
        })

        self.nodes[0].setcollateraltoken({
            'token': idBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1
        })
        self.nodes[0].generate(1)

        self.nodes[0].createloanscheme(150, 100, 'LOAN150')

        self.nodes[0].generate(5)

        iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]

        self.nodes[0].minttokens("300@" + self.symboldUSD)
        self.nodes[0].generate(1)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.000'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        })
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {self.account0: ["30@" + self.symbolDFI, "300@" + self.symboldUSD]}, self.account0)
        self.nodes[0].generate(1)

    def run_test(self):
        self.setup()

        vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId, self.account0, "400@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "2000@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        self.nodes[0].generate(25) # Accrue interest

        getInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
        assert_equal(getInterest[0]['totalInterest'], Decimal('0.99923876'))
        assert_equal(getInterest[0]['interestPerBlock'], Decimal('0.03843226'))

        self.nodes[0].deposittovault(vaultId, self.account0, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "500@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        getInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
        assert_equal(getInterest[0]['totalInterest'], Decimal('1.08571134'))
        assert_equal(getInterest[0]['interestPerBlock'], Decimal('0.04804032'))

        self.nodes[0].generate(10) # Activate FCM

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "500.2@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        getInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
        assert_equal(getInterest[0]['totalInterest'], Decimal('1.62376677'))
        assert_equal(getInterest[0]['interestPerBlock'], Decimal('0.05765223'))

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "0.00000001@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        getInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
        print("getInterest", getInterest)
        assert_equal(getInterest[0]['totalInterest'], Decimal('1.68141901'))
        assert_equal(getInterest[0]['interestPerBlock'], Decimal('0.05765224'))

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "50000.00000001@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        getInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
        assert_equal(getInterest[0]['totalInterest'], Decimal('2.69987797'))
        assert_equal(getInterest[0]['interestPerBlock'], Decimal('1.01845896'))

        for i in range(1, 50):
            self.nodes[0].takeloan({
                'vaultId': vaultId,
                'amounts': "0.00000001@" + self.symboldUSD
            })
            self.nodes[0].generate(1)
            newInterest = self.nodes[0].getinterest("LOAN150", "DUSD")
            assert_equal(newInterest[0]['totalInterest'], Decimal('2.69987797') + newInterest[0]['interestPerBlock'] * i)

if __name__ == '__main__':
    LoanGetInterestTest().main()
