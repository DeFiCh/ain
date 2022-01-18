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
from decimal import Decimal, getcontext, ROUND_UP
class Loan():
    def __init__(self, amount, vaultId, loanToken, height=0, returnedInterest=0, interestPerBlock=0):
        self.__amount = amount
        self.__height = height
        self.__returnedInterest = returnedInterest
        self.__vaultId = vaultId
        self.__loanToken = loanToken
        self.__interestPerBlock = interestPerBlock
        self.__isLoanTaken = False

    def getAmount(self): return self.__amount
    def getHeight(self): return self.__height
    def getReturnedInterest(self): return self.__returnedInterest
    def getVaultId(self): return self.__vaultId
    def getLoanToken(self): return self.__loanToken
    def getInterestPerBlock(self): return self.__interestPerBlock

    def setReturnedInterest(self, returnedInterest):
        self.__returnedInterest = returnedInterest

    def takeLoan(self, node):
        loanAmount = str(self.__amount) + "@" + self.__loanToken
        node.takeloan({
                    'vaultId': self.__vaultId,
                    'amounts': loanAmount})
        self.__height = node.getblockcount()+1
        self.__isLoanTaken = True

    def getBlocksSinceLoan(self, node):
        return (node.getblockcount() - self.__height) + 1

    # only add FCHheight and currentHeight when recalculation needs to be done
    def calculateInterestPerBlock(self, tokenInterest, loanInterest, blocksPerDay):
        if self.__isLoanTaken:
            self.__interestPerBlock = self.__amount * ((loanInterest + tokenInterest) / (blocksPerDay * 365))
        else:
            raise Exception("Loan has not been taken")

class LowInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=2', '-fortcanningheight=3', '-fortcanningmuseumheight=4', '-fortcanningparkheight=5', '-fortcanninghillheight=132', '-jellyfish_regtest=1', '-txindex=1']
        ]

    blocksPerDay = Decimal('144')
    account0 = ''
    symbolDFI = "DFI"
    symbolDOGE = "DOGE"
    symboldUSD = "DUSD"
    idDFI = 0
    iddUSD = 0
    idDOGE = 0
    tokenInterest = 0
    loanInterest = 0
    loans = []
    FCHheight = 0

    getcontext().prec = 8
    def tokenInterestPercentage(self):
        return Decimal(self.tokenInterest/100)

    def loanInterestPercentage(self):
        return Decimal(self.loanInterest/100)

    def setup(self):
        print('Generating initial chain...')
        self.FCHheight = self.nodes[0].getblockchaininfo()["softforks"]["fortcanninghill"]["height"]
        self.nodes[0].generate(100) # get initial UTXO balance from immature to trusted -> check getbalances()
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # UTXO -> token
        self.nodes[0].utxostoaccount({self.account0: "10000000@" + self.symbolDFI})
        self.nodes[0].generate(1)

        # Setup oracles
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DOGE"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "0.01@DOGE"},
                          {"currency": "USD", "tokenAmount": "10@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        # Setup loan tokens
        self.nodes[0].setloantoken({
                    'symbol': self.symboldUSD,
                    'name': "DUSD stable token",
                    'fixedIntervalPriceId': "DUSD/USD",
                    'mintable': True,
                    'interest': 0})

        self.tokenInterest = 1

        self.nodes[0].setloantoken({
                    'symbol': self.symbolDOGE,
                    'name': "DOGE token",
                    'fixedIntervalPriceId': "DOGE/USD",
                    'mintable': True,
                    'interest': self.tokenInterest})
        self.nodes[0].generate(1)

        # Set token ids
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDOGE = list(self.nodes[0].gettoken(self.symbolDOGE).keys())[0]

        # Mint tokens
        self.nodes[0].minttokens("100000@DOGE")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("2000000@" + self.symboldUSD) # necessary for pools
        self.nodes[0].generate(1)

        # Setup collateral tokens
        self.nodes[0].setcollateraltoken({
                    'token': self.idDFI,
                    'factor': 1,
                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        # Setup pools
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
            self.account0: ["1000000@" + self.symboldUSD, "100000@" + self.symbolDFI]
        }, self.account0, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["1000000@" + self.symboldUSD, "1000@" + self.symbolDOGE]
        }, self.account0, [])
        self.nodes[0].generate(1)
        self.loanInterest = 1;

        # Create loan schemes and vaults
        self.nodes[0].createloanscheme(150, self.loanInterest, 'LOAN150')
        self.nodes[0].generate(1)


    # Interest amount per block is under 1Sat so it is rounded to 1Sat and added in each block
    def interest_under_one_satoshi_pre_FCH(self):
        # Init vault
        vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        # Init loan
        loan = Loan(amount=Decimal('0.00657'),
                    vaultId=vaultId,
                    loanToken=self.symbolDOGE,
                    interestPerBlock=Decimal('0.00000001'))

        loan.takeLoan(self.nodes[0])
        self.nodes[0].generate(1)

        # Generate interest over 5 blocks
        self.nodes[0].generate(5)

        vault = self.nodes[0].getvault(loan.getVaultId())

        blocksSinceLoan = loan.getBlocksSinceLoan(self.nodes[0])
        interestAmount = Decimal(blocksSinceLoan) * loan.getInterestPerBlock()
        returnedInterest = Decimal(vault["interestAmounts"][0].split('@')[0])
        assert_equal(returnedInterest, interestAmount)

        # save loan for later use
        loan.setReturnedInterest(returnedInterest)
        self.loans.append(loan)

    # interest generated by loan is higher than 1Sat so calculations must be the same as post-fork
    def interest_over_one_satoshi_pre_FCH(self):
        # Init vault
        vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        # Init loan
        loan = Loan(amount=Decimal('0.657'),
                    vaultId=vaultId,
                    loanToken=self.symbolDOGE)

        loan.takeLoan(self.nodes[0])
        self.nodes[0].generate(1)

        loan.calculateInterestPerBlock(self.loanInterestPercentage(), self.tokenInterestPercentage(), self.blocksPerDay)

        # Generate interest over 5 blocks
        self.nodes[0].generate(5)

        vault = self.nodes[0].getvault(loan.getVaultId())

        blocksSinceLoan = loan.getBlocksSinceLoan(self.nodes[0])
        interestAmount = Decimal(blocksSinceLoan) * loan.getInterestPerBlock()
        returnedInterest = Decimal(vault["interestAmounts"][0].split('@')[0])
        assert_equal(returnedInterest, interestAmount)

        # save loan for later use
        loan.setReturnedInterest(returnedInterest)
        self.loans.append(loan)

    # Interest amount per block is under 1Sat interest is kept to 0.25 Sat/block and only total interest is ceiled
    def interest_under_one_satoshi_post_FCH(self):
        # Go to block 134 and check FCH is active
        self.nodes[0].generate(7)
        blockChainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockChainInfo["softforks"]["fortcanninghill"]["active"], True)

        # Init vault
        vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        # Init loan
        loan = Loan(amount=Decimal('0.00657'), vaultId=vaultId, loanToken=self.symbolDOGE)
        loan.takeLoan(self.nodes[0])
        self.nodes[0].generate(1)

        loan.calculateInterestPerBlock(self.loanInterestPercentage(), self.tokenInterestPercentage(), self.blocksPerDay)


        self.nodes[0].generate(5)
        vault = self.nodes[0].getvault(loan.getVaultId())

        blocksSinceLoan = loan.getBlocksSinceLoan(self.nodes[0])
        interestAmount = Decimal(blocksSinceLoan) * loan.getInterestPerBlock()
        returnedInterest = Decimal(vault["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountCeil = interestAmount.quantize(Decimal('1E-8'), rounding=ROUND_UP)
        assert_equal(returnedInterest, interestAmountCeil)

        # save loan for later use
        loan.setReturnedInterest(returnedInterest)
        self.loans.append(loan)

    # Interest generated by loan is higher than 1Sat so calculations must be the same as pre-fork
    def interest_over_one_satoshi_post_FCH(self):
        # Init vault
        vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        # Init loan
        loan = Loan(amount=Decimal('0.657'), vaultId=vaultId, loanToken=self.symbolDOGE)
        loan.takeLoan(self.nodes[0])
        self.nodes[0].generate(1)

        loan.calculateInterestPerBlock(self.loanInterestPercentage(), self.tokenInterestPercentage(), self.blocksPerDay)


        self.nodes[0].generate(5)
        vault = self.nodes[0].getvault(loan.getVaultId())

        blocksSinceLoan = loan.getBlocksSinceLoan(self.nodes[0])
        interestAmount = Decimal(blocksSinceLoan) * loan.getInterestPerBlock()
        returnedInterest = Decimal(vault["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountCeil = interestAmount.quantize(Decimal('1E-8'), rounding=ROUND_UP)
        assert_equal(returnedInterest, interestAmountCeil)

        # save loan for later use
        loan.setReturnedInterest(returnedInterest)
        self.loans.append(loan)

        # compare results of pre/post fork calculations when interest per block is bigger than 1Sat
        assert_equal(self.loans[1].getReturnedInterest(), loan.getReturnedInterest())

    # Take another loan in vault0 post fork and see how interests add up
    # (1Sat + Loan4 interest) will be added each block
    def interest_adding_pre_and_post_FCH(self):
        loan0 = self.loans[0]
        # Get vault0
        vaultId = loan0.getVaultId()

        # Init loan
        loan = Loan(amount=Decimal('0.657'), vaultId=vaultId, loanToken=self.symbolDOGE)
        loan.takeLoan(self.nodes[0])
        self.nodes[0].generate(1)

        loan.calculateInterestPerBlock(self.loanInterestPercentage(), self.tokenInterestPercentage(), self.blocksPerDay)
        loan0.calculateInterestPerBlock(self.loanInterestPercentage(), self.tokenInterestPercentage(), self.blocksPerDay)

        self.nodes[0].generate(5)
        vault = self.nodes[0].getvault(loan.getVaultId())

        # calculate interests pre FCH for loan0
        blocksPreFCHLoan0 = self.FCHheight - loan0.getHeight() + 1
        interestPreFCHLoan0 = Decimal(blocksPreFCHLoan0) * Decimal("0.00000001")
        # calculate interest post FCH for loan0
        blocksPostFCHLoan0 = self.nodes[0].getblockcount() - self.FCHheight
        interestPostFCHLoan0 = Decimal(blocksPostFCHLoan0) * loan0.getInterestPerBlock()
        # total interests for loan0
        interestAmount0 = interestPostFCHLoan0 + interestPreFCHLoan0

        # calculate interest for current loan post FCH
        blocksSinceLoan = loan.getBlocksSinceLoan(self.nodes[0])
        interestAmount = Decimal(blocksSinceLoan) * loan.getInterestPerBlock()
        # total interests loan0 and current loan
        totalInterest = interestAmount0 + interestAmount

        returnedInterest = Decimal(vault["interestAmounts"][0].split('@')[0])
        assert_equal(returnedInterest, totalInterest)
        # save loan for later use
        loan.setReturnedInterest(returnedInterest)
        self.loans.append(loan)

    # Payback full loan and test vault keeps empty loan and interest amounts
    def payback_pre_FCH(self):
        # Get loans data
        loan0 = self.loans[0]
        loan4 = self.loans[4]
        vaultId = loan4.getVaultId() # same as loan0

        # calculate interests pre FCH for loan0
        blocksPreFCHLoan0 = self.FCHheight - loan0.getHeight() + 1
        interestPreFCHLoan0 = Decimal(blocksPreFCHLoan0) * Decimal("0.00000001")
        # calculate interest post FCH for loan0
        blocksPostFCHLoan0 = self.nodes[0].getblockcount() - self.FCHheight
        interestPostFCHLoan0 = Decimal(blocksPostFCHLoan0) * loan0.getInterestPerBlock()
        # total interests for loan0
        interestAmount0 = interestPostFCHLoan0 + interestPreFCHLoan0

        blocksSinceLoan4 = loan4.getBlocksSinceLoan(self.nodes[0])
        interestAmount4 = Decimal(blocksSinceLoan4) * loan4.getInterestPerBlock()

        paybackAmount = loan0.getAmount() + interestAmount0 + loan4.getAmount() + interestAmount4

        vault = self.nodes[0].getvault(vaultId)
        assert_equal(paybackAmount, Decimal(vault["loanAmounts"][0].split('@')[0]))

        self.nodes[0].paybackloan({
                        'vaultId': vaultId,
                        'from': self.account0,
                        'amounts': [str(paybackAmount) +"@" + loan4.getLoanToken()]})
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vaultId)
        # Check loan is fully payed back
        assert_equal(vault["loanAmounts"], [])
        assert_equal(vault["interestAmounts"], [])
        # Generate blocks and check again
        self.nodes[0].generate(5)
        vault = self.nodes[0].getvault(vaultId)
        # Check loan is fully payed back
        assert_equal(vault["loanAmounts"], [])
        assert_equal(vault["interestAmounts"], [])

    def dont_accumulate_on_paid_loan(self):
        # generate some blocks to see if interest gets accumulated
        self.nodes[0].generate(10)

        # Create new vault to compare interest calculation
        vaultIdPostFCH = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultIdPostFCH, self.account0, "1@DFI")
        self.nodes[0].generate(1)

        # Get vault0 which has a paid loan
        loan0 = self.loans[0]
        vaultIdPaid = loan0.getVaultId()

        # Init loans
        loanPaid = Loan(amount=Decimal('0.657'), vaultId=vaultIdPaid, loanToken=self.symbolDOGE)
        loanPostFCH = Loan(amount=Decimal('0.657'), vaultId=vaultIdPostFCH, loanToken=self.symbolDOGE)
        loanPaid.takeLoan(self.nodes[0])
        loanPostFCH.takeLoan(self.nodes[0])

        self.nodes[0].generate(5)
        vaultPaid = self.nodes[0].getvault(vaultIdPaid)
        vaultPostFCH = self.nodes[0].getvault(vaultIdPostFCH)
        assert_equal(vaultPaid["loanAmounts"], vaultPostFCH["loanAmounts"])
        assert_equal(vaultPaid["interestAmounts"], vaultPostFCH["interestAmounts"])

    def run_test(self):

        self.setup()

        # pre FCH
        self.interest_under_one_satoshi_pre_FCH()
        self.interest_over_one_satoshi_pre_FCH()
        # post FCH
        self.interest_under_one_satoshi_post_FCH()
        self.interest_over_one_satoshi_post_FCH()
        self.interest_adding_pre_and_post_FCH()
        # Payback
        self.payback_pre_FCH()
        # Take loan on paid back vault
        self.dont_accumulate_on_paid_loan()


if __name__ == '__main__':
    LowInterestTest().main()
