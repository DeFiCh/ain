#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - interest test."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time
from decimal import Decimal

FCH_HEIGHT = 1200

class LowInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=2', '-fortcanningheight=3', '-fortcanningmuseumheight=4', '-fortcanningparkheight=5', f'-fortcanninghillheight={FCH_HEIGHT}', '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet']
        ]

    account0 = None
    oracle_id1 = None
    symbolDFI = "DFI"
    symbolDOGE = "DOGE"
    symboldUSD = "DUSD"
    idDFI = 0
    iddUSD = 0
    idDOGE = 0
    tokenInterest = 0
    loanInterest = 0

    def test_load_account0_with_DFI(self):
        print('loading up account0 with DFI token...')
        self.nodes[0].generate(100) # get initial UTXO balance from immature to trusted -> check getbalances()
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        # UTXO -> token
        self.nodes[0].utxostoaccount({self.account0: "199999900@" + self.symbolDFI})
        self.nodes[0].generate(1)
        account0_balance = self.nodes[0].getaccount(self.account0)
        assert_equal(account0_balance, ['199999900.00000000@DFI'])

    def test_setup_oracles(self):
        print('setting up oracles...')
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DOGE"}]
        self.oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "0.0001@DOGE"},
                          {"currency": "USD", "tokenAmount": "10@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(120) # let active price update
        oracle_data = self.nodes[0].getoracledata(self.oracle_id1)
        assert_equal(len(oracle_data["priceFeeds"]), 2)
        assert_equal(len(oracle_data["tokenPrices"]), 2)

    def test_setup_tokens(self):
        print('setting up loan and collateral tokens...')
        self.nodes[0].setloantoken({
                    'symbol': self.symboldUSD,
                    'name': "DUSD stable token",
                    'fixedIntervalPriceId': "DUSD/USD",
                    'mintable': True,
                    'interest': 0})

        self.tokenInterest = Decimal(1)
        self.nodes[0].setloantoken({
                    'symbol': self.symbolDOGE,
                    'name': "DOGE token",
                    'fixedIntervalPriceId': "DOGE/USD",
                    'mintable': True,
                    'interest': Decimal(self.tokenInterest*100)})
        self.nodes[0].generate(1)

        # Set token ids
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDOGE = list(self.nodes[0].gettoken(self.symbolDOGE).keys())[0]

        # Mint tokens
        self.nodes[0].minttokens("1000000@DOGE")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("2000000@" + self.symboldUSD) # necessary for pools
        self.nodes[0].generate(1)

        # Setup collateral tokens
        self.nodes[0].setcollateraltoken({
                    'token': self.idDFI,
                    'factor': 1,
                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(300)

        assert_equal(len(self.nodes[0].listtokens()), 3)
        assert_equal(len(self.nodes[0].listloantokens()), 2)
        assert_equal(len(self.nodes[0].listcollateraltokens()), 1)


    def test_setup_poolpairs(self):
        print("setting up pool pairs...")
        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])

        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDOGE,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DOGE",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["1000000@" + self.symboldUSD, "1000000@" + self.symbolDFI]
        }, self.account0, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["1000000@" + self.symboldUSD, "1000@" + self.symbolDOGE]
        }, self.account0, [])
        self.nodes[0].generate(1)
        assert_equal(len(self.nodes[0].listpoolpairs()), 2)

    def test_setup_loan_scheme(self):
        print("creating loan scheme...")
        self.loanInterest = Decimal(1)
        # Create loan schemes and vaults
        self.nodes[0].createloanscheme(150, Decimal(self.loanInterest*100), 'LOAN150')
        self.nodes[0].generate(1)
        assert_equal(len(self.nodes[0].listloanschemes()), 1)


    def setup(self):
        print('Generating initial chain...')
        self.test_load_account0_with_DFI()
        self.test_setup_oracles()
        self.test_setup_tokens()
        self.test_setup_poolpairs()
        self.test_setup_loan_scheme()

    def get_new_vault_and_deposit(self, loan_scheme=None, amount=10000):
        vault_id = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vault_id, self.account0, str(amount)+"@DFI") # enough collateral to take loans
        self.nodes[0].generate(1)
        return vault_id

    def go_to_FCH(self):
        blocksUntilFCH = FCH_HEIGHT - self.nodes[0].getblockcount() + 2
        self.nodes[0].generate(blocksUntilFCH)
        blockChainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockChainInfo["softforks"]["fortcanninghill"]["active"], True)

    def go_before_FCH(self, n_blocks=500):
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(FCH_HEIGHT-n_blocks))
        self.nodes[0].generate(1)

    def is_FCH(self):
        return self.nodes[0].getblockcount() > FCH_HEIGHT

    def test_new_loan_with_interest_lower_than_1satoshi(self, payback=False):
        vault_id = self.get_new_vault_and_deposit()

        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.001314@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000001@DOGE')

        self.nodes[0].generate(35)
        vault_data = self.nodes[0].getvault(vault_id)
        expected_interest = self.is_FCH() and '0.00000009' or '0.00000036'
        assert_equal(vault_data["interestAmounts"][0], expected_interest+'@DOGE')

        if not payback:
            return

        # Payback
        expected_payback = self.is_FCH() and '0.00131409' or '0.00131436'
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': expected_payback+'@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])


    def test_new_loan_with_interest_exactly_25satoshi(self, payback=False):
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.1314@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000025@DOGE')

        self.nodes[0].generate(35)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000900@DOGE')

        if not payback:
            return

        # Payback
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': '0.13140900@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_new_loan_with_interest_over_1satoshi(self, payback=False):
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "100@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00019026@DOGE')

        self.nodes[0].generate(35)
        vault_data = self.nodes[0].getvault(vault_id)
        expected_interest = self.is_FCH() and '0.00684932' or '0.00684936'
        assert_equal(vault_data["interestAmounts"][0], expected_interest+'@DOGE')

        if not payback:
            return
        # Payback
        expected_payback = self.is_FCH() and '100.00684932' or '100.00684936'
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': expected_payback+'@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_low_loan(self, payback=False):
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.00000001@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000001@DOGE')

        self.nodes[0].generate(35)
        vault_data = self.nodes[0].getvault(vault_id)
        expected_interest = self.is_FCH() and '0.00000001' or '0.00000036'
        assert_equal(vault_data["interestAmounts"][0], expected_interest+'@DOGE')

        if not payback:
            return
        # Payback
        expected_payback = self.is_FCH() and '0.00000002' or '0.00000037'
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': expected_payback+'@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_high_loan(self, payback=False):
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "2345.225@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00446200@DOGE')

        self.nodes[0].generate(35)
        vault_data = self.nodes[0].getvault(vault_id)
        expected_interest = self.is_FCH() and '0.16063185' or '0.16063200'
        assert_equal(vault_data["interestAmounts"][0], expected_interest+'@DOGE')

        if not payback:
            return
        # Payback
        expected_payback = self.is_FCH() and '2345.38563185' or '2345.38563200'
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': expected_payback+'@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_1satoshi_loan_pre_post_fork(self, payback=False):
        self.go_before_FCH(30)
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.00000001@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000001@DOGE')

        self.go_to_FCH()

        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.00000001@DOGE"})
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000028@DOGE')
        # No more interest from loan pre FCH is accumulated, new calc is applied
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000028@DOGE')

        if not payback:
            return
        # Payback
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': '0.00000030@DOGE'})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_new_loan_with_interest_lower_than_1satoshi_pre_post_fork(self, payback=True):
        self.go_before_FCH(30)
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.001314@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000001@DOGE')

        self.go_to_FCH()

        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "0.00000001@DOGE"})
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000031@DOGE')
        # No more interest from loan pre FCH is accumulated, new calc is applied
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00000033@DOGE')

        if not payback:
            return
        # Payback
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': '0.00131434@DOGE'})
        self.nodes[0].generate(100)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])

    def test_new_loan_with_interest_over_1satoshi_pre_post_fork(self, payback=True):
        self.go_before_FCH(30)
        vault_id = self.get_new_vault_and_deposit()
        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "100@DOGE"})
        self.nodes[0].generate(1)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00019026@DOGE')

        self.go_to_FCH()

        self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': "100@DOGE"})
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.00951298@DOGE')
        # No more interest from loan pre FCH is accumulated, new calc is applied
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"][0], '0.01331815@DOGE')

        if not payback:
            return
        # Payback
        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': '200.01331815@DOGE'})
        self.nodes[0].generate(10)
        vault_data = self.nodes[0].getvault(vault_id)
        assert_equal(vault_data["interestAmounts"], [])



    def run_test(self):
        self.setup()

        # PRE FCH
        self.test_new_loan_with_interest_lower_than_1satoshi(payback=True)
        self.test_new_loan_with_interest_exactly_25satoshi(payback=True)
        self.test_new_loan_with_interest_over_1satoshi(payback=True)
        self.test_new_loan_with_interest_lower_than_1satoshi()
        self.test_new_loan_with_interest_exactly_25satoshi()
        self.test_new_loan_with_interest_over_1satoshi()
        # limit amounts pre FCH
        self.test_low_loan(payback=True)
        self.test_high_loan(payback=True)
        self.test_low_loan()
        self.test_high_loan()

        self.go_to_FCH()

        self.test_new_loan_with_interest_lower_than_1satoshi(payback=True)
        self.test_new_loan_with_interest_exactly_25satoshi(payback=True)
        self.test_new_loan_with_interest_over_1satoshi(payback=True)
        self.test_new_loan_with_interest_lower_than_1satoshi()
        self.test_new_loan_with_interest_exactly_25satoshi()
        self.test_new_loan_with_interest_over_1satoshi()
        # limit amounts post FCH
        self.test_low_loan(payback=True)
        self.test_high_loan(payback=True)
        self.test_low_loan()
        self.test_high_loan()

        self.test_1satoshi_loan_pre_post_fork(payback=True)
        self.test_new_loan_with_interest_lower_than_1satoshi_pre_post_fork(payback=True)
        self.test_new_loan_with_interest_over_1satoshi_pre_post_fork(payback=True)
        self.test_1satoshi_loan_pre_post_fork()
        self.test_new_loan_with_interest_lower_than_1satoshi_pre_post_fork()
        self.test_new_loan_with_interest_over_1satoshi_pre_post_fork()

if __name__ == '__main__':
    LowInterestTest().main()
