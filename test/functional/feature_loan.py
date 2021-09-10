#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan."""

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

import calendar
import time
from decimal import Decimal

class LoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(219)
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
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

        # setup oracle
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"}, {"currency": "USD", "tokenAmount": "10@DFI"}, {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        # set DFI an BTC as collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})
        self.nodes[0].setcollateraltoken({
                                    'token': "BTC",
                                    'factor': 1,
                                    'priceFeedId': oracle_id1})
        self.nodes[0].generate(1)

        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(150, 0.5, 'LOAN0002')
        self.nodes[0].generate(1)

        # Create vault
        vaultId1 = self.nodes[0].createvault(account, '') # default loan scheme
        self.nodes[0].generate(1)

        # deposit DFI and BTC to vault1
        self.nodes[0].deposittovault(vaultId1, account, '1000@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '1000@BTC')
        self.nodes[0].generate(1)

        # set TSLA as loan token
        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'priceFeedId': oracle_id1,
                            'mintable': True,
                            'interest': 1})
        self.nodes[0].generate(1)

        # take loan
        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "1000@TSLA"})
        self.nodes[0].generate(1)
        accountBalances = self.nodes[0].getaccount(account)
        assert_equal(accountBalances, ['1000.00000000@DFI', '1000.00000000@BTC', '1000.00000000@TSLA'])

        self.nodes[0].generate(60) # ~1 Month
        vault1 = self.nodes[0].getvault(vaultId1)
        interests = self.nodes[0].getinterest(
            vault1['loanSchemeId'],
            'TSLA'
        )
        totalLoanInterests = (1+interests[0]['totalInterest']) * 1000 # Initial loan taken
        assert_equal(Decimal(vault1['loanAmount'][0].split("@")[0]), totalLoanInterests)

        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "20@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(10)

        accountBalances = self.nodes[0].getaccount(account)
        vault1 = self.nodes[0].getvault(vaultId1)
        try:
            self.nodes[0].accounttoaccount(account, {self.nodes[0].getnewaddress(): "1000@BTC"} )
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("First bid should include liquidation penalty of 5%" in errorString)

        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 2)
        try:
            self.nodes[0].auctionbid(vaultId1, 0, account, "500@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("First bid should include liquidation penalty of 5%" in errorString)

        # No TSLA in account bidding
        try:
            self.nodes[0].auctionbid(vaultId1, 0, account, "525@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("amount 0.00000000 is less than 525.00000000" in errorString)

        self.nodes[0].minttokens("2000@TSLA")
        self.nodes[0].generate(1)

        self.nodes[0].auctionbid(vaultId1, 0, account, "525@TSLA")
        self.nodes[0].generate(1)
        accountBalances = self.nodes[0].getaccount(account)
        assert_equal(accountBalances, ['1000.00000000@DFI', '1000.00000000@BTC', '1475.00000000@TSLA'])

        # new account for new bidder
        self.nodes[0].generate(1)
        account2 = self.nodes[0].getnewaddress()
        self.nodes[0].accounttoaccount(account, {account2: "1000@TSLA"} )
        self.nodes[0].generate(1)
        self.sync_blocks()
        accountBalances2 = self.nodes[0].getaccount(account2)
        accountBalances = self.nodes[0].getaccount(account)

        try:
            self.nodes[1].auctionbid(vaultId1, 0, account2, "526@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("amount 0.00000000 is less than 525.00000000" in errorString)

if __name__ == '__main__':
    LoanTest().main()

