#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan Scheme."""

from typing_extensions import assert_never
from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class CreateLoanSchemeTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=110'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=110']
            ]

    def goto_block(self, block_number, node_number=0):
        current_block = self.nodes[node_number].getblockcount()
        if block_number > current_block:
            self.nodes[node_number].generate(block_number - current_block)

    def setup(self):
        self.nodes[0].generate(101)

    def create_loanscheme_before_Fort_Canning(self):
        # Try and create a loan scheme before Fort Canning
        try:
            self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("called before FortCanning height" in errorString)

        # Go to Fort Canning height
        self.nodes[0].generate(9)

    def create_update_loanscheme_success(self, ratio, interest, name, after_block, default=False, update=False):
        # Create update loan scheme
        args = [ratio, interest, name]
        if after_block:
            args.append(after_block)

        if update:
            self.nodes[0].updateloanscheme(*args)
        else:
            self.nodes[0].createloanscheme(*args)
        self.nodes[0].generate(1)

        if after_block is None:
            # Check loan scheme created
            result = self.nodes[0].getloanscheme(name)
            assert_equal(result['id'], name)
            assert_equal(result['mincolratio'], ratio)
            assert_equal(Decimal(result['interestrate']), Decimal(str(interest)))
            assert_equal(result['default'], default)

    def test_create_loanscheme_wrong_params(self):
        # Try and create a loan scheme with duplicate name
        try:
            self.nodes[0].createloanscheme(100, 1, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme already exist with id LOANMAX" in errorString)

        # Try and create a loan scheme with duplicate ratio and rate
        try:
            self.nodes[0].createloanscheme(999, 0.6, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme LOANMAX with same interestrate and mincolratio already exists" in errorString)

        # Try and create a loan scheme with too small ratio
        try:
            self.nodes[0].createloanscheme(99, 0.5, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("minimum collateral ratio cannot be less than 100" in errorString)

        # Try and create a loan scheme with too small rate
        try:
            self.nodes[0].createloanscheme(1000, 0.009, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("interest rate cannot be less than 0.01" in errorString)

        # Try and create a loan scheme without ID
        try:
            self.nodes[0].createloanscheme(1000, 0.5, '')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Try and create a loan scheme with too long an ID
        try:
            self.nodes[0].createloanscheme(1000, 0.5, 'XXXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

    def test_getloanscheme_errors(self):
        # Try to get a loan scheme with too long an ID
        try:
            self.nodes[0].getloanscheme('scheme123')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Try getloanscheme with wrong id
        try:
            self.nodes[0].getloanscheme('scheme12')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id " in errorString)

    def test_get_list_loanscheme_success(self):
        # Get loan scheme
        loanscheme = self.nodes[0].getloanscheme('LOAN0001')
        assert_equal(loanscheme['id'], 'LOAN0001')
        assert_equal(loanscheme['mincolratio'], 150)
        assert_equal(loanscheme['interestrate'], Decimal('5.00000000'))

        # Check loan scheme created
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['id'], 'LOAN0001')
        assert_equal(results[0]['mincolratio'], 150)
        assert_equal(results[0]['interestrate'], Decimal('5.00000000'))
        assert_equal(results[0]['default'], False)

    def update_loanscheme_errors(self):
        # Try and update a loan scheme with a non-existant name
        try:
            self.nodes[0].updateloanscheme(1000, 0.5, 'XXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id XXXXXXXX" in errorString)

        # Try and update a loan scheme with same rate and ratio as another scheme
        try:
            self.nodes[0].updateloanscheme(150, 5, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme LOAN0001 with same interestrate and mincolratio already exists" in errorString)

    def test_error_update_wrong_height(self):
        # Try and update a loan scheme below the current height
        try:
            self.nodes[0].updateloanscheme(1000, 0.5, 'LOANMAX', 112)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Update height below current block height, set future height" in errorString)

    def test_update_to_same_programmed_values(self):
        # Try and change another loan scheme to pending loan scheme
        try:
            self.nodes[0].updateloanscheme(1000, 0.5, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme LOANMAX with same interestrate and mincolratio pending on block 142" in errorString)

    def goto_update_height_test_update_success(self):
        # Move to update block and check loan scheme has updated
        self.goto_block(142)
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1000)
        assert_equal(results[1]['interestrate'], Decimal('0.50000000'))
        assert_equal(results[1]['default'], True)

    def rollback_and_revert_update(self):
        # Test rollback reverts delayed update block
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1001)
        assert_equal(results[1]['interestrate'], Decimal('0.70000000'))
        assert_equal(results[1]['default'], True)

    def test_listloanschemes_order(self):
        # Check all loan schemes created and shown in order
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['id'], 'LOAN0001')
        assert_equal(results[0]['mincolratio'], 150)
        assert_equal(results[0]['interestrate'], Decimal('5.00000000'))
        assert_equal(results[0]['default'], False)
        assert_equal(results[1]['id'], 'LOAN0002')
        assert_equal(results[1]['mincolratio'], 175)
        assert_equal(results[1]['interestrate'], Decimal('3.00000000'))
        assert_equal(results[1]['default'], False)
        assert_equal(results[2]['id'], 'LOAN0003')
        assert_equal(results[2]['mincolratio'], 200)
        assert_equal(results[2]['interestrate'], Decimal('2.00000000'))
        assert_equal(results[2]['default'], False)
        assert_equal(results[3]['id'], 'LOAN0004')
        assert_equal(results[3]['mincolratio'], 350)
        assert_equal(results[3]['interestrate'], Decimal('1.50000000'))
        assert_equal(results[3]['default'], False)
        assert_equal(results[4]['id'], 'LOAN0005')
        assert_equal(results[4]['mincolratio'], 500)
        assert_equal(results[4]['interestrate'], Decimal('1.00000000'))
        assert_equal(results[4]['default'], False)
        assert_equal(results[5]['id'], 'LOANMAX')
        assert_equal(results[5]['mincolratio'], 1000)
        assert_equal(results[5]['interestrate'], Decimal('0.50000000'))
        assert_equal(results[5]['default'], True)

    def set_default_loanscheme_errors(self):
        # Test changing the default loan scheme without ID
        try:
            self.nodes[0].setdefaultloanscheme('')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Test changing the default loan scheme with too long an ID
        try:
            self.nodes[0].setdefaultloanscheme('XXXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Test changing the default loan scheme to one that does not exist
        try:
            self.nodes[0].setdefaultloanscheme('XXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id XXXXXXXX" in errorString)

        # Test changing the default loan scheme to the existing loan scheme
        try:
            self.nodes[0].setdefaultloanscheme('LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme with id LOANMAX is already set as default" in errorString)

    def set_default_loanscheme_success(self, default_loanscheme_name):
        self.nodes[0].setdefaultloanscheme(default_loanscheme_name)
        self.nodes[0].generate(1)
        results = self.nodes[0].listloanschemes()
        for loanscheme in results:
            if loanscheme['id'] == default_loanscheme_name:
                assert_equal(loanscheme['default'], True)
            else:
                assert_equal(loanscheme['default'], False)

    def destroy_loanscheme_errors(self):
        # Test destroying a loan scheme without ID
        try:
            self.nodes[0].destroyloanscheme('')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Test destroying a loan scheme with too long an ID
        try:
            self.nodes[0].destroyloanscheme('XXXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id cannot be empty or more than 8 chars long" in errorString)

        # Test destroying a loan scheme that does not exist
        try:
            self.nodes[0].destroyloanscheme('XXXXXXXX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id XXXXXXXX" in errorString)

        # Test destroying the default loan scheme
        try:
            self.nodes[0].destroyloanscheme('LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot destroy default loan scheme, set new default first" in errorString)

    def destroy_loanscheme_successs(self, loanscheme_name):
        resultsBefore = self.nodes[0].listloanschemes()

        self.nodes[0].destroyloanscheme(loanscheme_name)
        self.nodes[0].generate(1)

        resultsAfter = self.nodes[0].listloanschemes()
        assert_equal(len(resultsAfter), len(resultsBefore)-1)
        for loanscheme in resultsAfter:
            assert(loanscheme['id'] != loanscheme_name)

    def test_delayed_destroy_loanscheme(self, loanscheme_name, destruction_height, rollback=False):
        resultsBefore = self.nodes[0].listloanschemes()
        # Create delayed loan scheme destruction
        self.nodes[0].destroyloanscheme(loanscheme_name, destruction_height)
        self.nodes[0].generate(2)

        # Loan scheme should still be present
        resultsAfter = self.nodes[0].listloanschemes()
        assert_equal(len(resultsAfter), len(resultsBefore))
        assert_equal(resultsAfter[1]['id'], loanscheme_name)

        if rollback:
            rollback_height = self.nodes[0].getblockcount()
            self.goto_block(destruction_height)

            resultsAfterDelay = self.nodes[0].listloanschemes()
            assert_equal(len(resultsAfterDelay), len(resultsAfter)-1)
            for loanscheme in resultsAfterDelay:
                assert(loanscheme['id'] != loanscheme_name)

            # rollback
            self.nodes[0].invalidateblock(self.nodes[0].getblockhash(rollback_height))

    def set_default_loanscheme_to_loanscheme_to_be_destroyed(self, loanscheme_name):
        # Try and set the loan scheme to be destroyed as default
        try:
            self.nodes[0].setdefaultloanscheme(loanscheme_name)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as default, set to destroyed on block 187" in errorString)

    def update_loanscheme_to_same_interest_and_ratio_than_pending_update(self):
        # Cannot update loan scheme due to pending update
        try:
            self.nodes[0].updateloanscheme(170, 4, 'LOAN0003')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme LOAN0002 with same interestrate and mincolratio pending on block 197" in errorString)

    def loansecheme_check_not_exists(self, loanscheme_name):
        results = self.nodes[0].listloanschemes()
        for loanscheme in results:
            assert(loanscheme['id'] != loanscheme_name)

    def update_existent_loan_to_destroyed_loan_parameters(self):
        # Can now update loan scheme as pending updates deleted
        self.nodes[0].updateloanscheme(170, 4, 'LOAN0003')

    def run_test(self):
        self.setup()

        self.create_loanscheme_before_Fort_Canning()
        self.create_update_loanscheme_success(ratio=999, interest=0.6, name='LOANMAX',  after_block=None, default=True)
        self.test_create_loanscheme_wrong_params()
        self.create_update_loanscheme_success(ratio=150, interest=5, name='LOAN0001', after_block=None)
        self.test_getloanscheme_errors()
        self.test_get_list_loanscheme_success()
        self.update_loanscheme_errors()
        self.create_update_loanscheme_success(ratio=1001, interest=0.7, name='LOANMAX', after_block=None, default=True, update=True)
        self.test_error_update_wrong_height()
        self.create_update_loanscheme_success(ratio=1000, interest=0.5, name='LOANMAX', after_block=142, default=True,  update=True)
        self.test_update_to_same_programmed_values()
        self.goto_update_height_test_update_success()
        self.rollback_and_revert_update()
        self.goto_update_height_test_update_success()
        self.create_update_loanscheme_success(ratio=175, interest=3, name='LOAN0002', after_block=None)
        self.create_update_loanscheme_success(ratio=200, interest=2, name='LOAN0003', after_block=None)
        self.create_update_loanscheme_success(ratio=350, interest=1.5, name='LOAN0004', after_block=None)
        self.create_update_loanscheme_success(ratio=500, interest=1, name='LOAN0005', after_block=None)
        self.test_listloanschemes_order()
        self.set_default_loanscheme_errors()
        self.set_default_loanscheme_success('LOAN0001')
        self.destroy_loanscheme_errors()
        self.destroy_loanscheme_successs('LOANMAX')
        self.test_delayed_destroy_loanscheme('LOAN0002', 187)
        self.set_default_loanscheme_to_loanscheme_to_be_destroyed('LOAN0002')
        # Set update on same block and later on
        self.create_update_loanscheme_success(ratio=160, interest=4.5, name='LOAN0002', after_block=187, update=True)
        self.create_update_loanscheme_success(ratio=170, interest=4, name='LOAN0002', after_block=197, update=True)
        self.test_delayed_destroy_loanscheme('LOAN0002', 187, rollback=True)
        self.update_loanscheme_to_same_interest_and_ratio_than_pending_update()
        # Go forward and test loanscheme is destroyed
        self.goto_block(187)
        self.loansecheme_check_not_exists('LOAN0002')
        self.update_existent_loan_to_destroyed_loan_parameters()

if __name__ == '__main__':
    CreateLoanSchemeTest().main()
