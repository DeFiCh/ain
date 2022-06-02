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

    def create_tokens(self):
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

    def mint_tokens(self):
        # mint BTC
        self.nodes[0].minttokens("2000@BTC")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("1000@TSLA")
        self.nodes[0].generate(1)

        self.account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # transfer DFI
        self.nodes[0].utxostoaccount({self.account: "2000@DFI"})
        self.nodes[0].generate(1)

    def setup_oracles(self):
        # setup oracle
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        self.price_feeds = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, self.price_feeds, 10)
        self.nodes[0].generate(1)
        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, self.price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        self.oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"}, {"currency": "USD", "tokenAmount": "10@DFI"}, {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, self.oracle1_prices)
        self.nodes[0].generate(1)
        self.oracle2_prices = [{"currency": "USD", "tokenAmount": "15@TSLA"}, {"currency": "USD", "tokenAmount": "15@DFI"}, {"currency": "USD", "tokenAmount": "15@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, self.oracle2_prices)
        self.nodes[0].generate(1)

    def set_collateral_tokens(self):
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

    def create_loanschemes(self):
        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(150, 0.5, 'LOAN0002')
        self.nodes[0].generate(1)

    def set_loan_tokens(self):
        # set TSLA as loan token
        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 1})
        self.nodes[0].generate(6)

    def init_vault(self):
        # Create vault
        self.vaultId1 = self.nodes[0].createvault(self.account, '') # default loan scheme
        self.nodes[0].generate(5)

        # deposit DFI and BTC to vault1
        self.nodes[0].deposittovault(self.vaultId1, self.account, '1000@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(self.vaultId1, self.account, '1000@BTC')
        self.nodes[0].generate(1)

        # take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId1,
                    'amounts': "1000@TSLA"})
        self.nodes[0].generate(1)
        accountBal = self.nodes[0].getaccount(self.account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '2000.00000000@TSLA'])
        self.nodes[0].generate(60) # ~10 hours
        vault1 = self.nodes[0].getvault(self.vaultId1)
        interests = self.nodes[0].getinterest(
            vault1['loanSchemeId'],
            'TSLA'
        )
        totalLoanAmount = (1000+interests[0]['totalInterest']) # Initial loan taken
        assert_equal(Decimal(vault1['loanAmounts'][0].split("@")[0]), totalLoanAmount)

    def setup(self):
        self.nodes[0].generate(300)
        self.nodes[1].generate(110)
        self.create_tokens()
        self.setup_oracles()
        self.set_collateral_tokens()
        self.set_loan_tokens()
        self.mint_tokens()
        self.create_loanschemes()
        self.init_vault()

    def trigger_liquidation(self):
        # Trigger liquidation updating price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "20@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "30@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        # Auction tests
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 3)

        getloaninfo = self.nodes[0].getloaninfo()
        assert_equal(getloaninfo['totals']['openAuctions'], 3)

    def place_too_low_bid(self):
        # Fail auction bid
        try:
            self.nodes[0].placeauctionbid(self.vaultId1, 0, self.account, "410@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("First bid should include liquidation penalty of 5%" in errorString)

    def place_auction_bids_batches_0_1(self):
        self.nodes[0].placeauctionbid(self.vaultId1, 0, self.account, "550@TSLA")
        self.nodes[0].generate(1)

        batches = self.nodes[0].listauctions()[0]['batches']
        highestBid = batches[0]['highestBid']
        assert_equal(highestBid['owner'], self.account)
        assert_equal(highestBid['amount'], '550.00000000@TSLA')
        accountBal = self.nodes[0].getaccount(self.account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1450.00000000@TSLA'])

        self.nodes[0].placeauctionbid(self.vaultId1, 1, self.account, "450@TSLA")
        self.nodes[0].generate(1)

        batches = self.nodes[0].listauctions()[0]['batches']
        highestBid = batches[1]['highestBid']
        assert_equal(highestBid['owner'], self.account)
        assert_equal(highestBid['amount'], '450.00000000@TSLA')
        accountBal = self.nodes[0].getaccount(self.account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '1000.00000000@TSLA'])

    def bid_with_new_account_in_different_node(self):
        # new account for new bidder
        self.sync_blocks()
        self.account2 = self.nodes[1].getnewaddress()
        self.nodes[0].accounttoaccount(self.account, {self.account2: "1000@TSLA"} )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Fail auction bid less that 1% higher
        try:
            self.nodes[1].placeauctionbid(self.vaultId1, 0, self.account2, "555@TSLA") # just under 1%
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Bid override should be at least 1% higher than current one" in errorString)
        # Fail bid from not owned account
        try:
            self.nodes[0].placeauctionbid(self.vaultId1, 0, self.account2, "600@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for" in errorString)

        self.nodes[1].placeauctionbid(self.vaultId1, 0, self.account2, "555.5@TSLA") # above 1%
        self.nodes[1].generate(1)
        self.sync_blocks()

        # check balances are right after greater bid
        account2Bal = self.nodes[0].getaccount(self.account2)
        accountBal = self.nodes[0].getaccount(self.account)
        assert_equal(accountBal, ['1000.00000000@DFI', '1000.00000000@BTC', '550.00000000@TSLA'])
        assert_equal(account2Bal, ['444.50000000@TSLA'])


    def let_auction_end_and_check_balances(self):
        self.nodes[0].generate(8)
        account2Bal = self.nodes[0].getaccount(self.account2)
        accountBal = self.nodes[0].getaccount(self.account)
        vault1 = self.nodes[0].getvault(self.vaultId1)
        assert_equal(vault1['state'], "inLiquidation")
        assert_equal(vault1['liquidationHeight'], 577)
        assert_equal(vault1['liquidationPenalty'], Decimal('5.00000000'))
        assert_equal(vault1['batchCount'], 1)
        assert_equal(accountBal, ['1400.00000000@DFI', '1400.00000000@BTC', '550.00000000@TSLA'])
        # auction winner account has now first batch collaterals
        assert_equal(account2Bal, ['400.00000000@DFI', '400.00000000@BTC', '444.50000000@TSLA'])

        auction = self.nodes[0].listauctionhistory()[0]
        assert_equal(auction['winner'], self.account)
        assert_equal(auction['blockHeight'], 540)
        assert_equal(auction['vaultId'], self.vaultId1)
        assert_equal(auction['batchIndex'], 1)
        assert_equal(auction['auctionBid'], "450.00000000@TSLA")
        assert_equal(auction['auctionWon'].sort(), ['400.00000000@DFI', '400.00000000@BTC'].sort())

    def test_auction_without_bid_is_reopened(self):
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist[0]['batches']), 1)

    def bid_on_remaining_auction_and_exit_vault_inliquidation_state(self):
        self.nodes[0].placeauctionbid(self.vaultId1, 0, self.account, "220@TSLA") # above 5% and leave vault with some loan to exit liquidation state
        self.nodes[0].generate(40) # let auction end

        auction = self.nodes[0].listauctionhistory(self.account)[0]
        assert_equal(auction['winner'], self.account)
        assert_equal(auction['blockHeight'], 577)
        assert_equal(auction['vaultId'], self.vaultId1)
        assert_equal(auction['batchIndex'], 0)
        assert_equal(auction['auctionBid'], "220.00000000@TSLA")
        assert_equal(auction['auctionWon'].sort(), ['200.00000000@DFI', '200.00000000@DFI'].sort())

    def check_auctionhistory_and_vault_balances(self):
        auctions = self.nodes[0].listauctionhistory("all")
        assert_equal(len(auctions), 3)
        assert_equal(auctions[0]['winner'], self.account)
        assert_equal(auctions[1]['winner'], self.account2)
        assert_equal(auctions[2]['winner'], self.account)
        assert_equal(auctions[0]['blockHeight'], 577)
        assert_equal(auctions[1]['blockHeight'], 540)
        assert_equal(auctions[2]['blockHeight'], 540)
        assert_equal(auctions[0]['auctionBid'], "220.00000000@TSLA")
        assert_equal(auctions[1]['auctionBid'], "555.50000000@TSLA")
        assert_equal(auctions[2]['auctionBid'], "450.00000000@TSLA")

        auctionsV = self.nodes[0].listauctionhistory(self.vaultId1)
        assert_equal(len(auctionsV), 3)
        assert_equal(auctions, auctionsV)

        auctionsAccount2 = self.nodes[0].listauctionhistory(self.account2)
        assert_equal(len(auctionsAccount2), 1)

        vault1 = self.nodes[0].getvault(self.vaultId1)
        accountBal = self.nodes[0].getaccount(self.account)
        account2Bal = self.nodes[0].getaccount(self.account2)

        assert_equal(vault1['state'], "active")
        assert_equal(accountBal, ['1600.00000000@DFI', '1600.00000000@BTC', '330.00000000@TSLA'])
        assert_equal(account2Bal, ['400.00000000@DFI', '400.00000000@BTC', '444.50000000@TSLA'])

    def deposit_and_takeloan_in_now_active_vault(self):
        self.nodes[0].deposittovault(self.vaultId1, self.account, '600@DFI')
        self.nodes[0].generate(1)

        # update prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "2@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "3@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId1,
            'amounts': "1000@TSLA"
        })
        self.nodes[0].generate(1)
        vault1 = self.nodes[0].getvault(self.vaultId1)
        assert_equal(vault1['collateralAmounts'], ['600.00000000@DFI'])
        assert_equal(vault1['loanAmounts'], ['1000.00038051@TSLA'])

    def trigger_liquidation_again(self):
        vault = self.nodes[0].getvault(self.vaultId1)
        assert_equal(vault['state'], 'active')
        # Reset price in oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "200@TSLA"}]
        oracle2_prices = [{"currency": "USD", "tokenAmount": "300@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle2_prices)
        self.nodes[0].generate(36)

        vault = self.nodes[0].getvault(self.vaultId1)
        assert_equal(vault['state'], 'inLiquidation')

    def test_autoselect_account_for_bid(self):
        # Not enought tokens on account
        try:
            self.nodes[0].placeauctionbid(self.vaultId1, 0, "*", "1600@TSLA")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Not enough tokens on account, call sendtokenstoaddress to increase it." in errorString)

        self.nodes[0].minttokens("1600@TSLA")
        self.nodes[0].generate(1)
        newAddress = self.nodes[0].getnewaddress("")
        self.nodes[0].sendtokenstoaddress({}, {newAddress: "1600@TSLA"}) # newAddress has now the highest amount of TSLA token
        self.nodes[0].generate(1)

        self.nodes[0].placeauctionbid(self.vaultId1, 0, "*", "1600@TSLA") # Autoselect address with highest amount of TSLA token
        self.nodes[0].generate(40) # let auction end

        auction = self.nodes[0].listauctionhistory(newAddress)[0]
        assert_equal(auction['winner'], newAddress)
        assert_equal(auction['blockHeight'], 666)
        assert_equal(auction['vaultId'], self.vaultId1)
        assert_equal(auction['batchIndex'], 0)
        assert_equal(auction['auctionBid'], "1600.00000000@TSLA")

    def test_lisauctionhistory_pagination_filtering(self):
        self.sync_blocks()
        list = self.nodes[0].listauctionhistory("all", {"limit": 1})
        assert_equal(len(list), 1)
        list = self.nodes[0].listauctionhistory("all", {"maxBlockHeight": 541})
        assert_equal(len(list), 2)
        list = self.nodes[0].listauctionhistory("all", {"index": 0})
        assert_equal(len(list), 3)
        list = self.nodes[0].listauctionhistory("all", {"index": 1})
        assert_equal(len(list), 1)
        list = self.nodes[0].listauctionhistory("all")
        assert_equal(len(list), 4)
        list = self.nodes[0].listauctionhistory("mine")
        assert_equal(len(list), 3)
        list = self.nodes[0].listauctionhistory()
        assert_equal(len(list), 3)

        self.sync_blocks()

        # node 1
        list1 = self.nodes[1].listauctionhistory()
        assert_equal(len(list1), 1)
        list1 = self.nodes[1].listauctionhistory("all")
        assert_equal(len(list1), 4)
        list1 = self.nodes[1].listauctionhistory("all", {"index": 1})
        assert_equal(len(list1), 1)
        list1 = self.nodes[1].listauctionhistory("all", {"index": 0})
        assert_equal(len(list1), 3)
        list1 = self.nodes[1].listauctionhistory("all", {"index": 0, "limit": 2})
        assert_equal(len(list1), 2)

    def run_test(self):
        self.setup()
        self.trigger_liquidation()
        self.place_too_low_bid()
        self.place_auction_bids_batches_0_1()
        self.bid_with_new_account_in_different_node()
        self.let_auction_end_and_check_balances()
        self.test_auction_without_bid_is_reopened()
        self.bid_on_remaining_auction_and_exit_vault_inliquidation_state()
        self.check_auctionhistory_and_vault_balances()
        self.deposit_and_takeloan_in_now_active_vault()
        self.trigger_liquidation_again()
        self.test_autoselect_account_for_bid()
        self.test_lisauctionhistory_pagination_filtering()

if __name__ == '__main__':
    LoanTest().main()

