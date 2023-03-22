#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time


class LoanTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1',
             '-fortcanningheight=1']
        ]

    def run_test(self):
        self.nodes[0].generate(400)
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
        self.nodes[0].minttokens("3000@BTC")
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # transfer DFI
        self.nodes[0].utxostoaccount({account: "3000@DFI"})
        self.nodes[0].generate(1)

        # setup oracle
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"},
                        {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"}, {"currency": "USD", "tokenAmount": "10@DFI"},
                          {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6)

        # set DFI an BTC as collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': "DFI",
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].setcollateraltoken({
            'token': "BTC",
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].generate(1)

        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].generate(1)

        # Create vault
        vaultsId = []
        vault_number = 5
        for _ in range(vault_number):
            vaultsId.append(self.nodes[0].createvault(account, ''))
        self.nodes[0].generate(1)

        # deposit DFI to vaults and let price enter valid state
        self.nodes[0].generate(2)
        for id in vaultsId:
            self.nodes[0].deposittovault(id, account, '100@DFI')
        self.nodes[0].generate(1)

        # deposit BTC to vaults
        for id in vaultsId:
            self.nodes[0].deposittovault(id, account, '100@BTC')
        self.nodes[0].generate(1)

        # set TSLA as loan token
        self.nodes[0].setloantoken({
            'symbol': "TSLA",
            'name': "Tesla Token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(8)  # let active price update

        # take loan
        for id in vaultsId:
            self.nodes[0].takeloan({
                'vaultId': id,
                'amounts': "10@TSLA"})
        self.nodes[0].generate(1)

        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "1000@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(
            12)  # if price is invalid, auctions are blocked so listauction is empty. We need 2 cicles of price update.

        # Auction tests
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), vault_number)  # all vaults should be under liquidation

        secondAuction = auctionlist[1]  # get second auction to test pagination

        auctionlist = self.nodes[0].listauctions(
            {"start": {"vaultId": secondAuction["vaultId"], "height": secondAuction["liquidationHeight"]}})
        assert_equal(len(auctionlist), vault_number - 2)

        auctionlist = self.nodes[0].listauctions(
            {"start": {"vaultId": secondAuction["vaultId"], "height": secondAuction["liquidationHeight"]},
             "including_start": True})
        assert_equal(len(auctionlist), vault_number - 1)
        assert_equal(secondAuction, auctionlist[0])

        auctionlist = self.nodes[0].listauctions({"limit": 1})
        assert_equal(len(auctionlist), 1)


if __name__ == '__main__':
    LoanTest().main()
