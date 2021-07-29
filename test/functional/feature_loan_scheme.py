#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan Scheme."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class CreateLoanSchemeTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=110']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Try and create a loan scheme before Fort Canning
        try:
            self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("called before FortCanning height" in errorString)

        self.nodes[0].generate(9)

        # Create loan scheme
        self.nodes[0].createloanscheme(999, 0.6, 'LOANMAX')
        self.nodes[0].generate(1)

        # Check loan scheme created
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['id'], 'LOANMAX')
        assert_equal(results[0]['mincolratio'], 999)
        assert_equal(results[0]['interestrate'], Decimal('0.60000000'))
        assert_equal(results[0]['default'], True)

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

        # Create one more loan scheme
        self.nodes[0].createloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].generate(1)

        # Check loan scheme created
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['id'], 'LOAN0001')
        assert_equal(results[0]['mincolratio'], 150)
        assert_equal(results[0]['interestrate'], Decimal('5.00000000'))
        assert_equal(results[0]['default'], False)

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

        # Update loan scheme
        self.nodes[0].updateloanscheme(1001, 0.7, 'LOANMAX')
        self.nodes[0].generate(1)

        # Check loan scheme updated
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1001)
        assert_equal(results[1]['interestrate'], Decimal('0.70000000'))
        assert_equal(results[1]['default'], True)

        # Try and update a loan scheme below the current height
        try:
            self.nodes[0].updateloanscheme(1000, 0.5, 'LOANMAX', 112)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Update height below current block height, set future height" in errorString)

        # Update loan scheme after a delay
        self.nodes[0].updateloanscheme(1000, 0.5, 'LOANMAX', 115)
        self.nodes[0].generate(1)

        # Check loan scheme not updated
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1001)
        assert_equal(results[1]['interestrate'], Decimal('0.70000000'))
        assert_equal(results[1]['default'], True)

        # Try and change another loan scheme to pending loan scheme
        try:
            self.nodes[0].updateloanscheme(1000, 0.5, 'LOAN0001')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan scheme LOANMAX with same interestrate and mincolratio pending on block 115" in errorString)

        # Move to update block and check loan scheme has updated
        self.nodes[0].generate(1)
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1000)
        assert_equal(results[1]['interestrate'], Decimal('0.50000000'))
        assert_equal(results[1]['default'], True)

        # Test rollback reverts delayed update block
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1001)
        assert_equal(results[1]['interestrate'], Decimal('0.70000000'))
        assert_equal(results[1]['default'], True)

        # Move forward again to update block and check loan scheme has updated
        self.nodes[0].generate(1)
        results = self.nodes[0].listloanschemes()
        assert_equal(results[1]['id'], 'LOANMAX')
        assert_equal(results[1]['mincolratio'], 1000)
        assert_equal(results[1]['interestrate'], Decimal('0.50000000'))
        assert_equal(results[1]['default'], True)

        # Create more loan schemes
        self.nodes[0].createloanscheme(175, 3, 'LOAN0002')
        self.nodes[0].createloanscheme(200, 2, 'LOAN0003')
        self.nodes[0].createloanscheme(350, 1.5, 'LOAN0004')
        self.nodes[0].createloanscheme(500, 1, 'LOAN0005')
        self.nodes[0].generate(1)

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

        # Test changing the loan scheme
        self.nodes[0].setdefaultloanscheme('LOAN0001')
        self.nodes[0].generate(1)
        results = self.nodes[0].listloanschemes()
        assert_equal(results[0]['id'], 'LOAN0001')
        assert_equal(results[0]['default'], True)
        assert_equal(results[1]['id'], 'LOAN0002')
        assert_equal(results[1]['default'], False)
        assert_equal(results[2]['id'], 'LOAN0003')
        assert_equal(results[2]['default'], False)
        assert_equal(results[3]['id'], 'LOAN0004')
        assert_equal(results[3]['default'], False)
        assert_equal(results[4]['id'], 'LOAN0005')
        assert_equal(results[4]['default'], False)
        assert_equal(results[5]['id'], 'LOANMAX')
        assert_equal(results[5]['default'], False)

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

        # Destroy a loan scheme
        self.nodes[0].destroyloanscheme('LOANMAX')
        self.nodes[0].generate(1)
        results = self.nodes[0].listloanschemes()
        assert_equal(len(results), 5)
        assert_equal(results[0]['id'], 'LOAN0001')
        assert_equal(results[1]['id'], 'LOAN0002')
        assert_equal(results[2]['id'], 'LOAN0003')
        assert_equal(results[3]['id'], 'LOAN0004')
        assert_equal(results[4]['id'], 'LOAN0005')

        # Create delayed loan scheme destruction
        destruction_height = self.nodes[0].getblockcount() + 3
        self.nodes[0].destroyloanscheme('LOAN0002', destruction_height)
        self.nodes[0].generate(1)

        # Loan scheme should still be present
        results = self.nodes[0].listloanschemes()
        assert_equal(len(results), 5)
        assert_equal(results[1]['id'], 'LOAN0002')

        # Try and set the loan scheme to be destroyed as default
        try:
            self.nodes[0].setdefaultloanscheme('LOAN0002')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as default, set to destroyed on block 121" in errorString)

        # Set update on same block and later on
        self.nodes[0].updateloanscheme(160, 4.5, 'LOAN0002', destruction_height)
        self.nodes[0].updateloanscheme(170, 4, 'LOAN0002', destruction_height + 10)

        # Move to destrction block and check
        self.nodes[0].generate(2)
        results = self.nodes[0].listloanschemes()
        assert_equal(len(results), 4)
        assert_equal(results[1]['id'], 'LOAN0003')

        # Rollback to make sure loan restored
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        results = self.nodes[0].listloanschemes()
        assert_equal(len(results), 5)
        assert_equal(results[1]['id'], 'LOAN0002')

        # Cannot update loan scheme due to pending update
        try:
            self.nodes[0].updateloanscheme(170, 4, 'LOAN0003')
        except JSONRPCException as e:
            errorString = e.error['message']
        print(errorString)
        assert("Loan scheme LOAN0002 with same interestrate and mincolratio pending on block 131" in errorString)

        # Go forward again to destroy loan
        self.nodes[0].generate(2)
        results = self.nodes[0].listloanschemes()
        assert_equal(len(results), 4)
        assert_equal(results[1]['id'], 'LOAN0003')

        # Can now update loan scheme as pending updates deleted
        self.nodes[0].updateloanscheme(170, 4, 'LOAN0003')

        # VAULT TESTS

        # Create vault with invalid address
        try:
            self.nodes[0].createvault('ffffffffff')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Error: Invalid owneraddress address' in errorString)

        # Create vault with invalid loanschemeid and default owneraddress
        try:
            self.nodes[0].createvault('', 'FAKELOAN')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Cannot find existing loan scheme with id' in errorString)

        # create 2 vaults
        vaultId1 = self.nodes[0].createvault('') # default loan scheme
        owneraddress2 = self.nodes[0].getnewaddress('', 'legacy')
        self.nodes[0].generate(1)
        vaultId2 = self.nodes[0].createvault(owneraddress2, 'LOAN0003')

        self.nodes[0].generate(1)
        # check listvaults
        listVaults = self.nodes[0].listvaults()
        assert(listVaults[vaultId1])
        assert(listVaults[vaultId2])
        owneraddress1 = listVaults[vaultId1]['owneraddress']
        # assert default loanscheme was assigned correctly
        assert_equal(listVaults[vaultId1]['loanschemeid'], 'LOAN0001')
        assert_equal(listVaults[vaultId1]['owneraddress'], owneraddress1)
        # assert non-default loanscheme was assigned correctly
        assert_equal(listVaults[vaultId2]['loanschemeid'], 'LOAN0003')
        assert_equal(listVaults[vaultId2]['owneraddress'], owneraddress2)


if __name__ == '__main__':
    CreateLoanSchemeTest().main()
