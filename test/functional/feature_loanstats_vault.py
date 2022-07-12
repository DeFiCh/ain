#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Auction loanstats."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
import time

class LoanstatsAuctionTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-loanstats=1']
            ]

    def run_test(self):
        self.nodes[0].generate(110)

        # Set address alias
        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Transfer DFI
        self.nodes[0].utxostoaccount({account: "120@DFI"})
        self.nodes[0].generate(1)

        # Setup oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id = self.nodes[0].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[0].generate(1)

        # Set oracle prices
        oracle_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}]
        self.nodes[0].setoracledata(oracle_id, int(time.time()), oracle_prices)
        self.nodes[0].generate(6)

        # Create collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        # Create loan tokens
        self.nodes[0].setloantoken({
            'symbol': "TSLA",
            'name': "Tesla Token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': 'DUSD',
            'name': 'DUSD token',
            'fixedIntervalPriceId': 'DUSD/USD',
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(4)

        # Mint tokens
        self.nodes[0].minttokens("100000@TSLA")
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(200, 1, 'LOAN200')
        self.nodes[0].generate(1)

        # Create vault
        vault_id = self.nodes[0].createvault(account)
        self.nodes[0].generate(1)

        # Deposit to vault
        self.nodes[0].deposittovault(vault_id, account, '120@DFI')
        self.nodes[0].generate(1)

        # Take loans
        self.nodes[0].takeloan({
                'vaultId': vault_id,
                'amounts': "60@TSLA"})
        self.nodes[0].takeloan({
                'vaultId': vault_id,
                'amounts': "1@DUSD"})
        self.nodes[0].generate(1)

        # Check live stats
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loan/2'], Decimal('1.00000000'))

        # Update oracle price to liquidate vaults
        oracle_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}]
        self.nodes[0].setoracledata(oracle_id, int(time.time()), oracle_prices)
        self.nodes[0].generate(12)

        # Place auction bid
        self.nodes[0].placeauctionbid(vault_id, 2, account, '1.1@DUSD')
        self.nodes[0].generate(35)

        # Check live stats
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loan/2'], Decimal('0.00000000'))

if __name__ == '__main__':
    LoanstatsAuctionTest().main()

