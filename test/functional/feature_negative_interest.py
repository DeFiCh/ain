#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test negative interest"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_greater_than

import calendar
import time
from decimal import ROUND_DOWN, Decimal

def getDecimalAmount(amount):
    amountTmp = amount.split('@')[0]
    return Decimal(amountTmp)


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
        self.symbolGOLD = "GOLD"

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
        self.nodes[0].generate(1)
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.nodes[0].setloantoken({
                                    'symbol': self.symbolGOLD,
                                    'name': "GOLD token",
                                    'fixedIntervalPriceId': "GOLD/USD",
                                    'mintable': True,
                                    'interest': -3})
        self.nodes[0].generate(1)
        self.idGOLD = list(self.nodes[0].gettoken(self.symbolGOLD).keys())[0]
        self.nodes[0].minttokens("1000@GOLD")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("1000@TSLA")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("10000@DUSD")
        self.nodes[0].generate(1)
        toAmounts = {self.account0: ["10000@DUSD", "1000@TSLA", "1000@GOLD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)
        self.nodes[0].utxostoaccount({self.account0: "10000@0"})
        self.nodes[0].generate(1)


    def createOracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DUSD"},
                        {"currency": "USD", "token": "GOLD"},
                        {"currency": "USD", "token": "TSLA"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                          {"currency": "USD", "tokenAmount": "1@DUSD"},
                          {"currency": "USD", "tokenAmount": "10@GOLD"},
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

    def createPoolPairs(self):
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "DFI-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolTSLA,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "TSLA-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolGOLD,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "GOLD-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@TSLA", "1000@DUSD"]
        }, self.account0)
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@DFI", "1000@DUSD"]
        }, self.account0)
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@GOLD", "1000@DUSD"]
        }, self.account0)
        self.nodes[0].generate(1)

    def setup(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI
        self.nodes[0].generate(100)
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.createOracles()
        self.createTokens()
        self.createPoolPairs()

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
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-1.90258751902587510E-7'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000019025875190258751@TSLA"])

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
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-0.000002758751902587519020'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000275875190258751902@TSLA"])

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
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-0.000002758751902587519020'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000275875190258751902@TSLA"])


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
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestAmounts"], ["0.00000000@TSLA"])
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-0.000002758751902587519020'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000275875190258751902@TSLA"])


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
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-0.000009322676652397260270'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000932267665239726027@TSLA"])

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
        assert_equal(Decimal(vault["interestPerBlockValue"]), Decimal('-0.000004661332849124809740'))
        assert_equal(Decimal(vault["interestValue"]), Decimal('0E-8'))
        assert_equal(vault["interestsPerBlock"], ["-0.000000466133284912480974@TSLA"])
        assert_equal(vault["loanAmounts"], ["0.99999812@TSLA"])

    # Test withdrawfromvault after setting back negative interest
    def withdraw_interest_zero(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-50'}})
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId5, verbose)
        accountInfo = self.nodes[0].getaccount(self.account0)
        assert_equal(vault["collateralAmounts"], ['10.00000000@DFI'])
        assert_equal(accountInfo[0], '13830.00000000@DFI')

        self.nodes[0].withdrawfromvault(self.vaultId5, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId5, verbose)
        accountInfo = self.nodes[0].getaccount(self.account0)
        assert_equal(vault["collateralAmounts"], ['9.00000000@DFI'])
        assert_equal(accountInfo[0], '13831.00000000@DFI')

    # Increase interest of previous vault and try to payback with interest > 0
    # Loan scheme interest -> 1%
    # Token interest -> 0%
    # Resulting interest -> 1%
    def payback_after_interest_increase(self):
        # Check interest is zero
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '-0.000004661332849124809740')

        # Set token interest -> 0%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Check interests
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        print(vault["interestPerBlockValue"])

        # This should be NOT 0 as interest of the total interest should be 1%
        assert_equal(vault["interestPerBlockValue"], '0.000000190258483637747330')

    # Set total interest to zero and increase after some blocks to positive interest to test withdraw
    def check_interest_amounts_after_interest_changes(self):
        # Total interest: 1% (loan scheme) -1% (token) = 0%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')
        assert_equal(vault["interestAmounts"], ['0.00000002@TSLA'])

        # Generate some blocks to accrue interest
        self.nodes[0].generate(10)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')
        assert_equal(vault["interestAmounts"], ['0.00000002@TSLA'])

        # Set token interest to 0%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'0'}})
        self.nodes[0].generate(1)

        # Now total interest should be 1%
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129241818873660')
        assert_equal(vault["interestAmounts"], ['0.00000003@TSLA'])

        # One block interest is added correctly
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129241818873660')
        assert_equal(vault["interestAmounts"], ['0.00000004@TSLA'])

        # 10 blocks interest is added correctly
        self.nodes[0].generate(10)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129241818873660')
        assert_equal(vault["interestAmounts"], ['0.00000014@TSLA'])

        # check rounding up to 1 sat is working
        self.nodes[0].generate(80)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129241818873660')
        assert_equal(vault["interestAmounts"], ['0.00000090@TSLA'])

    # Updating vault should put interest to 0% and -1% after
    def updates_of_vault_scheme(self):
        # Total interest: 3% (loan scheme) -2% (token) = 1%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-2'}})
        self.nodes[0].createloanscheme(175, 3, 'LOAN3')
        self.nodes[0].generate(1)
        self.nodes[0].updatevault(self.vaultId6, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        self.nodes[0].generate(1)
        assert_equal(vault["interestPerBlockValue"], '-0.000000095129241818873660')

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # Same as previous IPB
        assert_equal(vault["interestPerBlockValue"], '-0.000000095129241818873660')

        # After takeloan vault is updated and new interest per block are set
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId6,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)

        assert_equal(vault["interestPerBlockValue"], '0.000000190258617770167420')
        assert_equal(vault["interestAmounts"], ['0.00000089@TSLA'])

        # Generate some blocks to check IPB behaviour
        self.nodes[0].generate(10)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000190258617770167420')

        # Update vault to LOAN1 (1%) so overall interest becomes negative -1%
        self.nodes[0].updatevault(self.vaultId6, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # Does not update until new loan is taken or payback
        assert_equal(vault["interestPerBlockValue"], '0.000000190258617770167420')

        # After takeloan vault is updated and new interest per block are set
        self.nodes[0].paybackloan({
                        'vaultId': self.vaultId6,
                        'from': self.account0,
                        'amounts': ["1@" + self.symbolTSLA]})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # Updated after payback
        assert_equal(vault["interestPerBlockValue"], '-0.000000095129346461187210')

        self.nodes[0].takeloan({
                    'vaultId': self.vaultId6,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '-0.000000190258721461187210')

        # Update vault again and test payback
        self.nodes[0].updatevault(self.vaultId6, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # This should not be negative anymore
        assert_equal(vault["interestPerBlockValue"], '-0.000000190258721461187210')

        self.nodes[0].paybackloan({
                        'vaultId': self.vaultId6,
                        'from': self.account0,
                        'amounts': ["1@" + self.symbolTSLA]})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)

        # IPB is updated again after paybackloan
        assert_equal(vault["interestPerBlockValue"], '0.000000095129341704718410')

        # Check same with withdrawfromvault action
        self.nodes[0].updatevault(self.vaultId6, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129341704718410')

        # Withdraw does not update the IPB
        self.nodes[0].withdrawfromvault(self.vaultId6, self.account0, "1@DFI")
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129341704718410')

        # Check same with deposittovault action
        self.nodes[0].deposittovault(self.vaultId6, self.account0, '1@DFI')
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000095129341704718410')

        # Update IPB with paybackloan
        self.nodes[0].paybackloan({
                        'vaultId': self.vaultId6,
                        'from': self.account0,
                        'amounts': ["0.1@" + self.symbolTSLA]})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId6, verbose)
        # IPB is updated again after paybackloan
        assert_equal(vault["interestPerBlockValue"], '-0.000000085616407914764070')

    # Check total interest with different loans taken
    def total_interest_with_multiple_takeloan(self):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-2'}})
        self.nodes[0].generate(1)
        self.vaultId7 = self.nodes[0].createvault(self.account0, 'LOAN1')
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')

        self.nodes[0].deposittovault(self.vaultId7, self.account0, "10@DFI")
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')
        # Update with token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')

        # Take loan 1 and accrue interest at 0% (loan scheme interest 1% - token interest -1%)
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId7,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(10)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')

        self.nodes[0].updatevault(self.vaultId7, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        # now total interest should be 2%
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')

        # IPB update through takeloan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId7,
                    'amounts': "1@" + self.symbolTSLA})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000380517503805175030')

        # Take GOLD loan
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId7,
                    'amounts': "1@" + self.symbolGOLD})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000380517503805175030')
        assert_equal(vault["interestsPerBlock"], ['0.000000038051750380517503@TSLA','0.000000000000000000000000@GOLD'])
        # Update loan scheme to 1%.
        # TSLA loan interest -> -1%
        # GOLD loan interest -> -2%
        self.nodes[0].updatevault(self.vaultId7, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000380517503805175030')
        assert_equal(vault["interestsPerBlock"], ['0.000000038051750380517503@TSLA','0.000000000000000000000000@GOLD'])

        # Update GOLD interest to 1 so total GOLD loan has positive interest 2%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idGOLD}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        # Only GOLD loan should update
        assert_equal(vault["interestPerBlockValue"], '0.000000570776255707762540')
        assert_equal(vault["interestsPerBlock"], ['0.000000038051750380517503@TSLA','0.000000019025875190258751@GOLD'])

    # Check vault limit states with negative interest
    def vault_status_with_negative_interest(self):
        # Drain vault with different actions
        self.nodes[0].withdrawfromvault(self.vaultId7, self.account0, "4@DFI")
        self.nodes[0].generate(1)

        # Take absolut maximum amount of loan to leave the vault about to enter liquidation
        self.nodes[0].takeloan({
                    'vaultId': self.vaultId7,
                    'amounts': "0.43839517@" + self.symbolGOLD})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["interestPerBlockValue"], '0.000000654184773592085220')

        # Generate some blocks to put vault into liquidation
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["state"], 'mayLiquidate')

        # set total interest to 0%
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idGOLD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["state"], 'mayLiquidate')
        assert_equal(vault["interestPerBlockValue"], '0.000000000000000000000000')

        # set total interest to -10% and check if exits mayLiquidate state
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idTSLA}/loan_minting_interest':'-10'}})
        self.nodes[0].generate(1)
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idGOLD}/loan_minting_interest':'-10'}})
        self.nodes[0].generate(1)

        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        assert_equal(vault["state"], 'active')
        assert_equal(vault["interestPerBlockValue"], '-0.000002289646707572298320')

    def payback_interests_and_continue_with_negative_interest(self):
        self.nodes[0].paybackloan({
                        'vaultId': self.vaultId7,
                        'from': self.account0,
                        'amounts': ["0.00001000@" + self.symbolTSLA]})
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        amounts0 = []
        for amount in vault["loanAmounts"]:
            amounts0.append(getDecimalAmount(amount))
        self.nodes[0].generate(1)
        verbose = True
        vault = self.nodes[0].getvault(self.vaultId7, verbose)
        amounts1 = []
        for amount in vault["loanAmounts"]:
            amounts1.append(getDecimalAmount(amount))

        for amount in amounts0:
            print(amount, amounts1[amounts0.index(amount)])
            assert_greater_than(amounts1[amounts0.index(amount)], amount)

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
        self.payback_after_interest_increase()
        self.check_interest_amounts_after_interest_changes()
        self.updates_of_vault_scheme()
        self.total_interest_with_multiple_takeloan()
        self.vault_status_with_negative_interest()
        self.payback_interests_and_continue_with_negative_interest()


if __name__ == '__main__':
    NegativeInterestTest().main()
