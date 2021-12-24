#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan."""

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time
from decimal import Decimal

class LoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(300)
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
        self.nodes[0].minttokens("2000@BTC")
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # transfer DFI
        self.nodes[0].utxostoaccount({account: "2000@DFI"})
        self.nodes[0].generate(1)

        # setup oracle
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)
        oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        oracle_id2 = self.nodes[0].appointoracle(oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"}, {"currency": "USD", "tokenAmount": "10@DFI"}, {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)
        oracle2_prices = [{"currency": "USD", "tokenAmount": "15@TSLA"}, {"currency": "USD", "tokenAmount": "15@DFI"}, {"currency": "USD", "tokenAmount": "15@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(1)

        # set DFI an BTC as collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)
        self.nodes[0].setcollateraltoken({
                                    'token': "BTC",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].generate(1)

        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(150, 0.5, 'LOAN0002')
        self.nodes[0].generate(1)

        # Create vault
        vaultId1 = self.nodes[0].createvault(account, '') # default loan scheme
        self.nodes[0].generate(5)

        # deposit DFI and BTC to vault1
        self.nodes[0].deposittovault(vaultId1, account, '1000@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '1000@BTC')
        self.nodes[0].generate(1)

        # set TSLA as loan token
        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 1})
        self.nodes[0].generate(6)

        # take loan
        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "1000@TSLA"})
        self.nodes[0].generate(1)
        accountBal = self.nodes[0].getaccount(account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1000.00000000@TSLA'])
        self.nodes[0].generate(60) # ~10 hours
        vault1 = self.nodes[0].getvault(vaultId1)
        interests = self.nodes[0].getinterest(
            vault1['loanSchemeId'],
            'TSLA'
        )
        totalLoanAmount = (1000+interests[0]['totalInterest']) # Initial loan taken
        assert_equal(Decimal(vault1['loanAmounts'][0].split("@")[0]), totalLoanAmount)

        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "20@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "30@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        # Auction tests
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 3)
        vault1 = self.nodes[0].getvault(vaultId1)

        # Fail auction bid
        try:
            self.nodes[0].placeauctionbid(vaultId1, 0, account, "410@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("First bid should include liquidation penalty of 5%" in errorString)

        self.nodes[0].minttokens("1000@TSLA")
        self.nodes[0].generate(1)

        self.nodes[0].placeauctionbid(vaultId1, 0, account, "550@TSLA")
        self.nodes[0].generate(1)

        batches = self.nodes[0].listauctions()[0]['batches']
        highestBid = batches[0]['highestBid']
        assert_equal(highestBid['owner'], account)
        assert_equal(highestBid['amount'], '550.00000000@TSLA')
        accountBal = self.nodes[0].getaccount(account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1450.00000000@TSLA'])

        # new account for new bidder
        self.nodes[0].generate(1)
        account2 = self.nodes[0].getnewaddress()
        self.nodes[0].accounttoaccount(account, {account2: "1000@TSLA"} )
        self.nodes[0].generate(1)

        # Fail auction bid less that 1% higher
        try:
            self.nodes[0].placeauctionbid(vaultId1, 0, account2, "555@TSLA") # just under 1%
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Bid override should be at least 1% higher than current one" in errorString)

        self.nodes[0].placeauctionbid(vaultId1, 0, account2, "555.5@TSLA") # above 1%
        self.nodes[0].generate(1)

        # check balances are right after greater bid
        account2Bal = self.nodes[0].getaccount(account2)
        accountBal = self.nodes[0].getaccount(account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1000.00000000@TSLA'])
        assert_equal(account2Bal, ['444.50000000@TSLA'])

        # let auction end and check account balances
        self.nodes[0].generate(8)
        account2Bal = self.nodes[0].getaccount(account2)
        accountBal = self.nodes[0].getaccount(account)
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['state'], "inLiquidation")
        assert_equal(vault1['liquidationHeight'], 469)
        assert_equal(vault1['liquidationPenalty'], Decimal('5.00000000'))
        assert_equal(vault1['batchCount'], 2)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1000.00000000@TSLA'])
        # auction winner account has now first batch collaterals
        assert_equal(account2Bal, ['400.00000000@DFI', '400.00000000@BTC', '444.50000000@TSLA'])

        auction = self.nodes[0].listauctionhistory()[0]
        assert_equal(auction['winner'], account2)
        assert_equal(auction['blockHeight'], 432)
        assert_equal(auction['vaultId'], vaultId1)
        assert_equal(auction['batchIndex'], 0)
        assert_equal(auction['auctionBid'], "555.50000000@TSLA")
        assert_equal(auction['auctionWon'].sort(), ['400.00000000@DFI', '400.00000000@BTC'].sort())

        # check that still auction due to 1 batch without bid
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 2)

        self.nodes[0].placeauctionbid(vaultId1, 0, account, "515@TSLA") # above 5% and leave vault with some loan to exit liquidation state
        self.nodes[0].generate(40) # let auction end

        auction = self.nodes[0].listauctionhistory(account)[0]
        assert_equal(auction['winner'], account)
        assert_equal(auction['blockHeight'], 469)
        assert_equal(auction['vaultId'], vaultId1)
        assert_equal(auction['batchIndex'], 0)
        assert_equal(auction['auctionBid'], "515.00000000@TSLA")
        assert_equal(auction['auctionWon'].sort(), ['399.99999600@DFI', '399.99999600@DFI'].sort())

        self.nodes[0].placeauctionbid(vaultId1, 0, account, "259@TSLA")
        self.nodes[0].generate(40) # let auction end

        auctions = self.nodes[0].listauctionhistory()
        assert_equal(len(auctions), 3)
        assert_equal(auctions[0]['winner'], account)
        assert_equal(auctions[1]['winner'], account)
        assert_equal(auctions[2]['winner'], account2)
        assert_equal(auctions[0]['blockHeight'], 506)
        assert_equal(auctions[1]['blockHeight'], 469)
        assert_equal(auctions[2]['blockHeight'], 432)
        assert_equal(auctions[0]['auctionBid'], "259.00000000@TSLA")
        assert_equal(auctions[1]['auctionBid'], "515.00000000@TSLA")
        assert_equal(auctions[2]['auctionBid'], "555.50000000@TSLA")

        auctionsV = self.nodes[0].listauctionhistory(vaultId1)
        assert_equal(len(auctionsV), 3)
        assert_equal(auctions, auctionsV)

        vault1 = self.nodes[0].getvault(vaultId1)
        accountBal = self.nodes[0].getaccount(account)

        assert_equal(vault1['state'], "active")
        assert_equal(accountBal, ['1600.00000000@DFI', '1600.00000000@BTC', '226.00000000@TSLA'])


        self.nodes[0].deposittovault(vaultId1, account, '600@DFI')
        self.nodes[0].generate(1)
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['600.00000000@DFI'])

        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "2@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "3@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        # take loan
        self.nodes[0].takeloan({
            'vaultId': vaultId1,
            'amounts': "1000@TSLA"
        })
        self.nodes[0].generate(1)

        # Reset price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "200@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "300@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        # Not enought tokens on account
        try:
            self.nodes[0].placeauctionbid(vaultId1, 0, "*", "1600@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Not enough tokens on account, call sendtokenstoaddress to increase it." in errorString)

        newAddress = self.nodes[0].getnewaddress("")
        self.nodes[0].sendtokenstoaddress({}, {newAddress: "1600@TSLA"}) # newAddress has now the highest amount of TSLA token
        self.nodes[0].generate(1)

        self.nodes[0].placeauctionbid(vaultId1, 0, "*", "1600@TSLA") # Autoselect address with highest amount of TSLA token
        self.nodes[0].generate(40) # let auction end

        auction = self.nodes[0].listauctionhistory(newAddress)[0]
        assert_equal(auction['winner'], newAddress)
        assert_equal(auction['blockHeight'], 600)
        assert_equal(auction['vaultId'], vaultId1)
        assert_equal(auction['batchIndex'], 0)
        assert_equal(auction['auctionBid'], "1600.00000000@TSLA")

if __name__ == '__main__':
    LoanTest().main()

