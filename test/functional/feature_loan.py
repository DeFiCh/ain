#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

import calendar
import time

class LoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(120)
        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].createloanscheme(150, 0.5, 'LOAN0002')
        self.nodes[0].generate(1)

        self.nodes[0].setdefaultloanscheme('LOAN0001')
        self.nodes[0].generate(1)

        vaultId1 = self.nodes[0].createvault('') # default loan scheme
        self.nodes[0].generate(1)

        vault1 = self.nodes[0].getvault(vaultId1)

        # Prepare tokens for deposittoloan/takeloan
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(100)

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

        # deposit DFI and BTC to vault1
        self.nodes[0].deposittovault(vaultId1, accountDFI, '1000@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, accountBTC, '1000@BTC')
        self.nodes[0].generate(1)

        vault1 = self.nodes[0].getvault(vaultId1)

        # set TSLA as loan token
        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'priceFeedId': oracle_id1,
                            'mintable': True,
                            'interest': 0})
        self.nodes[0].generate(1)

        # take loan
        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "1000@TSLA"})
        self.nodes[0].generate(1)

        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "20@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(10)

        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 2)
        assert_raises_rpc_error(None, "First bid should include liquidation penalty of 5%", self.nodes[0].auctionbid, vaultId1, 0, account, "500@TSLA")

if __name__ == '__main__':
    LoanTest().main()

