#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getstoredinterest"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    get_decimal_amount,
)

import calendar
import time
from decimal import ROUND_DOWN, Decimal

BLOCKS_PER_YEAR = Decimal(1051200)


class GetStoredInterestTest(DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.fortcanninggreatworldheight = 700
        self.extra_args = [
            ['-txnotokens=0',
             '-amkheight=1',
             '-bayfrontheight=1',
             '-eunosheight=1',
             '-fortcanningheight=1',
             '-fortcanningmuseumheight=1',
             '-fortcanningparkheight=1',
             '-fortcanninghillheight=1',
             '-fortcanningroadheight=1',
             '-fortcanningcrunchheight=1',
             '-fortcanningspringheight=1',
             f'-fortcanninggreatworldheight={self.fortcanninggreatworldheight}',
             '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet=1']
        ]

    def new_vault(self, loan_scheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DFI")
        self.nodes[0].generate(1)
        return vaultId

    def goto_gw_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanninggreatworldheight > blockHeight:
            self.nodes[0].generate((self.fortcanninggreatworldheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanninggreatworld"]["active"], True)

    def goto_setup_height(self):
        self.rollback_to(self.setup_height)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanninggreatworld"]["active"], False)

    # Default token = 1 = dUSD
    def set_token_interest(self, token='1', interest=0):
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{token}/loan_minting_interest': str(interest)}})
        self.nodes[0].generate(1)

    def update_vault_IPB_negative_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]) + expected_IPB_tmp,
                     Decimal(storedInterest1["interestToHeight"]))

        # Set IPB negative and leave ITH possitive until paid
        self.set_token_interest(interest=-52)  # Total IPB should be -50%
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.5') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))
        self.nodes[0].generate(1)

        # after this update interest should be paid now ITH should be negative
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.51') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.5') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]) + expected_IPB_tmp,
                     Decimal(storedInterest1["interestToHeight"]))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_positive_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-5)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.03') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                   ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]) + expected_IPB_tmp,
                     Decimal(storedInterest1["interestToHeight"]))

        # Set IPB negative and leave ITH possitive until paid
        self.set_token_interest(interest=48)  # Total IPB should be 50%
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.5') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        self.nodes[0].generate(1)

        # after this update interest should be paid now ITH should be positive
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN1'})
        self.nodes[0].generate(1)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.49') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_greater_than_or_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_IPB_tmp = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.5') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]) + expected_IPB_tmp,
                     Decimal(storedInterest1["interestToHeight"]))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_and_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-5)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.04') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Force ITH update
        self.set_token_interest(interest=-4)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.03') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal('-0.000000418569254185692533'))
        # Update vault
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('-0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                   ROUND_DOWN)
        assert (storedInterest["interestPerBlock"] != storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]),
                     Decimal(storedInterest["interestToHeight"]) + Decimal(expected_IPB))
        # ITH not updated after one block
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest2["interestToHeight"]),
                     Decimal(storedInterest["interestToHeight"]) + Decimal(expected_IPB))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def update_vault_IPB_and_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Force ITH update
        self.set_token_interest(interest=0)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal('0.000000209284627092846261'))
        # Update vault
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert (storedInterest["interestPerBlock"] != storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]),
                     Decimal(storedInterest["interestToHeight"]) + Decimal(expected_IPB))
        # ITH not updated after one block
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterest2["interestToHeight"]),
                     Decimal(storedInterest["interestToHeight"]) + Decimal(expected_IPB))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_and_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Take another loan
        loanAmount = 2
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB1 = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert (storedInterest["interestPerBlock"] != storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(Decimal(storedInterest1["interestToHeight"]),
                     Decimal(storedInterest["interestToHeight"]) + Decimal(expected_IPB * 11))

        # Repeat process with anotther loan
        self.nodes[0].generate(10)
        loanAmount = 3
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB2 = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert (storedInterest1["interestPerBlock"] != storedInterest2["interestPerBlock"])
        assert_equal(Decimal(storedInterest2["interestPerBlock"]), Decimal(expected_IPB2))
        assert_equal(Decimal(storedInterest2["interestToHeight"]),
                     Decimal(storedInterest1["interestToHeight"]) + Decimal(expected_IPB1 * 11))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_and_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-6)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # generate some blocks and check again
        # IPB = previous IPB
        # ITH not updated
        self.nodes[0].generate(10)
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))
        # Take another loan
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])
        expected_IPB1 = Decimal(Decimal('-0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                   ROUND_DOWN)
        assert (storedInterest["interestPerBlock"] != storedInterest1["interestPerBlock"])
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), Decimal(expected_IPB1))
        assert_equal(storedInterest1["interestToHeight"], '0.000000000000000000000000')

        # Repeat process with another loan
        self.nodes[0].generate(10)
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterest2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])
        expected_IPB2 = Decimal(Decimal('-0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                   ROUND_DOWN)
        assert (storedInterest1["interestPerBlock"] != storedInterest2["interestPerBlock"])
        assert_equal(Decimal(storedInterest2["interestPerBlock"]), Decimal(expected_IPB2))
        assert_equal(storedInterest2["interestToHeight"], '0.000000000000000000000000')

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_negative_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * loanAmount).quantize(Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB negative
        self.set_token_interest(interest=-4)
        self.nodes[0].generate(1)

        # Check positive ITH and negative IPB
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02') / Decimal(1051200 * Decimal(loanAmount))).quantize(Decimal('1E-24'),
                                                                                                   ROUND_DOWN)
        expected_ITH = expected_ITH - expected_IPB
        assert_equal(Decimal(storedInterest["interestPerBlock"]), expected_IPB)
        assert_equal(Decimal(storedInterest["interestToHeight"]), expected_ITH)
        assert (Decimal(storedInterest["interestPerBlock"]) < 0)
        assert (Decimal(storedInterest["interestToHeight"]) > 0)

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert (float(interestAmount) > 0)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])

        # Take new loan
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])

        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), expected_IPB)
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(
            '0.000000076103500761034999'))  # expected_ITH = expected_ITH + expected_IPB off by 1E-24 rounding difference

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def takeloan_IPB_positive_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-2)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.01') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(19)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 20

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB positive
        self.set_token_interest(interest=2)
        self.nodes[0].generate(1)

        # Check positive IPB and negative ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert (Decimal(storedInterest["interestPerBlock"]) > 0)
        assert (Decimal(storedInterest["interestToHeight"]) < 0)

        # Take loan
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)

        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])
        expected_IPB = Decimal(Decimal('0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(storedInterest1["interestToHeight"], '0.000000000000000000000000')
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), expected_IPB)

        self.nodes[0].generate(10)
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + self.symboldUSD})
        self.nodes[0].generate(1)

        storedInterest1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        loanAmount = get_decimal_amount(self.nodes[0].getloantokens(vaultId)[0])
        expected_IPB = Decimal(Decimal('0.05') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), expected_IPB)

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_positive_and_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=1)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
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
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
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
        expected_IPB1 = Decimal(Decimal('0.02') / 2 / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                      ROUND_DOWN)
        assert_equal(expected_IPB1, Decimal(storedInterest1["interestPerBlock"]))
        assert_equal(Decimal(storedInterest1["interestToHeight"]), 0)
        assert_equal(Decimal(storedInterest1["interestPerBlock"]),
                     (Decimal(storedInterest["interestPerBlock"]) / 2).quantize(Decimal('1E-24'), ROUND_DOWN))

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_positive_and_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-2)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(19)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN3'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 20

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB positive
        self.set_token_interest(interest=2)
        self.nodes[0].generate(1)
        vault = self.nodes[0].getvault(vaultId)

        # Check positive IPB and negative ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert (Decimal(storedInterest["interestPerBlock"]) > 0)
        assert (Decimal(storedInterest["interestToHeight"]) < 0)

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
        expected_IPB = Decimal(Decimal('0.025') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(Decimal(storedInterest1["interestToHeight"]), 0)  # negative interest was used for payback
        assert_equal(Decimal(storedInterest1["interestPerBlock"]), expected_IPB)

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_negative_and_ITH_positive(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB negative
        self.set_token_interest(interest=-4)
        self.nodes[0].generate(1)

        # Check positive ITH and negative IPB
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        expected_ITH = expected_ITH - expected_IPB
        assert_equal(Decimal(storedInterest["interestPerBlock"]), expected_IPB)
        assert_equal(Decimal(storedInterest["interestToHeight"]), expected_ITH)
        assert (Decimal(storedInterest["interestPerBlock"]) < 0)
        assert (Decimal(storedInterest["interestToHeight"]) > 0)

        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert (float(interestAmount) > 0)
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

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def payback_loan_IPB_negative_and_ITH_negative(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=0)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.01') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(Decimal(0), Decimal(storedInterest["interestToHeight"]))

        # Generate 10 blocks + update vault and check again
        self.nodes[0].generate(9)
        self.nodes[0].updatevault(vaultId, {'loanSchemeId': 'LOAN2'})
        self.nodes[0].generate(1)
        expected_ITH = Decimal(expected_IPB) * 10

        # Check expected IPB and ITH
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                 ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))
        assert_equal(expected_ITH, Decimal(storedInterest["interestToHeight"]))

        # Set IPB negative
        self.set_token_interest(interest=-4)
        self.nodes[0].generate(1)

        # Check negative IPB
        storedInterest = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB = Decimal(Decimal('-0.02') / BLOCKS_PER_YEAR * Decimal(loanAmount)).quantize(Decimal('1E-24'),
                                                                                                  ROUND_DOWN)
        assert_equal(expected_IPB, Decimal(storedInterest["interestPerBlock"]))

        # Accrue enough negative interest to pay part of the loan
        self.nodes[0].generate(10)
        vault = self.nodes[0].getvault(vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert (float(interestAmount) < 0)
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

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def template_fn(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # fn body

        if do_revert:
            self.rollback_to(blockHeight)
            # further check for changes undone

    def vault_in_liquidation_negative_interest(self, do_revert=True):
        blockHeight = self.nodes[0].getblockcount()
        # Init use case
        # Set interest
        self.goto_gw_height()
        self.set_token_interest(interest=-3)
        # Create vault and take loan
        loanScheme = 'LOAN1'
        vaultId = self.new_vault(loanScheme)
        newOwner = self.nodes[0].getnewaddress()
        # new owner for later
        params = {'ownerAddress': newOwner}
        self.nodes[0].updatevault(vaultId, params)
        self.nodes[0].generate(1)

        loanAmount = 5
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)
        storedInterestDUSD = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        expected_IPB_DUSD = Decimal(Decimal('-0.02') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(
            Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB_DUSD, Decimal(storedInterestDUSD["interestPerBlock"]))

        self.update_oracle_price()  # set live oracles price will be the same

        # let vault enter inLiquidation state
        self.set_token_interest(interest=10000000, token=self.idTSLA)
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symbolTSLA})
        self.nodes[0].generate(1)
        # Check expected IPB and ITH
        storedInterestTSLA = self.nodes[0].getstoredinterest(vaultId, self.symbolTSLA)
        expected_IPB_TSLA = Decimal(Decimal('100000.01') * Decimal(loanAmount) / BLOCKS_PER_YEAR).quantize(
            Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB_TSLA, Decimal(storedInterestTSLA["interestPerBlock"]))
        expected_IPB_Value = (expected_IPB_TSLA * 10) + expected_IPB_DUSD

        storedInterestDUSD1 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(storedInterestDUSD["interestPerBlock"], storedInterestDUSD1["interestPerBlock"])

        vault = self.nodes[0].getvault(vaultId, True)
        assert_equal(Decimal(vault["interestPerBlockValue"]), expected_IPB_Value)
        loanAmounts = self.nodes[0].getloantokens(vaultId)
        assert_equal(loanAmounts, ['5.00000000@DUSD', '5.00000000@TSLA'])

        # let vault enter inLiquidation state
        fixedPrice = self.nodes[0].getfixedintervalprice("TSLA/USD")
        blocks_to_price_update = fixedPrice["nextPriceBlock"] - self.nodes[0].getblockcount()
        self.nodes[0].generate(blocks_to_price_update)

        vault = self.nodes[0].getvault(vaultId, True)
        loanAmounts1 = self.nodes[0].getloantokens(vaultId)
        assert_equal(loanAmounts1, [])
        storedInterestTSLA1 = self.nodes[0].getstoredinterest(vaultId, self.symbolTSLA)
        assert_equal(Decimal(storedInterestTSLA1["interestPerBlock"]), Decimal(0))
        assert_equal(Decimal(storedInterestTSLA1["interestToHeight"]), Decimal(0))
        storedInterestDUSD2 = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        assert_equal(Decimal(storedInterestDUSD2["interestPerBlock"]), Decimal(0))
        assert_equal(Decimal(storedInterestDUSD2["interestToHeight"]), Decimal(0))

        auctionList = self.nodes[0].listauctions()
        liquidationHeight = auctionList[0]["liquidationHeight"]
        batchLoanDUSD = get_decimal_amount(auctionList[0]["batches"][0]["loan"])
        assert (batchLoanDUSD < get_decimal_amount(loanAmounts[0]))
        batchLoanTSLA = get_decimal_amount(auctionList[0]["batches"][1]["loan"])
        assert (batchLoanTSLA > get_decimal_amount(loanAmounts[1]))
        account = self.nodes[0].getaccount(newOwner)
        assert_equal(account, ['5.00000000@DUSD', '5.00000000@TSLA'])

        # bid to put vault to active state
        # mint needed tokens
        self.nodes[0].minttokens("5@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("15@TSLA")
        self.nodes[0].generate(1)
        toAmounts = {newOwner: ["5@DUSD", "15@TSLA"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)
        account1 = self.nodes[0].getaccount(newOwner)
        assert_equal(account1, ['10.00000000@DUSD', '20.00000000@TSLA'])

        self.nodes[0].placeauctionbid(vaultId, 1, newOwner,
                                      "11@TSLA")  # above 5% and leave vault with some loan to exit liquidation state
        self.nodes[0].generate(1)  # let auction end
        auctionHistory = self.nodes[0].listauctionhistory(newOwner)
        assert_equal(auctionHistory, [])
        self.nodes[0].generate(liquidationHeight - self.nodes[0].getblockcount() + 1)  # let auction end
        self.set_token_interest(token=self.idTSLA, interest=-4)
        self.set_token_interest(interest=1)

        auctionHistory = self.nodes[0].listauctionhistory(newOwner)[0]
        assert_equal(auctionHistory['winner'], newOwner)
        assert_equal(auctionHistory['blockHeight'], liquidationHeight)
        assert_equal(auctionHistory['vaultId'], vaultId)
        assert_equal(auctionHistory['batchIndex'], 1)
        assert_equal(auctionHistory['auctionBid'], "11.00000000@TSLA")
        assert_equal(auctionHistory['auctionWon'], ['9.53410975@DFI'])

        # Check DUSD is again in vault generating interest
        storedInterestDUSD = self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
        loanAmounts = self.nodes[0].getloantokens(vaultId)
        expected_IPB_DUSD = Decimal(Decimal('0.02') * get_decimal_amount(loanAmounts[0]) / BLOCKS_PER_YEAR).quantize(
            Decimal('1E-24'), ROUND_DOWN)
        assert_equal(expected_IPB_DUSD, Decimal(storedInterestDUSD["interestPerBlock"]))
        assert_equal(storedInterestDUSD["interestToHeight"], '-0.000000285387405821917806')

        # Check TSLA interests are 0
        storedInterestTSLA = self.nodes[0].getstoredinterest(vaultId, self.symbolTSLA)
        assert_equal(Decimal(storedInterestTSLA["interestPerBlock"]), Decimal(0))
        assert_equal(Decimal(storedInterestTSLA["interestToHeight"]), Decimal(0))

        # Check vault is correct
        vault = self.nodes[0].getvault(vaultId, True)
        assert_equal(vault["state"], "mayLiquidate")

        # Check account has won tokens from auction
        accountInfo = self.nodes[0].getaccount(newOwner)
        assert_equal(accountInfo, ['9.53410975@DFI', '10.00000000@DUSD', '9.00000000@TSLA'])

        if do_revert:
            self.rollback_to(blockHeight)
            try:
                self.nodes[0].getstoredinterest(vaultId, self.symboldUSD)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert ("Vault not found" in errorString)
            # further check for changes undone
            attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
            assert_equal(attributes["v0/token/1/loan_minting_interest"], '0')

    def create_tokens(self):
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

    def create_oracles(self):
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

    def update_oracle_price(self):
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                         {"currency": "USD", "tokenAmount": "1@DUSD"},
                         {"currency": "USD", "tokenAmount": "10@GOLD"},
                         {"currency": "USD", "tokenAmount": "10@DFI"}]

        mock_time = int(time.time() + 3000)
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(self.oracle_id1, mock_time, oracle_prices)
        self.nodes[0].generate(120)

    def create_pool_pairs(self):
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
        assert_equal(len(self.nodes[0].listtokens()), 1)  # only one token == DFI
        self.nodes[0].generate(100)
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.create_oracles()
        self.create_tokens()
        self.create_pool_pairs()
        self.nodes[0].createloanscheme(150, 1, 'LOAN1')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(200, 2, 'LOAN2')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(300, 3, 'LOAN3')
        self.nodes[0].generate(10)
        self.setup_height = self.nodes[0].getblockcount()

    def run_test(self):
        rollback = True

        # Initial set up
        self.setup()
        self.update_oracle_price()
        # Auctions
        self.vault_in_liquidation_negative_interest(do_revert=rollback)
        # Update vault
        self.update_oracle_price()
        self.update_vault_IPB_and_ITH_positive(do_revert=rollback)
        self.update_vault_IPB_and_ITH_negative(do_revert=rollback)
        self.update_vault_IPB_negative_ITH_positive(do_revert=rollback)
        self.update_vault_IPB_positive_ITH_negative(do_revert=rollback)
        # Takeloan
        self.update_oracle_price()
        self.takeloan_IPB_and_ITH_positive(do_revert=rollback)
        self.takeloan_IPB_and_ITH_negative(do_revert=rollback)
        self.takeloan_IPB_negative_ITH_positive(do_revert=rollback)
        self.takeloan_IPB_positive_ITH_negative(do_revert=rollback)
        # Payback
        self.update_oracle_price()
        self.payback_loan_IPB_positive_and_ITH_positive(do_revert=rollback)
        self.payback_loan_IPB_positive_and_ITH_negative(do_revert=rollback)
        self.payback_loan_IPB_negative_and_ITH_positive(do_revert=rollback)
        self.payback_loan_IPB_negative_and_ITH_negative(do_revert=rollback)


if __name__ == '__main__':
    GetStoredInterestTest().main()
