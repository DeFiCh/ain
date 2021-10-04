#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan Scheme."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

class VaultTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(120)
        self.nodes[0].createloanscheme(175, 3, 'LOAN0001')
        self.nodes[0].createloanscheme(200, 2, 'LOAN0002')
        self.nodes[0].createloanscheme(350, 1.5, 'LOAN0003')
        self.nodes[0].createloanscheme(550, 1.5, 'LOAN0004')
        self.nodes[0].generate(1)

        self.nodes[0].setdefaultloanscheme('LOAN0001')

        self.nodes[0].generate(1)
        # VAULT TESTS
        # Create vault with invalid address
        try:
            self.nodes[0].createvault('ffffffffff')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Error: Invalid owner address' in errorString)

        # Create vault with invalid loanschemeid and default owner address
        try:
            self.nodes[0].createvault('', 'FAKELOAN')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Cannot find existing loan scheme with id FAKELOAN' in errorString)

        # create 4 vaults
        ownerAddress1 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId1 = self.nodes[0].createvault(ownerAddress1) # default loan scheme

        ownerAddress2 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId2 = self.nodes[0].createvault(ownerAddress2, 'LOAN0001')
        self.nodes[0].createvault(ownerAddress2, 'LOAN0003')
        self.nodes[0].createvault(ownerAddress2, 'LOAN0003')
        self.nodes[0].generate(1)

        # check listvaults
        listVaults = self.nodes[0].listvaults()
        assert(len(listVaults) == 4)

        # check listVaults filter by ownerAddres
        listVaults = self.nodes[0].listvaults({ "ownerAddress": ownerAddress2 })
        assert(len(listVaults) == 3)
        for vault in listVaults:
            assert(vault["ownerAddress"] == ownerAddress2)

        # check listVaults filter by loanSchemeId
        listVaults = self.nodes[0].listvaults({ "loanSchemeId": "LOAN0003" })
        assert(len(listVaults) == 2)
        for vault in listVaults:
            assert(vault["loanSchemeId"] == "LOAN0003")

        # check listVaults pagination
        listVaults = self.nodes[0].listvaults({}, {"limit": 1})
        assert(len(listVaults) == 1)

        # check getvault

        # fail
        try:
            self.nodes[0].getvault('5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Vault <5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf> not found' in errorString)

        # success
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1["loanSchemeId"], 'LOAN0001')
        assert_equal(vault1["ownerAddress"], ownerAddress1)
        assert_equal(vault1["isUnderLiquidation"], False)
        assert_equal(vault1["collateralAmounts"], [])
        assert_equal(vault1["loanAmount"], [])
        assert_equal(vault1["collateralValue"], Decimal(0))
        assert_equal(vault1["loanValue"], Decimal(0))
        assert_equal(vault1["currentRatio"], -1)

        # updateVault

        # fail
        try:
            params = {}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least ownerAddress OR loanSchemeId must be set" in errorString)

        # bad loan scheme id
        try:
            params = {'loanSchemeId': 'FAKELOAN'}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id FAKELOAN" in errorString)

        # bad owner address
        try:
            params = {'ownerAddress': 'ffffffffff'}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Error: Invalid owner address" in errorString)

        # Create or update vault with loan scheme planned to be destroyed
        destruction_height = self.nodes[0].getblockcount() + 3
        self.nodes[0].destroyloanscheme('LOAN0002', destruction_height)
        self.nodes[0].generate(1)

        # create
        try:
            self.nodes[0].createvault('', 'LOAN0002') # default loan scheme
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as loan scheme, set to be destroyed on block 126" in errorString)

        # update
        try:
            params = {'loanSchemeId':'LOAN0002'}
            self.nodes[0].updatevault(vaultId2, params) # default loan scheme
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as loan scheme, set to be destroyed on block 126" in errorString)

        # check owner address auth
        othersAddress = self.nodes[1].getnewaddress('', 'legacy')
        self.nodes[1].generate(1)
        try:
            self.nodes[0].createvault(othersAddress)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Incorrect authorization for '+othersAddress in errorString)

        # update vault scheme
        newAddress = self.nodes[0].getnewaddress('', 'legacy')
        params = {'loanSchemeId': 'LOAN0001', 'ownerAddress': newAddress}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['ownerAddress'], newAddress)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

        # update with non-default loan scheme and delete loan to check automatic update
        params = {'loanSchemeId': 'LOAN0003'}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0003')

        self.nodes[0].destroyloanscheme('LOAN0003')
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

        # back to non-default loan scheme and delete scheme with delay
        params = {'loanSchemeId': 'LOAN0004'}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0004')

        destruction_height = self.nodes[0].getblockcount() + 2
        self.nodes[0].destroyloanscheme('LOAN0004', destruction_height)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0004')

        # now LOAN0002 is deleted in next block
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

if __name__ == '__main__':
    VaultTest().main()
