#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test negative interest"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time
from decimal import ROUND_DOWN, Decimal

class NegativeInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanningspringheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet=1']
        ]

    def createTokens(self):
        self.symbolDFI = "DFI"
        self.symboldUSD = "DUSD"
        self.symbolTSLA = "TSLA"

        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]

        self.nodes[0].setcollateraltoken({
                                    'token': self.idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(120)

        self.nodes[0].setloantoken({
                                    'symbol': self.symboldUSD,
                                    'name': "DUSD stable token",
                                    'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': -1})
        self.nodes[0].generate(120)
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]

        self.nodes[0].setloantoken({
                                    'symbol': self.symbolTSLA,
                                    'name': "TSLA token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': True,
                                    'interest': 0})
        self.nodes[0].generate(120)
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

    def createOracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DUSD"},
                        {"currency": "USD", "token": "TSLA"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                          {"currency": "USD", "tokenAmount": "1@DUSD"},
                          {"currency": "USD", "tokenAmount": "10@DFI"}]

        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle_prices)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)

    def setup(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI
        self.nodes[0].generate(100)
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.createOracles()
        self.createTokens()

    # Loan scheme interest->1% and loantoken interest-> -1%
    # Resulting interest must be 0.
    def vault_interest_zero(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN1')
        self.nodes[0].generate(1)

        # Init vault (create, deposit takeloan)
        self.nodes[0].utxostoaccount({self.account0: "4000@DFI"})
        # Create
        self.vaultId0 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId0, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId0,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId0, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@DUSD"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@DUSD"])

        # Generate some more blocks and check interest continues at 0
        self.nodes[0].generate(100)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId0, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@DUSD"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@DUSD"])

    # Loan scheme interest->2% and loan token interest-> -1%
    # Resulting interest must be 1%
    def vault_interest_over_zero_and_lower_than_loanscheme(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(155, 2, 'LOAN2')
        self.nodes[0].generate(1)
        # Init vault (create, deposit takeloan)
        # Create
        self.vaultId1 = self.nodes[0].createvault(self.account0, 'LOAN2')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId1, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId1,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check interests = 1%
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId1, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000001@DUSD"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0.000000009512937595129375'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('1E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000009512937595129375@DUSD"])

    # Loan scheme interest->2% and loan token interest->0%
    # Resulting interest must be 2%
    def vault_loantoken_interest_zero(self):
        # Init vault (create, deposit takeloan)
        # Create
        self.vaultId2 = self.nodes[0].createvault(self.account0, 'LOAN2')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId2, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId2,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        # Check interests = 2%
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId2, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000002@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0.000000190258751902587510'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('2E-7'))
        assert_equal(vault["interestsPerBlock"], ["0.000000019025875190258751@TSLA"])

    # Loan scheme interest->1% and loan token interest-> -3%
    # Resulting interest must be 0%
    def vault_with_negative_interest(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)
        # Init vault (create, deposit takeloan)
        # Create
        self.vaultId3 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId3, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId3,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId3, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])

    # Loan scheme interest->1% and loan token interest-> -30%
    # Resulting interest must be 0%
    def vault_with_big_negative_interest(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-30'}})
        self.nodes[0].generate(1)
        # Init vault (create, deposit takeloan)
        # Create
        self.vaultId4 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId4, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId4,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId4, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])

    # Loan scheme interest -> 1% and loan token -> 30%
    # Resulting interest -> 31%
    def takeloan_after_interest_increase(self):
        # Increase previously -30% interest -> +30%
        self.vaultId5 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId5, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        loanAmount = 1
        totalLoanAmount = loanAmount
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId5,
                    'amounts': str(loanAmount) + "@" + self.symbolTSLA})
        self.nodes[0].generate(1)
        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId4, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])


        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'30'}})
        self.nodes[0].generate(1)
        loanAmount = 1
        totalLoanAmount += loanAmount
        expected_IPB = Decimal(Decimal('0.31')/Decimal(1051200)*totalLoanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)

        # Use same vault as previous case
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId5,
                    'amounts': str(loanAmount) +"@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId5, verbose)
        interestPerBlockAmount = Decimal(vault["interestsPerBlock"][0].split('@')[0])
        assert_equal(interestPerBlockAmount, expected_IPB)

    # Loan scheme interest -> 1% and loan token -> 30%
    # Resulting interest -> 31%
    def takeloan_after_interest_decrease(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-30'}})
        self.nodes[0].generate(1)
        # Increase previously -30% interest -> +30%
        self.vaultId6 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        # Deposit
        self.nodes[0].deposittovault(self.vaultId6, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        # Take loan
        loanAmount = 1
        totalLoanAmount = loanAmount
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId6,
                    'amounts': str(loanAmount) + "@" + self.symbolTSLA})
        self.nodes[0].generate(1)
        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId4, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])


        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-50'}})
        self.nodes[0].generate(1)
        loanAmount = 1
        totalLoanAmount += loanAmount

        # Use same vault as previous case
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId6,
                    'amounts': str(loanAmount) +"@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])

    # Try payback of interest zero vault
    def payback_interest_zero(self):
        # Use previous vault -> vaultId6 with -49% interest (0% interest)
        self.nodes[0].paybackloan({
                        'vaultId': self.vaultId6,
                        'from': self.account0,
                        'amounts': ["1@" + self.symbolTSLA]})
        self.nodes[0].generate(1)

        # Check interests = 0
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('0E-16'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["0.000000000000000000000000@TSLA"])
        assert_equal(vault["loanAmounts"], ["1.00000000@TSLA"])

    # Test withdrawfromvault after setting back negative interest
    def withdraw_interest_zero(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-50'}})
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId5, verbose)
        accountInfo = self.nodes[0].getaccount(self.account0)
        assert_equal(vault["collateralAmounts"], ['10.00000000@DFI'])
        assert_equal(accountInfo[0], ['3930.00000000@DFI'])

        self.nodes[0].withdrawfromvault(self.vaultId5, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId5, verbose)
        accountInfo = self.nodes[0].getaccount(self.account0)
        assert_equal(vault["collateralAmounts"], ['9.00000000@DFI'])
        assert_equal(accountInfo[0], ['3931.00000000@DFI'])

    # Increase interest of previous vault and try to payback with interest > 0
    # Loan scheme interest -> 1%
    # Token interest -> 0%
    # Resulting interest -> 1%
    def payback_after_interest_increase(self):
        # Set token interest -> 0%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Acrue interest for 10 blocks
        self.nodes[0].generate(10)
        # Check interests
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # This should be NOT 0 as interest of the total interest should be 1%
        assert( vault["interestsPerBlockValue"] != Decimal(0))

    def run_test(self):
        self.setup()

        self.vault_interest_zero()
        self.vault_interest_over_zero_and_lower_than_loanscheme()
        self.vault_loantoken_interest_zero()
        self.vault_with_negative_interest()
        self.vault_with_big_negative_interest()
        self.takeloan_after_interest_increase()
        self.takeloan_after_interest_decrease()
        self.payback_interest_zero()
        self.withdraw_interest_zero()
        # Failing! interests not accrued.
        self.payback_after_interest_increase()


if __name__ == '__main__':
    NegativeInterestTest().main()
