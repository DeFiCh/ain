#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getstoredinterest"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import assert_equal, assert_greater_than_or_equal

import calendar
import time
from decimal import ROUND_DOWN, Decimal

def getDecimalAmount(amount):
    amountTmp = amount.split('@')[0]
    return Decimal(amountTmp)

class GetStoredInterestTest (DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.greatworldheight = 700
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanningspringheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', f'-greatworldheight={self.greatworldheight}', '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet=1']
        ]

    # Utils
    def revert(self, block):
        blockhash = self.nodes[0].getblockhash(block)
        self.nodes[0].invalidateblock(blockhash)
        self.nodes[0].clearmempool()

    def newvault(self, loanScheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loanScheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit)+"@DFI")
        self.nodes[0].generate(1)
        return vaultId

    def goToGWHeight(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.greatworldheight > blockHeight:
            self.nodes[0].generate((self.greatworldheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["greatworld"]["active"], True)

    def goToSetupHeight(self):
        self.revert(self.setupHeight)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["greatworld"]["active"], False)

    # Default token = 1 = dUSD
    def setTokenInterest(self, token=1, interest=0):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{token}/loan_minting_interest':str(interest)}})
        self.nodes[0].generate(1)

    def update_vault_IPB_negative_ITH_positive(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB)*10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"])+expected_IPB_tmp, Decimal(storedInterest1["interestToHeight"]))

        # Set IPB negative and leave ITH possitive until paid
        self.setTokenInterest(interest=-52) # Total IPB should be -50%
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.5')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))
        self.nodes[0].generate(1)

        # after this update interest should be paid now ITH should be negative
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.51')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB)*10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.5')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"])+expected_IPB_tmp, Decimal(storedInterest1["interestToHeight"]))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_positive_ITH_negative(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=-5)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB)*10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.03')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"])+expected_IPB_tmp, Decimal(storedInterest1["interestToHeight"]))

        # Set IPB negative and leave ITH possitive until paid
        self.setTokenInterest(interest=48) # Total IPB should be 50%
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.5')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        self.nodes[0].generate(1)

        # after this update interest should be paid now ITH should be positive
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.49')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB)*10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.5')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"])+expected_IPB_tmp, Decimal(storedInterest1["interestToHeight"]))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_and_ITH_negative(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()+1
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=-5)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Force ITH update
        self.setTokenInterest(interest=-4)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.03')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal('-0.000000418569254185692533'))
        # Update vault
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest["interestPerBlock"] !=  storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB))
        # ITH not updated after one block
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest2["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_and_ITH_positive(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Force ITH update
        self.setTokenInterest(interest=0)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal('0.000000209284627092846261'))
        # Update vault
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest["interestPerBlock"] !=  storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB))
        # ITH not updated after one block
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest2["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_and_ITH_positive(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Take another loan
        loanAmount = 2
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest["interestPerBlock"] !=  storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB*11))

        # Repeat process with anotther loan
        self.nodes[0].generate(10)
        loanAmount = 3
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB2 = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest1["interestPerBlock"] !=  storedInterest2["interestPerBlock"])
        assert_equal(Decimal(storedInterest2["interestPerBlock"]), Decimal(expected_IPB2))
        assert_equal(Decimal(storedInterest2["interestToHeight"]), Decimal(storedInterest1["interestToHeight"])+Decimal(expected_IPB1*11))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_and_ITH_negative(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=-6)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.05')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.05')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Take another loan
        loanAmount = 2
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.05')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest["interestPerBlock"] !=  storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), Decimal(storedInterest["interestToHeight"])+Decimal(expected_IPB*11))

        # Repeat process with another loan
        self.nodes[0].generate(10)
        loanAmount = 3
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB2 = Decimal(Decimal('-0.05')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert(storedInterest1["interestPerBlock"] !=  storedInterest2["interestPerBlock"])
        assert_equal(Decimal(storedInterest2["interestPerBlock"]), Decimal(expected_IPB2))
        assert_equal(Decimal(storedInterest2["interestToHeight"]), Decimal(storedInterest1["interestToHeight"])+Decimal(expected_IPB1*11))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_positive_and_ITH_positive(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        self.nodes[0].generate(1)

        # Payback half the interest
        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': self.account0,
                    'amounts': f"{float(interestAmount) / 2}@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        expected_ITH = Decimal(expected_IPB * 2 - Decimal(interestAmount) / 2).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(Decimal(storedInterest["interestToHeight"]), expected_ITH)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), expected_IPB)

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        # Payback half the loan
        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': self.account0,
                    'amounts': f"{loanAmount / 2 + float(interestAmount)}@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02') / 2 / Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), 0)
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), (Decimal(storedInterest["interestPerBlock"]) / 2).quantize(Decimal('1E-24'), ROUND_DOWN))

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_positive_and_ITH_negative(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=-2)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(19)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 20

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB positive
        self.setTokenInterest(interest=2)
        self.nodes[0].generate(1)
        vault = self.nodes[0].getvault(vaultId)
        print("vault", vault)
        print("expected_IPB", expected_IPB)
        print("expected_ITH", expected_ITH)

        # Check positive IPB and negative ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert(Decimal(storedInterest["interestPerBlock"]) > 0)
        assert(Decimal(storedInterest["interestToHeight"]) < 0)

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")

        # Payback half the loan, check that negative interest is used first
        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': self.account0,
                    'amounts': f"{loanAmount / 2 + float(interestAmount)}@" + self.symboldUSD})
        self.nodes[0].generate(1)
        vault = self.nodes[0].getvault(vaultId)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.025') / Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(Decimal(storedInterest1["interestToHeight"]), 0) # negative interest was used for payback
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), expected_IPB)

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_negative_and_ITH_positive(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB negative
        self.setTokenInterest(interest=-4)
        self.nodes[0].generate(1)

        # Check positive ITH and negative IPB
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        expected_ITH = expected_ITH - expected_IPB
        assert_equal(Decimal(storedInterest["interestPerBlock"]), expected_IPB)
        assert_equal(Decimal(storedInterest["interestToHeight"]), expected_ITH)
        assert(Decimal(storedInterest["interestPerBlock"]) < 0)
        assert(Decimal(storedInterest["interestToHeight"]) > 0)

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert(float(interestAmount) > 0)
        loanAmount = vault["loanAmounts"][0]

        # Payback rest of the loan
        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': self.account0,
                    'amounts': loanAmount})
        self.nodes[0].generate(1)

        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest["interestToHeight"]), 0)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), 0)

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_negative_and_ITH_negative(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goToGWHeight()
        self.setTokenInterest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.newvault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
                    'vaultId': vaultId,
                    'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB negative
        self.setTokenInterest(interest=-4)
        self.nodes[0].generate(1)

        # Check negative IPB
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02')/Decimal(1051200)*loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))

        # Accrue enough negative interest to pay part of the loan
        self.nodes[0].generate(10)
        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert(float(interestAmount) < 0)
        loanAmount = vault["loanAmounts"][0]

        # Payback rest of the loan
        self.nodes[0].paybackloan({
                    'vaultId': vaultId,
                    'from': self.account0,
                    'amounts': loanAmount})
        self.nodes[0].generate(1)

        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest["interestToHeight"]), 0)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), 0)

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def templateFn(self, doRevert = True):
        blockHeight = self.nodes[0].getblockcount()
        # fn body

        if doRevert:
            self.revert(blockHeight)
            block = self.nodes[0].getblockcount()
            assert_equal(block+1, blockHeight)
            # further check for changes undone

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
                                    'interest': 0})
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
                                    'interest': 0})
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
        self.nodes[0].createloanscheme(150, 1, 'LOAN1')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(200, 2, 'LOAN2')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(300, 3, 'LOAN3')
        self.nodes[0].generate(10)
        self.setupHeight = self.nodes[0].getblockcount()

    def run_test(self):
        self.setup()
        # Update vault
        self.update_vault_IPB_and_ITH_positive()
        self.update_vault_IPB_and_ITH_negative()
        self.update_vault_IPB_negative_ITH_positive()
        self.update_vault_IPB_positive_ITH_negative()
        # Take loan
        self.takeloan_IPB_and_ITH_positive()
        self.takeloan_IPB_and_ITH_negative()
        #self.takeloan_IPB_negative_ITH_positive()
        #self.takeloan_IPB_positive_ITH_negative()

        self.payback_loan_IPB_positive_and_ITH_positive()
        self.payback_loan_IPB_positive_and_ITH_negative()
        self.payback_loan_IPB_negative_and_ITH_positive()
        self.payback_loan_IPB_negative_and_ITH_negative()

if __name__ == '__main__':
    GetStoredInterestTest().main()
